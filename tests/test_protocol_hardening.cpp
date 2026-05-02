#include <gtest/gtest.h>

#include <limits>
#include <vector>

#include <polycpp/mysql2/mysql2.hpp>

#include "support/mysql2_test_utils.hpp"

namespace mysql2 = polycpp::mysql2;
namespace test = polycpp::mysql2::test;

TEST(protocol_hardening, upstream_sql_format_fixture_edges) {
    EXPECT_EQ(mysql2::escape(std::string("a\0b\nc\r\t\b\x1a'\"\\", 12)), "'a\\0b\\nc\\r\\t\\b\\Z\\'\\\"\\\\'");
    EXPECT_EQ(mysql2::escape(std::numeric_limits<double>::infinity()), "NULL");
    EXPECT_EQ(mysql2::escape(std::numeric_limits<double>::quiet_NaN()), "NULL");
    EXPECT_EQ(mysql2::escape_id("db.users.name"), "`db`.`users`.`name`");
    EXPECT_EQ(mysql2::format("SELECT ??, ? FROM ??", {std::string("user`name"), int64_t{7}, std::string("accounts")}),
              "SELECT `user``name`, 7 FROM `accounts`");
    EXPECT_EQ(mysql2::format_named("SELECT ':literal', :id, `:identifier`", {{"id", uint64_t{9}}}),
              "SELECT ':literal', 9, `:identifier`");
    EXPECT_THROW((void)mysql2::format_named("SELECT :missing", {}), mysql2::Error);
}

TEST(protocol_hardening, malformed_binlog_packets_fail_closed) {
    EXPECT_THROW(mysql2::parse_binlog_event_packet(mysql2::Buffer{}), mysql2::Error);
    EXPECT_THROW(mysql2::parse_binlog_event_packet(mysql2::Buffer::from({0x01, 0x00, 0x00})), mysql2::Error);
    EXPECT_THROW(mysql2::parse_binlog_event_packet(mysql2::Buffer::from({0x00, 0x01, 0x02})), mysql2::Error);

    mysql2::BinlogParser parser;
    std::vector<uint8_t> truncated_row_body;
    test::append_u48_le(truncated_row_body, 42);
    EXPECT_THROW(parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::WRITE_ROWS_V1, truncated_row_body)),
                 mysql2::Error);
}

TEST(protocol_hardening, unsupported_binlog_column_type_fails_closed) {
    std::vector<uint8_t> table_map_body;
    test::append_table_map_prefix(table_map_body, 100, "test", "unsupported", {mysql2::constants::column_type::VECTOR});
    test::append_u8(table_map_body, 0);
    test::append_u8(table_map_body, 0);

    mysql2::BinlogParser parser;
    parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::TABLE_MAP, table_map_body));

    std::vector<uint8_t> row_body;
    test::append_write_rows_prefix(row_body, 100, 1);
    test::append_u8(row_body, 0x00);
    test::append_u8(row_body, 0x01);
    EXPECT_THROW(parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::WRITE_ROWS_V1, row_body)),
                 mysql2::Error);
}

TEST(protocol_hardening, invalid_temporal_precision_fails_closed) {
    std::vector<uint8_t> table_map_body;
    test::append_table_map_prefix(table_map_body, 101, "test", "bad_time", {mysql2::constants::column_type::TIME2});
    test::append_u8(table_map_body, 1);
    test::append_u8(table_map_body, 7);
    test::append_u8(table_map_body, 0);

    mysql2::BinlogParser parser;
    parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::TABLE_MAP, table_map_body));

    std::vector<uint8_t> row_body;
    test::append_write_rows_prefix(row_body, 101, 1);
    test::append_u8(row_body, 0x00);
    test::append_u24_be(row_body, 0x800000);
    EXPECT_THROW(parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::WRITE_ROWS_V1, row_body)),
                 mysql2::Error);
}

TEST(protocol_hardening, binlog_decodes_less_common_row_column_families) {
    using mysql2::constants::column_type::BIT;
    using mysql2::constants::column_type::BLOB;
    using mysql2::constants::column_type::DATE;
    using mysql2::constants::column_type::DATETIME;
    using mysql2::constants::column_type::DOUBLE;
    using mysql2::constants::column_type::ENUM;
    using mysql2::constants::column_type::FLOAT;
    using mysql2::constants::column_type::INT24;
    using mysql2::constants::column_type::JSON;
    using mysql2::constants::column_type::LONGLONG;
    using mysql2::constants::column_type::NEWDECIMAL;
    using mysql2::constants::column_type::SET;
    using mysql2::constants::column_type::SHORT;
    using mysql2::constants::column_type::TIME;
    using mysql2::constants::column_type::TIMESTAMP;
    using mysql2::constants::column_type::TINY;
    using mysql2::constants::column_type::TINY_BLOB;
    using mysql2::constants::column_type::YEAR;

    const std::vector<uint8_t> column_types = {
        TINY, SHORT, INT24, LONGLONG, FLOAT, DOUBLE, YEAR, DATE, TIME, DATETIME,
        TIMESTAMP, NEWDECIMAL, BIT, TINY_BLOB, BLOB, JSON, ENUM, SET};

    std::vector<uint8_t> metadata;
    test::append_u8(metadata, 4);
    test::append_u8(metadata, 8);
    test::append_u16_le(metadata, (5u << 8) | 2u);
    test::append_u16_le(metadata, (1u << 8) | 2u);
    test::append_u8(metadata, 1);
    test::append_u8(metadata, 2);
    test::append_u8(metadata, 1);
    test::append_u16_le(metadata, 1);
    test::append_u16_le(metadata, 1);

    std::vector<uint8_t> table_map_body;
    test::append_table_map_prefix(table_map_body, 102, "test", "families", column_types);
    test::append_lenenc_int(table_map_body, metadata.size());
    table_map_body.insert(table_map_body.end(), metadata.begin(), metadata.end());
    test::append_u8(table_map_body, 0);
    test::append_u8(table_map_body, 0);
    test::append_u8(table_map_body, 0);

    mysql2::BinlogParser parser;
    parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::TABLE_MAP, table_map_body));

    std::vector<uint8_t> row_body;
    test::append_write_rows_prefix(row_body, 102, column_types.size());
    test::append_u8(row_body, 0x00);
    test::append_u8(row_body, 0x00);
    test::append_u8(row_body, 0x00);
    test::append_i8(row_body, -5);
    test::append_i16_le(row_body, -1234);
    test::append_i24_le(row_body, -123456);
    test::append_i64_le(row_body, -1234567890123LL);
    test::append_float_le(row_body, 1.5f);
    test::append_double_le(row_body, 2.25);
    test::append_u8(row_body, 124);
    test::append_u24_le(row_body, (2024u << 9) | (5u << 5) | 6u);
    test::append_i24_le(row_body, 3723);
    test::append_u64_le(row_body, 20240506070809ULL);
    test::append_u32_le(row_body, 1234567890u);
    test::append_u8(row_body, 0x80);
    test::append_u8(row_body, 0x7b);
    test::append_u8(row_body, 0x2d);
    test::append_u8(row_body, 0x02);
    test::append_u8(row_body, 0xaa);
    test::append_u8(row_body, 3);
    test::append_string(row_body, "abc");
    test::append_u16_le(row_body, 4);
    test::append_string(row_body, "blob");
    test::append_u8(row_body, 7);
    test::append_string(row_body, R"({"a":1})");
    test::append_u8(row_body, 2);
    test::append_u8(row_body, 5);

    const auto event = parser.parse(test::make_binlog_event(mysql2::constants::binlog_event_type::WRITE_ROWS_V1, row_body));
    ASSERT_EQ(event.row_changes.size(), 1u);
    const auto& row = event.row_changes[0].after;
    ASSERT_EQ(row.size(), column_types.size());
    EXPECT_EQ(std::get<int64_t>(row[0]), -5);
    EXPECT_EQ(std::get<int64_t>(row[1]), -1234);
    EXPECT_EQ(std::get<int64_t>(row[2]), -123456);
    EXPECT_EQ(std::get<int64_t>(row[3]), -1234567890123LL);
    EXPECT_DOUBLE_EQ(std::get<double>(row[4]), 1.5);
    EXPECT_DOUBLE_EQ(std::get<double>(row[5]), 2.25);
    EXPECT_EQ(std::get<int64_t>(row[6]), 2024);
    EXPECT_EQ(std::get<std::string>(row[7]), "2024-05-06");
    EXPECT_EQ(std::get<std::string>(row[8]), "01:02:03");
    EXPECT_EQ(std::get<std::string>(row[9]), "2024-05-06 07:08:09");
    EXPECT_EQ(std::get<uint64_t>(row[10]), 1234567890u);
    EXPECT_EQ(std::get<std::string>(row[11]), "123.45");
    EXPECT_EQ(std::get<uint64_t>(row[12]), 0x02aau);
    EXPECT_EQ(std::get<mysql2::Buffer>(row[13]).toString(), "abc");
    EXPECT_EQ(std::get<mysql2::Buffer>(row[14]).toString(), "blob");
    EXPECT_EQ(std::get<mysql2::Buffer>(row[15]).toString(), R"({"a":1})");
    EXPECT_EQ(std::get<uint64_t>(row[16]), 2u);
    EXPECT_EQ(std::get<uint64_t>(row[17]), 5u);
}
