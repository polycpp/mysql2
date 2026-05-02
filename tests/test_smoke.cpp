#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <polycpp/event_loop.hpp>
#include <polycpp/mysql2/mysql2.hpp>

namespace mysql2 = polycpp::mysql2;

namespace {

void trace_step(const char* step) {
    if (std::getenv("MYSQL2_TEST_TRACE")) {
        std::cerr << "[mysql2-test] " << step << '\n';
    }
}

void trace_result(const char* step, const mysql2::QueryResult& result) {
    if (std::getenv("MYSQL2_TEST_TRACE")) {
        std::cerr << "[mysql2-test] " << step << " fields=" << result.fields.size()
                  << " rows=" << result.rows.size()
                  << " status=0x" << std::hex << result.ok.server_status << std::dec << '\n';
    }
}

std::string unique_socket_path() {
    static std::atomic<unsigned> counter{0};
    std::ostringstream name;
    name << "polycpp-mysql2-" << std::chrono::steady_clock::now().time_since_epoch().count()
         << "-" << counter++ << ".sock";
    return (std::filesystem::temp_directory_path() / name.str()).string();
}

bool env_enabled(const char* name) {
    const char* value = std::getenv(name);
    return value && std::string(value) != "0" && std::string(value) != "false";
}

uint64_t value_as_u64(const mysql2::Value& value) {
    if (const auto* u = std::get_if<uint64_t>(&value)) return *u;
    if (const auto* i = std::get_if<int64_t>(&value)) return static_cast<uint64_t>(*i);
    if (const auto* d = std::get_if<double>(&value)) return static_cast<uint64_t>(*d);
    if (const auto* s = std::get_if<std::string>(&value)) return static_cast<uint64_t>(std::stoull(*s));
    throw mysql2::Error("test expected numeric mysql2 value");
}

std::string value_as_string(const mysql2::Value& value) {
    if (const auto* s = std::get_if<std::string>(&value)) return *s;
    if (const auto* b = std::get_if<mysql2::Buffer>(&value)) return b->toString();
    throw mysql2::Error("test expected string mysql2 value");
}

void append_u8(std::vector<uint8_t>& payload, uint8_t value) {
    payload.push_back(value);
}

void append_u16_le(std::vector<uint8_t>& payload, uint16_t value) {
    payload.push_back(static_cast<uint8_t>(value & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
}

void append_u24_be(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(value & 0xff));
}

void append_u32_le(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>(value & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
}

void append_u32_be(std::vector<uint8_t>& payload, uint32_t value) {
    payload.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
    payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    payload.push_back(static_cast<uint8_t>(value & 0xff));
}

void append_u40_be(std::vector<uint8_t>& payload, uint64_t value) {
    for (int i = 4; i >= 0; --i) {
        payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void append_u48_le(std::vector<uint8_t>& payload, uint64_t value) {
    for (int i = 0; i < 6; ++i) {
        payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void append_u64_le(std::vector<uint8_t>& payload, uint64_t value) {
    for (int i = 0; i < 8; ++i) {
        payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
    }
}

void append_string(std::vector<uint8_t>& payload, const std::string& value) {
    payload.insert(payload.end(), value.begin(), value.end());
}

mysql2::Buffer make_binlog_event(uint8_t type, const std::vector<uint8_t>& body) {
    std::vector<uint8_t> payload;
    append_u8(payload, 0x00);
    append_u32_le(payload, 1);
    append_u8(payload, type);
    append_u32_le(payload, 99);
    append_u32_le(payload, static_cast<uint32_t>(19 + body.size()));
    append_u32_le(payload, 1234);
    append_u16_le(payload, 0);
    payload.insert(payload.end(), body.begin(), body.end());
    return mysql2::Buffer::from(payload.data(), payload.size());
}

}  // namespace

TEST(sql_escape, scalar_values) {
    EXPECT_EQ(mysql2::escape(nullptr), "NULL");
    EXPECT_EQ(mysql2::escape(true), "true");
    EXPECT_EQ(mysql2::escape(false), "false");
    EXPECT_EQ(mysql2::escape(int64_t{-42}), "-42");
    EXPECT_EQ(mysql2::escape(uint64_t{42}), "42");
    EXPECT_EQ(mysql2::escape(std::string("O'Reilly\\book\n")), "'O\\'Reilly\\\\book\\n'");

    auto bytes = mysql2::Buffer::from({0x00, 0x0f, 0xff});
    EXPECT_EQ(mysql2::escape(mysql2::Value{bytes}), "X'000fff'");
    EXPECT_EQ(mysql2::escape(mysql2::Value{mysql2::raw("CURRENT_TIMESTAMP")}), "CURRENT_TIMESTAMP");
}

TEST(connection_options, charset_name_sets_charset_number) {
    mysql2::ConnectionOptions options;
    options.charset = "latin1";
    trace_step("create connection");
    mysql2::Connection connection(options);
    EXPECT_EQ(connection.options().charset_number, 8);
    EXPECT_EQ(mysql2::get_charset_number("GB18030_CHINESE_CI"), 248);
    EXPECT_EQ(mysql2::get_charset_number("utf8mb4_0900_ai_ci"), 255);
    EXPECT_EQ(mysql2::get_charset_encoding(248), "gb18030");
    EXPECT_EQ(mysql2::get_charset_encoding(95), "cp932");
}

TEST(connection_options, parse_connection_uri) {
    const auto options = mysql2::parse_connection_uri(
        "mysql://ada:p%40ss@db.example.test:3307/app?compress=true&ssl=true&sslProfile=Amazon%20RDS&multipleStatements=true&charset=latin1&maxPreparedStatements=7");
    EXPECT_EQ(options.host, "db.example.test");
    EXPECT_EQ(options.port, 3307);
    EXPECT_EQ(options.user, "ada");
    EXPECT_EQ(options.password, "p@ss");
    EXPECT_EQ(options.database, "app");
    EXPECT_TRUE(options.compress);
    EXPECT_TRUE(options.ssl.enabled);
    EXPECT_EQ(options.ssl.profile, "Amazon RDS");
    EXPECT_TRUE(options.multiple_statements);
    EXPECT_EQ(options.charset_number, 8);
    EXPECT_EQ(options.max_prepared_statements, 7u);

    const auto socket_options = mysql2::parse_connection_uri(
        "mysql://root@localhost/test?socketPath=/tmp/polycpp-mysql.sock");
    EXPECT_EQ(socket_options.socket_path, "/tmp/polycpp-mysql.sock");
}

TEST(connection_options, ssl_profiles_and_parser_cache_controls) {
    const auto names = mysql2::ssl_profile_names();
    ASSERT_FALSE(names.empty());
    EXPECT_EQ(names[0], "Amazon RDS");
    const auto ca_pems = mysql2::ssl_profile_ca_pems("Amazon RDS");
    ASSERT_GT(ca_pems.size(), 1u);
    EXPECT_NE(ca_pems.front().find("-----BEGIN CERTIFICATE-----"), std::string::npos);
    EXPECT_THROW(mysql2::ssl_profile_ca_pems("unknown"), mysql2::Error);

    mysql2::set_max_parser_cache(32);
    EXPECT_EQ(mysql2::max_parser_cache(), 32u);
    mysql2::clear_parser_cache();
    EXPECT_EQ(mysql2::max_parser_cache(), 32u);
}

TEST(binlog, parse_query_event_packet) {
    std::vector<uint8_t> payload;
    auto append_u8 = [&](uint8_t value) { payload.push_back(value); };
    auto append_u16 = [&](uint16_t value) {
        payload.push_back(static_cast<uint8_t>(value & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    };
    auto append_u32 = [&](uint32_t value) {
        payload.push_back(static_cast<uint8_t>(value & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    };
    auto append_string = [&](const std::string& value) {
        payload.insert(payload.end(), value.begin(), value.end());
    };

    const std::string schema = "test";
    const std::string query = "CREATE TABLE t(id INT)";
    const uint32_t body_size = 4 + 4 + 1 + 2 + 2 + static_cast<uint32_t>(schema.size()) + 1 +
        static_cast<uint32_t>(query.size());

    append_u8(0x00);
    append_u32(1);
    append_u8(2);
    append_u32(99);
    append_u32(19 + body_size);
    append_u32(1234);
    append_u16(0);
    append_u32(42);
    append_u32(0);
    append_u8(static_cast<uint8_t>(schema.size()));
    append_u16(0);
    append_u16(0);
    append_string(schema);
    append_u8(0);
    append_string(query);

    const auto event = mysql2::parse_binlog_event_packet(mysql2::Buffer::from(payload.data(), payload.size()));
    EXPECT_EQ(event.name, "QueryEvent");
    EXPECT_EQ(event.header.event_type, 2);
    EXPECT_EQ(event.header.server_id, 99u);
    EXPECT_EQ(event.header.log_position, 1234u);
    EXPECT_EQ(event.schema, schema);
    EXPECT_EQ(event.query, query);
}

TEST(binlog, stateful_parser_decodes_table_map_and_write_rows) {
    auto append_u8 = [](std::vector<uint8_t>& payload, uint8_t value) { payload.push_back(value); };
    auto append_u16 = [](std::vector<uint8_t>& payload, uint16_t value) {
        payload.push_back(static_cast<uint8_t>(value & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
    };
    auto append_u32 = [](std::vector<uint8_t>& payload, uint32_t value) {
        payload.push_back(static_cast<uint8_t>(value & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        payload.push_back(static_cast<uint8_t>((value >> 24) & 0xff));
    };
    auto append_u48 = [](std::vector<uint8_t>& payload, uint64_t value) {
        for (int i = 0; i < 6; ++i) {
            payload.push_back(static_cast<uint8_t>((value >> (8 * i)) & 0xff));
        }
    };
    auto append_string = [](std::vector<uint8_t>& payload, const std::string& value) {
        payload.insert(payload.end(), value.begin(), value.end());
    };
    auto make_event = [&](uint8_t type, const std::vector<uint8_t>& body) {
        std::vector<uint8_t> payload;
        append_u8(payload, 0x00);
        append_u32(payload, 1);
        append_u8(payload, type);
        append_u32(payload, 99);
        append_u32(payload, static_cast<uint32_t>(19 + body.size()));
        append_u32(payload, 1234);
        append_u16(payload, 0);
        payload.insert(payload.end(), body.begin(), body.end());
        return mysql2::Buffer::from(payload.data(), payload.size());
    };

    std::vector<uint8_t> table_map_body;
    append_u48(table_map_body, 7);
    append_u16(table_map_body, 0);
    append_u8(table_map_body, 4);
    append_string(table_map_body, "test");
    append_u8(table_map_body, 0);
    append_u8(table_map_body, 1);
    append_string(table_map_body, "t");
    append_u8(table_map_body, 0);
    append_u8(table_map_body, 2);
    append_u8(table_map_body, mysql2::constants::column_type::LONG);
    append_u8(table_map_body, mysql2::constants::column_type::VAR_STRING);
    append_u8(table_map_body, 2);
    append_u16(table_map_body, 255);
    append_u8(table_map_body, 0);

    std::vector<uint8_t> row_body;
    append_u48(row_body, 7);
    append_u16(row_body, 0);
    append_u8(row_body, 2);
    append_u8(row_body, 0x03);
    append_u8(row_body, 0x00);
    append_u32(row_body, 42);
    append_u8(row_body, 3);
    append_string(row_body, "Ada");

    mysql2::BinlogParser parser;
    const auto table_map = parser.parse(make_event(mysql2::constants::binlog_event_type::TABLE_MAP, table_map_body));
    EXPECT_EQ(table_map.name, "TableMapEvent");
    EXPECT_EQ(table_map.table_id, 7u);
    EXPECT_EQ(table_map.schema, "test");
    EXPECT_EQ(table_map.table, "t");
    ASSERT_EQ(table_map.column_types.size(), 2u);

    const auto rows = parser.parse(make_event(mysql2::constants::binlog_event_type::WRITE_ROWS_V1, row_body));
    EXPECT_EQ(rows.name, "WriteRowsEventV1");
    EXPECT_EQ(rows.schema, "test");
    EXPECT_EQ(rows.table, "t");
    ASSERT_EQ(rows.row_changes.size(), 1u);
    ASSERT_EQ(rows.row_changes[0].after.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(rows.row_changes[0].after[0]), 42);
    EXPECT_EQ(std::get<std::string>(rows.row_changes[0].after[1]), "Ada");
}

TEST(binlog, stateful_parser_decodes_update_delete_and_temporal2_rows) {
    std::vector<uint8_t> table_map_body;
    append_u48_le(table_map_body, 9);
    append_u16_le(table_map_body, 0);
    append_u8(table_map_body, 4);
    append_string(table_map_body, "test");
    append_u8(table_map_body, 0);
    append_u8(table_map_body, 8);
    append_string(table_map_body, "temporal");
    append_u8(table_map_body, 0);
    append_u8(table_map_body, 5);
    append_u8(table_map_body, mysql2::constants::column_type::LONG);
    append_u8(table_map_body, mysql2::constants::column_type::VAR_STRING);
    append_u8(table_map_body, mysql2::constants::column_type::TIMESTAMP2);
    append_u8(table_map_body, mysql2::constants::column_type::DATETIME2);
    append_u8(table_map_body, mysql2::constants::column_type::TIME2);
    append_u8(table_map_body, 5);
    append_u16_le(table_map_body, 255);
    append_u8(table_map_body, 6);
    append_u8(table_map_body, 6);
    append_u8(table_map_body, 6);
    append_u8(table_map_body, 0);

    const auto append_temporal_values = [](std::vector<uint8_t>& payload,
                                           int32_t id,
                                           const std::string& name,
                                           uint32_t timestamp_seconds,
                                           uint64_t datetime2_int,
                                           uint32_t time2_int) {
        append_u8(payload, 0x00);
        append_u32_le(payload, static_cast<uint32_t>(id));
        append_u8(payload, static_cast<uint8_t>(name.size()));
        append_string(payload, name);
        append_u32_be(payload, timestamp_seconds);
        append_u24_be(payload, 123456);
        append_u40_be(payload, datetime2_int + 0x8000000000ULL);
        append_u24_be(payload, 123456);
        append_u24_be(payload, time2_int + 0x800000);
        append_u24_be(payload, 123456);
    };
    const auto datetime2_int = (static_cast<uint64_t>(2024 * 13 + 5) << 22) |
                               (static_cast<uint64_t>(6) << 17) |
                               (static_cast<uint64_t>(7) << 12) |
                               (static_cast<uint64_t>(8) << 6) |
                               9;
    const auto time2_int = (12u << 12) | (34u << 6) | 56u;

    mysql2::BinlogParser parser;
    const auto table_map = parser.parse(make_binlog_event(mysql2::constants::binlog_event_type::TABLE_MAP, table_map_body));
    ASSERT_EQ(table_map.column_types.size(), 5u);
    ASSERT_EQ(table_map.column_metadata.size(), 5u);
    EXPECT_EQ(table_map.column_metadata[2], 6u);
    EXPECT_EQ(table_map.column_metadata[3], 6u);
    EXPECT_EQ(table_map.column_metadata[4], 6u);

    std::vector<uint8_t> update_body;
    append_u48_le(update_body, 9);
    append_u16_le(update_body, 0);
    append_u8(update_body, 5);
    append_u8(update_body, 0x1f);
    append_u8(update_body, 0x1f);
    append_temporal_values(update_body, 42, "Ada", 1234567890u, datetime2_int, time2_int);
    append_temporal_values(update_body, 43, "Bea", 1234567891u, datetime2_int, time2_int);

    const auto update = parser.parse(make_binlog_event(mysql2::constants::binlog_event_type::UPDATE_ROWS_V1, update_body));
    EXPECT_EQ(update.name, "UpdateRowsEventV1");
    ASSERT_EQ(update.row_changes.size(), 1u);
    ASSERT_EQ(update.row_changes[0].before.size(), 5u);
    ASSERT_EQ(update.row_changes[0].after.size(), 5u);
    EXPECT_EQ(std::get<int64_t>(update.row_changes[0].before[0]), 42);
    EXPECT_EQ(std::get<int64_t>(update.row_changes[0].after[0]), 43);
    const auto& timestamp = std::get<mysql2::BinlogTimestamp>(update.row_changes[0].after[2]);
    EXPECT_EQ(timestamp.seconds_since_epoch, 1234567891u);
    EXPECT_EQ(timestamp.microsecond, 123456u);
    const auto& datetime = std::get<mysql2::BinlogDateTime>(update.row_changes[0].after[3]);
    EXPECT_EQ(datetime.to_string(), "2024-05-06 07:08:09.123456");
    const auto& time = std::get<mysql2::BinlogTime>(update.row_changes[0].after[4]);
    EXPECT_FALSE(time.negative);
    EXPECT_EQ(time.to_string(), "12:34:56.123456");

    std::vector<uint8_t> delete_body;
    append_u48_le(delete_body, 9);
    append_u16_le(delete_body, 0);
    append_u8(delete_body, 5);
    append_u8(delete_body, 0x1f);
    append_temporal_values(delete_body, 43, "Bea", 1234567891u, datetime2_int, time2_int);

    const auto deleted = parser.parse(make_binlog_event(mysql2::constants::binlog_event_type::DELETE_ROWS_V1, delete_body));
    EXPECT_EQ(deleted.name, "DeleteRowsEventV1");
    ASSERT_EQ(deleted.row_changes.size(), 1u);
    ASSERT_EQ(deleted.row_changes[0].before.size(), 5u);
    EXPECT_TRUE(deleted.row_changes[0].after.empty());
    EXPECT_EQ(std::get<int64_t>(deleted.row_changes[0].before[0]), 43);
}

TEST(binlog, stateful_parser_decodes_negative_time2_fractional_rows) {
    std::vector<uint8_t> table_map_body;
    append_u48_le(table_map_body, 10);
    append_u16_le(table_map_body, 0);
    append_u8(table_map_body, 4);
    append_string(table_map_body, "test");
    append_u8(table_map_body, 0);
    append_u8(table_map_body, 8);
    append_string(table_map_body, "time_neg");
    append_u8(table_map_body, 0);
    append_u8(table_map_body, 1);
    append_u8(table_map_body, mysql2::constants::column_type::TIME2);
    append_u8(table_map_body, 1);
    append_u8(table_map_body, 2);
    append_u8(table_map_body, 0);

    mysql2::BinlogParser parser;
    parser.parse(make_binlog_event(mysql2::constants::binlog_event_type::TABLE_MAP, table_map_body));

    std::vector<uint8_t> row_body;
    append_u48_le(row_body, 10);
    append_u16_le(row_body, 0);
    append_u8(row_body, 1);
    append_u8(row_body, 0x01);
    append_u8(row_body, 0x00);
    append_u8(row_body, 0x7f);
    append_u8(row_body, 0xff);
    append_u8(row_body, 0xff);
    append_u8(row_body, 0xff);

    const auto rows = parser.parse(make_binlog_event(mysql2::constants::binlog_event_type::WRITE_ROWS_V1, row_body));
    ASSERT_EQ(rows.row_changes.size(), 1u);
    ASSERT_EQ(rows.row_changes[0].after.size(), 1u);
    const auto& time = std::get<mysql2::BinlogTime>(rows.row_changes[0].after[0]);
    EXPECT_TRUE(time.negative);
    EXPECT_EQ(time.hours, 0u);
    EXPECT_EQ(time.minutes, 0u);
    EXPECT_EQ(time.seconds, 0u);
    EXPECT_EQ(time.microsecond, 10000u);
    EXPECT_EQ(time.to_string(), "-00:00:00.01");
}

TEST(binlog, parses_rotate_format_xid_gtid_previous_gtids_and_unknown_events) {
    std::vector<uint8_t> rotate_body;
    append_u64_le(rotate_body, 4567);
    append_string(rotate_body, "mysql-bin.000002");
    const auto rotate = mysql2::parse_binlog_event_packet(
        make_binlog_event(mysql2::constants::binlog_event_type::ROTATE, rotate_body));
    EXPECT_EQ(rotate.name, "RotateEvent");
    EXPECT_EQ(rotate.next_position, 4567u);
    EXPECT_EQ(rotate.next_binlog, "mysql-bin.000002");

    std::vector<uint8_t> format_body;
    append_u16_le(format_body, 4);
    std::string server_version = "8.4.0-polycpp";
    format_body.insert(format_body.end(), server_version.begin(), server_version.end());
    format_body.resize(format_body.size() + (50 - server_version.size()), 0);
    append_u32_le(format_body, 777);
    append_u8(format_body, 19);
    append_u8(format_body, 1);
    append_u8(format_body, 2);
    const auto format = mysql2::parse_binlog_event_packet(
        make_binlog_event(mysql2::constants::binlog_event_type::FORMAT_DESCRIPTION, format_body));
    EXPECT_EQ(format.name, "FormatDescriptionEvent");
    EXPECT_EQ(format.binlog_version, 4u);
    EXPECT_EQ(format.server_version, server_version);
    EXPECT_EQ(format.create_timestamp, 777u);
    EXPECT_EQ(format.event_header_length, 19u);
    EXPECT_EQ(format.event_type_header_lengths.length(), 2u);

    std::vector<uint8_t> xid_body;
    append_u64_le(xid_body, 0x1122334455667788ULL);
    const auto xid = mysql2::parse_binlog_event_packet(
        make_binlog_event(mysql2::constants::binlog_event_type::XID, xid_body));
    EXPECT_EQ(xid.name, "XidEvent");
    EXPECT_EQ(xid.xid, 0x1122334455667788ULL);

    const std::array<uint8_t, 16> sid = {
        0x3e, 0x11, 0xfa, 0x47, 0x71, 0xca, 0x11, 0xe1,
        0x9e, 0x33, 0xc8, 0x0a, 0xa9, 0x42, 0x95, 0x62};
    std::vector<uint8_t> gtid_body;
    append_u8(gtid_body, 1);
    gtid_body.insert(gtid_body.end(), sid.begin(), sid.end());
    append_u64_le(gtid_body, 99);
    const auto gtid = mysql2::parse_binlog_event_packet(
        make_binlog_event(mysql2::constants::binlog_event_type::GTID, gtid_body));
    EXPECT_EQ(gtid.name, "GtidEvent");
    EXPECT_EQ(gtid.gtid_flags, 1u);
    EXPECT_EQ(gtid.gtid_sid, "3e11fa47-71ca-11e1-9e33-c80aa9429562");
    EXPECT_EQ(gtid.gtid_sequence_number, 99u);

    std::vector<uint8_t> previous_body;
    append_u64_le(previous_body, 1);
    previous_body.insert(previous_body.end(), sid.begin(), sid.end());
    append_u64_le(previous_body, 1);
    append_u64_le(previous_body, 10);
    append_u64_le(previous_body, 21);
    const auto previous = mysql2::parse_binlog_event_packet(
        make_binlog_event(mysql2::constants::binlog_event_type::PREVIOUS_GTIDS, previous_body));
    ASSERT_EQ(previous.previous_gtids.size(), 1u);
    EXPECT_EQ(previous.previous_gtids[0].sid, "3e11fa47-71ca-11e1-9e33-c80aa9429562");
    ASSERT_EQ(previous.previous_gtids[0].intervals.size(), 1u);
    EXPECT_EQ(previous.previous_gtids[0].intervals[0].start, 10u);
    EXPECT_EQ(previous.previous_gtids[0].intervals[0].end, 20u);

    const auto unknown = mysql2::parse_binlog_event_packet(make_binlog_event(99, {0x01, 0x02, 0x03}));
    EXPECT_EQ(unknown.name, "UnknownEvent");
    EXPECT_EQ(unknown.header.event_type, 99u);
    EXPECT_EQ(unknown.body.length(), 3u);
}

TEST(binlog, parses_gtid_set_text) {
    const auto sources = mysql2::parse_gtid_set("3E11FA47-71CA-11E1-9E33-C80AA9429562:1-3:7");
    ASSERT_EQ(sources.size(), 1u);
    EXPECT_EQ(sources[0].sid, "3E11FA47-71CA-11E1-9E33-C80AA9429562");
    ASSERT_EQ(sources[0].intervals.size(), 2u);
    EXPECT_EQ(sources[0].intervals[0].start, 1u);
    EXPECT_EQ(sources[0].intervals[0].end, 3u);
    EXPECT_EQ(sources[0].intervals[1].start, 7u);
    EXPECT_EQ(sources[0].intervals[1].end, 7u);
}

TEST(sql_escape, identifiers_and_format) {
    EXPECT_EQ(mysql2::escape_id("users.name"), "`users`.`name`");
    EXPECT_EQ(mysql2::escape_id("we`ird", true), "`we``ird`");

    const auto sql = mysql2::format(
        "SELECT ?? FROM ?? WHERE id = ? AND name = ?",
        {std::string("name"), std::string("users"), int64_t{7}, std::string("Ada")});
    EXPECT_EQ(sql, "SELECT `name` FROM `users` WHERE id = 7 AND name = 'Ada'");

    const auto named = mysql2::format_named(
        "SELECT * FROM users WHERE id = :id AND note = ':not_a_param' AND name = :name",
        {{"id", int64_t{9}}, {"name", std::string("Lin")}});
    EXPECT_EQ(named, "SELECT * FROM users WHERE id = 9 AND note = ':not_a_param' AND name = 'Lin'");
}

TEST(rows, json_helpers_and_event_emitter_surface) {
    mysql2::Field id;
    id.name = "id";
    mysql2::Field name;
    name.name = "name";
    mysql2::Row row;
    row.values = {int64_t{42}, std::string("Ada")};
    row.index_by_name = {{"id", 0}, {"name", 1}};

    EXPECT_EQ(mysql2::row_to_json_line(row, {id, name}), R"({"id":42,"name":"Ada"})" "\n");
    mysql2::RowStream row_stream;
    EXPECT_TRUE(row_stream.push(row));
    row_stream.pushEnd();
    auto first = row_stream.read();
    ASSERT_TRUE(first.has_value());
    EXPECT_EQ(std::get<int64_t>(first->at("id")), 42);
    EXPECT_FALSE(row_stream.read().has_value());

    mysql2::BinlogDumpOptions binlog_options;
    binlog_options.server_id = 12345;
    mysql2::BinlogStream binlog_stream(binlog_options, {}, {});
    EXPECT_EQ(binlog_stream.options().server_id, 12345u);
    bool saw_binlog_event = false;
    binlog_stream.on(polycpp::stream::event::Data, [&](const mysql2::BinlogEvent& event) {
        saw_binlog_event = event.name == "QueryEvent";
    });
    mysql2::BinlogEvent binlog_event;
    binlog_event.name = "QueryEvent";
    EXPECT_TRUE(binlog_stream.push(binlog_event));
    binlog_stream.pushEnd();
    EXPECT_TRUE(saw_binlog_event);

    mysql2::Connection connection;
    bool saw_error = false;
    connection.on(mysql2::event::Error_, [&](const mysql2::Error& error) {
        saw_error = std::string(error.what()).find("synthetic") != std::string::npos;
    });
    connection.emit(mysql2::event::Error_, mysql2::Error("synthetic"));
    EXPECT_TRUE(saw_error);

    bool saw_trace = false;
    connection.on(mysql2::event::Trace, [&](const mysql2::TraceEvent& event) {
        saw_trace = event.operation == "query" && event.phase == "start";
    });
    mysql2::TraceEvent event;
    event.operation = "query";
    event.phase = "start";
    connection.emit(mysql2::event::Trace, event);
    EXPECT_TRUE(saw_trace);
}

TEST(server_mode, loopback_query_uses_adapted_server_api) {
    mysql2::ServerOptions server_options;
    server_options.handshake.server_version = "polycpp-mysql2-test";

    std::atomic<bool> auth_seen{false};
    std::atomic<bool> connection_seen{false};
    std::atomic<bool> query_seen{false};
    std::atomic<bool> ping_seen{false};
    std::atomic<bool> quit_seen{false};
    std::atomic<bool> stmt_prepare_seen{false};
    std::atomic<bool> stmt_execute_seen{false};
    std::atomic<bool> protocol_stmt_prepare_seen{false};
    std::atomic<bool> protocol_stmt_execute_seen{false};

    server_options.handshake.auth_callback = [&](const mysql2::ServerAuthInfo& auth) -> std::optional<mysql2::Error> {
        const auto attr = auth.connect_attributes.find("polycpp_test");
        auth_seen = auth.user == "polycpp" &&
                    attr != auth.connect_attributes.end() &&
                    attr->second == "server-mode";
        return std::nullopt;
    };

    auto server = mysql2::create_server(server_options);
    server.on(mysql2::event::ServerConnectionAccepted, [&](mysql2::ServerConnection& connection) {
        connection_seen = true;
        connection.on(mysql2::event::ServerQuery, [&](mysql2::ServerConnection& conn, const std::string& sql) {
            query_seen = sql == "SELECT 42 AS answer";

            mysql2::Field answer;
            answer.name = "answer";
            answer.column_type = mysql2::constants::column_type::LONG;
            answer.character_set = 224;
            answer.encoding = "utf8";

            mysql2::Row row;
            row.values = {int64_t{42}};
            conn.write_text_result(std::vector<mysql2::Row>{row}, std::vector<mysql2::Field>{answer});
        });
        connection.on(mysql2::event::ServerPing, [&](mysql2::ServerConnection& conn) {
            ping_seen = true;
            conn.write_ok();
        });
        connection.on(mysql2::event::ServerStatementPrepare, [&](mysql2::ServerConnection& conn, const std::string& sql) {
            if (sql == "SELECT ? AS answer") {
                protocol_stmt_prepare_seen = true;
                mysql2::Field parameter;
                parameter.name = "param";
                parameter.column_type = mysql2::constants::column_type::LONGLONG;
                parameter.character_set = 63;
                parameter.encoding = "binary";

                mysql2::Field answer;
                answer.name = "answer";
                answer.column_type = mysql2::constants::column_type::LONGLONG;
                answer.character_set = 63;
                answer.encoding = "binary";
                conn.write_statement_prepare_ok(7, {parameter}, {answer});
            } else {
                stmt_prepare_seen = sql.rfind("PREPARE ", 0) == 0;
                conn.write_ok();
            }
        });
        connection.on(mysql2::event::ServerStatementExecute,
                      [&](mysql2::ServerConnection& conn, const mysql2::ServerStatementExecuteInfo& info) {
            if (info.statement_id == 7) {
                protocol_stmt_execute_seen = !info.values.empty() && std::get<int64_t>(info.values[0]) == 41;
                mysql2::Field answer;
                answer.name = "answer";
                answer.column_type = mysql2::constants::column_type::LONGLONG;
                answer.character_set = 63;
                answer.encoding = "binary";

                mysql2::Row row;
                row.values = {int64_t{42}};
                conn.write_binary_result(std::vector<mysql2::Row>{row}, std::vector<mysql2::Field>{answer});
            } else {
                stmt_execute_seen = info.query == "EXECUTE polycpp_stmt";
                conn.write_ok();
            }
        });
        connection.on(mysql2::event::ServerQuit, [&](mysql2::ServerConnection&) {
            quit_seen = true;
        });
    });

    server.listen();

    mysql2::ConnectionOptions client_options;
    client_options.host = server.address();
    client_options.port = server.port();
    client_options.user = "polycpp";
    client_options.connect_timeout_ms = 5000;
    client_options.connect_attributes["polycpp_test"] = "server-mode";

    auto client = mysql2::create_connection(client_options);
    const auto result = client.query("SELECT 42 AS answer");
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0].at("answer")), 42);
    auto stream = client.query_stream("SELECT 42 AS answer");
    ASSERT_EQ(stream.fields().size(), 1u);
    EXPECT_THROW(client.query("SELECT 42 AS answer"), mysql2::Error);
    auto stream_row = stream.read();
    ASSERT_TRUE(stream_row.has_value());
    EXPECT_EQ(std::get<int64_t>(stream_row->at("answer")), 42);
    EXPECT_FALSE(stream.read().has_value());
    const auto after_stream = client.query("SELECT 42 AS answer");
    ASSERT_EQ(after_stream.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(after_stream.rows[0].at("answer")), 42);
    {
        auto abandoned_stream = client.query_stream("SELECT 42 AS answer");
        ASSERT_EQ(abandoned_stream.fields().size(), 1u);
    }
    const auto after_abandoned_stream = client.query("SELECT 42 AS answer");
    ASSERT_EQ(after_abandoned_stream.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(after_abandoned_stream.rows[0].at("answer")), 42);
    client.query("PREPARE polycpp_stmt FROM 'SELECT 1'");
    client.query("EXECUTE polycpp_stmt");
    const auto statement = client.prepare("SELECT ? AS answer");
    const auto prepared_result = client.execute(statement, {int64_t{41}});
    ASSERT_EQ(prepared_result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(prepared_result.rows[0].at("answer")), 42);
    client.ping();
    client.end();

    for (int i = 0; i < 50 && !quit_seen.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    server.close();

    EXPECT_TRUE(auth_seen);
    EXPECT_TRUE(connection_seen);
    EXPECT_TRUE(query_seen);
    EXPECT_TRUE(ping_seen);
    EXPECT_TRUE(quit_seen);
    EXPECT_TRUE(stmt_prepare_seen);
    EXPECT_TRUE(stmt_execute_seen);
    EXPECT_TRUE(protocol_stmt_prepare_seen);
    EXPECT_TRUE(protocol_stmt_execute_seen);
    EXPECT_EQ(server.connection_count(), 0u);
}

TEST(server_mode, loopback_query_supports_unix_socket_path) {
#if defined(_WIN32)
    GTEST_SKIP() << "Unix socket path tests are not available on Windows";
#else
    const auto path = unique_socket_path();
    std::filesystem::remove(path);

    mysql2::ServerOptions server_options;
    server_options.socket_path = path;
    server_options.handshake.server_version = "polycpp-mysql2-ipc-test";

    std::atomic<bool> query_seen{false};
    std::atomic<bool> quit_seen{false};
    auto server = mysql2::create_server(server_options);
    server.on(mysql2::event::ServerConnectionAccepted, [&](mysql2::ServerConnection& connection) {
        connection.on(mysql2::event::ServerQuery, [&](mysql2::ServerConnection& conn, const std::string& sql) {
            query_seen = sql == "SELECT 'ipc' AS transport";

            mysql2::Field transport;
            transport.name = "transport";
            transport.column_type = mysql2::constants::column_type::VAR_STRING;
            transport.character_set = 224;
            transport.encoding = "utf8";

            mysql2::Row row;
            row.values = {std::string("ipc")};
            conn.write_text_result(std::vector<mysql2::Row>{row}, std::vector<mysql2::Field>{transport});
        });
        connection.on(mysql2::event::ServerQuit, [&](mysql2::ServerConnection&) {
            quit_seen = true;
        });
    });

    server.listen();
    EXPECT_EQ(server.address(), path);
    EXPECT_EQ(server.port(), 0);

    mysql2::ConnectionOptions client_options;
    client_options.socket_path = path;
    client_options.user = "polycpp";
    client_options.connect_timeout_ms = 5000;

    auto client = mysql2::create_connection(client_options);
    const auto result = client.query("SELECT 'ipc' AS transport");
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0].at("transport")), "ipc");
    client.end();

    for (int i = 0; i < 50 && !quit_seen.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    server.close();
    std::filesystem::remove(path);

    EXPECT_TRUE(query_seen);
    EXPECT_TRUE(quit_seen);
    EXPECT_EQ(server.connection_count(), 0u);
#endif
}

TEST(server_mode, loopback_query_supports_tls_upgrade_when_configured) {
    const char* cert_file = std::getenv("MYSQL2_TEST_SERVER_TLS_CERT_FILE");
    const char* key_file = std::getenv("MYSQL2_TEST_SERVER_TLS_KEY_FILE");
    if (cert_file == nullptr || key_file == nullptr) {
        GTEST_SKIP() << "set MYSQL2_TEST_SERVER_TLS_CERT_FILE and MYSQL2_TEST_SERVER_TLS_KEY_FILE";
    }

    mysql2::ServerOptions server_options;
    server_options.handshake.server_version = "polycpp-mysql2-tls-test";
    server_options.tls.enabled = true;
    server_options.tls.cert_file = cert_file;
    server_options.tls.key_file = key_file;

    std::atomic<bool> query_seen{false};
    std::atomic<bool> quit_seen{false};
    auto server = mysql2::create_server(server_options);
    server.on(mysql2::event::ServerConnectionAccepted, [&](mysql2::ServerConnection& connection) {
        connection.on(mysql2::event::ServerQuery, [&](mysql2::ServerConnection& conn, const std::string& sql) {
            query_seen = sql == "SELECT 'tls' AS transport";

            mysql2::Field transport;
            transport.name = "transport";
            transport.column_type = mysql2::constants::column_type::VAR_STRING;
            transport.character_set = 224;
            transport.encoding = "utf8";

            mysql2::Row row;
            row.values = {std::string("tls")};
            conn.write_text_result(std::vector<mysql2::Row>{row}, std::vector<mysql2::Field>{transport});
        });
        connection.on(mysql2::event::ServerQuit, [&](mysql2::ServerConnection&) {
            quit_seen = true;
        });
    });

    server.listen();

    mysql2::ConnectionOptions client_options;
    client_options.host = server.address();
    client_options.port = server.port();
    client_options.user = "polycpp";
    client_options.connect_timeout_ms = 5000;
    client_options.ssl.enabled = true;
    client_options.ssl.reject_unauthorized = false;
    client_options.ssl.verify_identity = false;

    auto client = mysql2::create_connection(client_options);
    EXPECT_TRUE(client.encrypted());
    const auto result = client.query("SELECT 'tls' AS transport");
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(result.rows[0].at("transport")), "tls");
    client.end();

    for (int i = 0; i < 50 && !quit_seen.load(); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    server.close();

    EXPECT_TRUE(query_seen);
    EXPECT_TRUE(quit_seen);
}

TEST(server_mode, auth_callback_rejects_client_with_error_packet) {
    mysql2::ServerOptions server_options;
    server_options.handshake.auth_callback = [](const mysql2::ServerAuthInfo&) -> std::optional<mysql2::Error> {
        return mysql2::Error(1045, "28000", "access denied by test server");
    };

    auto server = mysql2::create_server(server_options);
    server.listen();

    mysql2::ConnectionOptions client_options;
    client_options.host = server.address();
    client_options.port = server.port();
    client_options.user = "rejected";
    client_options.connect_timeout_ms = 5000;

    try {
        (void)mysql2::create_connection(client_options);
        FAIL() << "expected server auth rejection";
    } catch (const mysql2::Error& error) {
        EXPECT_EQ(error.code(), 1045);
        EXPECT_EQ(error.sql_state(), "28000");
    }

    server.close();
}

TEST(mysql2_replication, binlog_stream_against_real_database_when_configured) {
    if (!env_enabled("MYSQL2_TEST_REPLICATION")) {
        GTEST_SKIP() << "Set MYSQL2_TEST_REPLICATION=1 with a binary-log-enabled server to run replication e2e";
    }
    const char* host = std::getenv("MYSQL2_TEST_HOST");
    if (!host) {
        GTEST_SKIP() << "Set MYSQL2_TEST_HOST/MYSQL2_TEST_USER to run mysql2 replication e2e tests";
    }

    mysql2::ConnectionOptions options;
    options.host = host;
    options.port = std::getenv("MYSQL2_TEST_PORT") ? static_cast<uint16_t>(std::stoi(std::getenv("MYSQL2_TEST_PORT"))) : 3306;
    options.user = std::getenv("MYSQL2_TEST_USER") ? std::getenv("MYSQL2_TEST_USER") : "root";
    options.password = std::getenv("MYSQL2_TEST_PASSWORD") ? std::getenv("MYSQL2_TEST_PASSWORD") : "";
    options.database = std::getenv("MYSQL2_TEST_DATABASE") ? std::getenv("MYSQL2_TEST_DATABASE") : "";
    options.connect_timeout_ms = 5000;

    mysql2::Connection writer(options);
    writer.connect();
    const auto log_bin = writer.query("SELECT @@log_bin AS log_bin");
    ASSERT_EQ(log_bin.rows.size(), 1u);
    if (value_as_u64(log_bin.rows[0].at("log_bin")) == 0) {
        GTEST_SKIP() << "Server binary logging is disabled";
    }
    try {
        writer.query("SET SESSION binlog_format = 'ROW'");
    } catch (const mysql2::Error&) {
    }
    const auto binlog_format = writer.query("SELECT @@session.binlog_format AS binlog_format");
    ASSERT_EQ(binlog_format.rows.size(), 1u);
    if (value_as_string(binlog_format.rows[0].at("binlog_format")) != "ROW") {
        GTEST_SKIP() << "Replication e2e requires ROW binlog_format";
    }
    writer.query("SET time_zone = '+00:00'");
    writer.query("DROP TABLE IF EXISTS polycpp_mysql2_binlog_e2e");
    writer.query("CREATE TABLE polycpp_mysql2_binlog_e2e ("
                 "id INT PRIMARY KEY, "
                 "name VARCHAR(32), "
                 "ts TIMESTAMP(6) NULL, "
                 "dt DATETIME(6) NULL, "
                 "tm TIME(6) NULL)");

    mysql2::QueryResult status;
    try {
        status = writer.query("SHOW BINARY LOG STATUS");
    } catch (const mysql2::Error&) {
        status = writer.query("SHOW MASTER STATUS");
    }
    ASSERT_EQ(status.rows.size(), 1u);
    mysql2::BinlogDumpOptions dump;
    dump.filename = value_as_string(status.rows[0].at("File"));
    dump.binlog_position = value_as_u64(status.rows[0].at("Position"));
    dump.flags = mysql2::constants::binlog_dump_flags::NON_BLOCK;
    dump.server_id = 61002;
    dump.max_events = 64;

    writer.query("INSERT INTO polycpp_mysql2_binlog_e2e "
                 "VALUES (1, 'Ada', '2009-02-13 23:31:30.123456', "
                 "'2024-05-06 07:08:09.123456', '12:34:56.123456'), "
                 "(2, 'Neo', '2009-02-13 23:31:31.654321', "
                 "'2025-06-07 08:09:10.654321', '-00:00:00.010000')");

    {
        mysql2::Connection max_reader(options);
        auto max_dump = dump;
        max_dump.server_id = 61003;
        max_dump.max_events = 1;
        auto stream = max_reader.create_binlog_stream(max_dump);
        ASSERT_TRUE(stream.read().has_value());
        EXPECT_FALSE(stream.read().has_value());
        EXPECT_FALSE(max_reader.connected());
    }

    mysql2::Connection reader(options);
    auto stream = reader.create_binlog_stream(dump);
    EXPECT_THROW(reader.query("SELECT 1"), mysql2::Error);

    bool saw_table_map = false;
    bool saw_write_rows = false;
    while (auto event = stream.read()) {
        if (event->name == "TableMapEvent" && event->table == "polycpp_mysql2_binlog_e2e") {
            saw_table_map = true;
        }
        if ((event->name == "WriteRowsEventV1" || event->name == "WriteRowsEventV2") &&
            event->table == "polycpp_mysql2_binlog_e2e") {
            saw_write_rows = true;
            ASSERT_EQ(event->row_changes.size(), 2u);
            for (const auto& change : event->row_changes) {
                ASSERT_GE(change.after.size(), 5u);
                const auto id = std::get<int64_t>(change.after[0]);
                if (id == 1) {
                    EXPECT_EQ(std::get<std::string>(change.after[1]), "Ada");
                    const auto& ts = std::get<mysql2::BinlogTimestamp>(change.after[2]);
                    EXPECT_EQ(ts.seconds_since_epoch, 1234567890u);
                    EXPECT_EQ(ts.microsecond, 123456u);
                    EXPECT_EQ(std::get<mysql2::BinlogDateTime>(change.after[3]).to_string(),
                              "2024-05-06 07:08:09.123456");
                    EXPECT_EQ(std::get<mysql2::BinlogTime>(change.after[4]).to_string(),
                              "12:34:56.123456");
                } else if (id == 2) {
                    EXPECT_EQ(std::get<std::string>(change.after[1]), "Neo");
                    const auto& ts = std::get<mysql2::BinlogTimestamp>(change.after[2]);
                    EXPECT_EQ(ts.seconds_since_epoch, 1234567891u);
                    EXPECT_EQ(ts.microsecond, 654321u);
                    EXPECT_EQ(std::get<mysql2::BinlogDateTime>(change.after[3]).to_string(),
                              "2025-06-07 08:09:10.654321");
                    const auto& time = std::get<mysql2::BinlogTime>(change.after[4]);
                    EXPECT_TRUE(time.negative);
                    EXPECT_EQ(time.to_string(), "-00:00:00.01");
                } else {
                    FAIL() << "unexpected replication e2e row id " << id;
                }
            }
        }
    }
    EXPECT_TRUE(saw_table_map);
    EXPECT_TRUE(saw_write_rows);
    EXPECT_FALSE(reader.connected());
    EXPECT_NO_THROW((void)reader.query("SELECT 1 AS reusable_after_eof"));

    mysql2::QueryResult after_status;
    try {
        after_status = writer.query("SHOW BINARY LOG STATUS");
    } catch (const mysql2::Error&) {
        after_status = writer.query("SHOW MASTER STATUS");
    }
    mysql2::BinlogDumpOptions abandon_dump;
    abandon_dump.filename = value_as_string(after_status.rows[0].at("File"));
    abandon_dump.binlog_position = value_as_u64(after_status.rows[0].at("Position"));
    abandon_dump.flags = mysql2::constants::binlog_dump_flags::NON_BLOCK;
    abandon_dump.server_id = 61004;
    abandon_dump.max_events = 0;
    mysql2::Connection abandon_reader(options);
    auto abandoned = abandon_reader.create_binlog_stream(abandon_dump);
    abandoned.destroy();
    EXPECT_FALSE(abandon_reader.connected());
}

TEST(mysql2_integration, query_against_real_database_when_configured) {
    const char* host = std::getenv("MYSQL2_TEST_HOST");
    if (!host) {
        GTEST_SKIP() << "Set MYSQL2_TEST_HOST/MYSQL2_TEST_USER to run mysql2 e2e tests";
    }

    mysql2::ConnectionOptions options;
    options.host = host;
    options.port = std::getenv("MYSQL2_TEST_PORT") ? static_cast<uint16_t>(std::stoi(std::getenv("MYSQL2_TEST_PORT"))) : 3306;
    options.user = std::getenv("MYSQL2_TEST_USER") ? std::getenv("MYSQL2_TEST_USER") : "root";
    options.password = std::getenv("MYSQL2_TEST_PASSWORD") ? std::getenv("MYSQL2_TEST_PASSWORD") : "";
    options.database = std::getenv("MYSQL2_TEST_DATABASE") ? std::getenv("MYSQL2_TEST_DATABASE") : "";
    if (std::getenv("MYSQL2_TEST_SSL")) {
        options.ssl.enabled = true;
        options.ssl.reject_unauthorized = std::getenv("MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED")
            ? std::string(std::getenv("MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED")) != "0"
            : false;
        options.ssl.verify_identity = std::getenv("MYSQL2_TEST_SSL_VERIFY_IDENTITY")
            ? std::string(std::getenv("MYSQL2_TEST_SSL_VERIFY_IDENTITY")) != "0"
            : options.ssl.reject_unauthorized;
        if (std::getenv("MYSQL2_TEST_SSL_CA_FILE")) {
            options.ssl.ca_file = std::getenv("MYSQL2_TEST_SSL_CA_FILE");
        }
    }

    mysql2::Connection connection(options);
    bool connect_event_seen = false;
    bool connect_trace_seen = false;
    bool query_trace_seen = false;
    connection.on(mysql2::event::Connect, [&](const mysql2::ConnectionInfo& info) {
        connect_event_seen = info.connection_id > 0 && !info.server_version.empty();
    });
    connection.on(mysql2::event::Trace, [&](const mysql2::TraceEvent& event) {
        if (event.operation == "connect" && event.phase == "success" && event.server_address == options.host) {
            connect_trace_seen = true;
        }
        if (event.operation == "query" && event.phase == "success" && event.sql.find("SELECT 1 AS one") != std::string::npos) {
            query_trace_seen = true;
        }
    });
    trace_step("connect");
    connection.connect();
    EXPECT_TRUE(connection.connected());
    EXPECT_TRUE(connect_event_seen);
    EXPECT_TRUE(connect_trace_seen);
    EXPECT_EQ(connection.encrypted(), options.ssl.enabled);
    EXPECT_GT(connection.connection_id(), 0u);
    EXPECT_FALSE(connection.server_version().empty());

    trace_step("ping");
    const auto pong = connection.ping();
    EXPECT_EQ(pong.warning_count, 0u);

    trace_step("simple query");
    const auto result = connection.query("SELECT 1 AS one, 'two' AS two, NULL AS none");
    ASSERT_TRUE(result.has_rows());
    ASSERT_EQ(result.fields.size(), 3u);
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0].at("one")), 1);
    EXPECT_EQ(std::get<std::string>(result.rows[0].at("two")), "two");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result.rows[0].at("none")));
    EXPECT_TRUE(query_trace_seen);

    trace_step("query options");
    mysql2::QueryOptions query_options;
    query_options.sql = "SELECT 20 AS option_value";
    query_options.timeout_ms = 5000;
    const auto option_result = connection.query(query_options);
    ASSERT_EQ(option_result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(option_result.rows[0].at("option_value")), 20);

    trace_step("execute options");
    mysql2::ExecuteOptions execute_options;
    execute_options.sql = "SELECT ? AS option_exec";
    execute_options.values = {int64_t{21}};
    execute_options.timeout_ms = 5000;
    const auto option_exec = connection.execute(execute_options);
    ASSERT_EQ(option_exec.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(option_exec.rows[0].at("option_exec")), 21);

    constexpr uint32_t client_query_attributes = 0x08000000;
    if ((connection.server_capability_flags() & client_query_attributes) != 0) {
        trace_step("query attributes query");
        const auto attr_result = connection.query(
            "SELECT 16 AS attr_probe",
            {{"trace", std::string("polycpp")}, {"flag", true}, {"count", int64_t{5}}, {"empty", std::monostate{}}});
        ASSERT_EQ(attr_result.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(attr_result.rows[0].at("attr_probe")), 16);

        trace_step("query attributes execute");
        const auto attr_exec = connection.execute(
            "SELECT ? AS param_value",
            {int64_t{17}},
            {{"trace", std::string("execute")}, {"flag", true}});
        trace_result("query attributes execute result", attr_exec);
        ASSERT_EQ(attr_exec.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(attr_exec.rows[0].at("param_value")), 17);

        bool attr_callback_seen = false;
        connection.query(
            "SELECT 18 AS attr_callback",
            {{"trace", std::string("callback")}},
            [&](std::exception_ptr error, mysql2::QueryResult callback_result) {
                EXPECT_EQ(error, nullptr);
                ASSERT_EQ(callback_result.rows.size(), 1u);
                EXPECT_EQ(std::get<int64_t>(callback_result.rows[0].at("attr_callback")), 18);
                attr_callback_seen = true;
            });
        EXPECT_TRUE(attr_callback_seen);

        bool attr_promise_seen = false;
        auto attr_promise = connection.execute_promise(
            "SELECT ? AS attr_promise",
            {int64_t{19}},
            {{"trace", std::string("promise")}});
        attr_promise.then([&](const mysql2::QueryResult& promise_result) {
            ASSERT_EQ(promise_result.rows.size(), 1u);
            EXPECT_EQ(std::get<int64_t>(promise_result.rows[0].at("attr_promise")), 19);
            attr_promise_seen = true;
        });
        polycpp::EventLoop::instance().restart();
        polycpp::EventLoop::instance().run();
        EXPECT_TRUE(attr_promise_seen);
    }

    bool callback_seen = false;
    trace_step("callback query");
    connection.query("SELECT 6 AS six", [&](std::exception_ptr error, mysql2::QueryResult callback_result) {
        EXPECT_EQ(error, nullptr);
        ASSERT_EQ(callback_result.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(callback_result.rows[0].at("six")), 6);
        callback_seen = true;
    });
    EXPECT_TRUE(callback_seen);

    bool promise_seen = false;
    trace_step("promise query");
    auto promise = connection.query_promise("SELECT 7 AS seven");
    promise.then([&](const mysql2::QueryResult& promise_result) {
        ASSERT_EQ(promise_result.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(promise_result.rows[0].at("seven")), 7);
        promise_seen = true;
    });
    polycpp::EventLoop::instance().restart();
    polycpp::EventLoop::instance().run();
    EXPECT_TRUE(promise_seen);

    trace_step("json stream query");
    auto json_stream = connection.query_stream_json("SELECT 8 AS eight");
    polycpp::EventLoop::instance().restart();
    const auto json_chunks = json_stream.toArray();
    ASSERT_EQ(json_chunks.size(), 1u);
    EXPECT_EQ(json_chunks[0].toString(), R"({"eight":8})" "\n");

    trace_step("typed row stream query");
    auto typed_stream = connection.query_stream("SELECT 9 AS nine");
    ASSERT_EQ(typed_stream.fields().size(), 1u);
    auto typed_row = typed_stream.read();
    ASSERT_TRUE(typed_row.has_value());
    EXPECT_EQ(std::get<int64_t>(typed_row->at("nine")), 9);
    EXPECT_FALSE(typed_stream.read().has_value());

    trace_step("empty value query");
    const auto empty_first = connection.query("SELECT '' AS empty_string, X'' AS empty_blob");
    ASSERT_EQ(empty_first.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(empty_first.rows[0].at("empty_string")), "");
    EXPECT_EQ(std::get<mysql2::Buffer>(empty_first.rows[0].at("empty_blob")).length(), 0u);

    trace_step("create temp table");
    const auto ddl = connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_t (id INT PRIMARY KEY, name VARCHAR(64))");
    EXPECT_FALSE(ddl.has_rows());
    const auto insert = connection.query("INSERT INTO polycpp_mysql2_t VALUES (1, 'alice'), (2, 'bob')");
    EXPECT_EQ(insert.ok.affected_rows, 2u);
    const auto rows = connection.query("SELECT id, name FROM polycpp_mysql2_t ORDER BY id");
    ASSERT_EQ(rows.rows.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(rows.rows[1].at("id")), 2);
    EXPECT_EQ(std::get<std::string>(rows.rows[1].at("name")), "bob");

    trace_step("execute cursor");
    auto cursor = connection.execute_cursor("SELECT id FROM polycpp_mysql2_t ORDER BY id");
    if (cursor.open()) {
        trace_step("cursor fetch first");
        const auto first_fetch = connection.fetch(cursor, 1);
        ASSERT_EQ(first_fetch.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(first_fetch.rows[0].at("id")), 1);
        trace_step("cursor fetch remaining");
        const auto remaining_fetch = connection.fetch(cursor, 16);
        ASSERT_GE(remaining_fetch.rows.size(), 1u);
        EXPECT_FALSE(cursor.open());
    }
    connection.close_statement(cursor.statement);

    trace_step("prepare insert");
    auto insert_stmt = connection.prepare("INSERT INTO polycpp_mysql2_t VALUES (?, ?)");
    EXPECT_EQ(insert_stmt.parameters.size(), 2u);
    const auto prepared_insert = connection.execute(insert_stmt, {int64_t{3}, std::string("carol")});
    EXPECT_EQ(prepared_insert.ok.affected_rows, 1u);
    connection.close_statement(insert_stmt);

    auto select_stmt = connection.prepare("SELECT id, name FROM polycpp_mysql2_t WHERE id > ? ORDER BY id");
    ASSERT_EQ(select_stmt.parameters.size(), 1u);
    const auto prepared_rows = connection.execute(select_stmt, {int64_t{1}});
    ASSERT_EQ(prepared_rows.rows.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(prepared_rows.rows[0].at("id")), 2);
    EXPECT_EQ(std::get<std::string>(prepared_rows.rows[0].at("name")), "bob");
    EXPECT_EQ(std::get<int64_t>(prepared_rows.rows[1].at("id")), 3);
    EXPECT_EQ(std::get<std::string>(prepared_rows.rows[1].at("name")), "carol");
    connection.close_statement(select_stmt);

    connection.begin_transaction();
    connection.query("INSERT INTO polycpp_mysql2_t VALUES (4, 'rolled_back')");
    connection.rollback();
    const auto rolled_back = connection.query("SELECT COUNT(*) AS count FROM polycpp_mysql2_t WHERE id = 4");
    ASSERT_EQ(rolled_back.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(rolled_back.rows[0].at("count")), 0);

    connection.begin_transaction();
    connection.query("INSERT INTO polycpp_mysql2_t VALUES (4, 'committed')");
    connection.commit();
    const auto committed = connection.query("SELECT COUNT(*) AS count FROM polycpp_mysql2_t WHERE id = 4");
    ASSERT_EQ(committed.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(committed.rows[0].at("count")), 1);

    const auto one_shot = connection.execute("SELECT ? AS label, ? AS none_value", {std::string("prepared"), std::monostate{}});
    ASSERT_EQ(one_shot.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(one_shot.rows[0].at("label")), "prepared");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(one_shot.rows[0].at("none_value")));

    const auto cached_one = connection.execute("SELECT ? AS cached_value", {int64_t{101}});
    const auto cached_two = connection.execute("SELECT ? AS cached_value", {int64_t{102}});
    ASSERT_EQ(cached_one.rows.size(), 1u);
    ASSERT_EQ(cached_two.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(cached_one.rows[0].at("cached_value")), 101);
    EXPECT_EQ(std::get<int64_t>(cached_two.rows[0].at("cached_value")), 102);
    connection.close_statement("SELECT ? AS cached_value");

    mysql2::ConnectionOptions same_user;
    same_user.user = options.user;
    same_user.password = options.password;
    same_user.database = options.database;
    same_user.charset = options.charset;
    connection.change_user(same_user);
    EXPECT_EQ(connection.ping().warning_count, 0u);

    connection.reset();
    EXPECT_TRUE(connection.connected());
    EXPECT_EQ(connection.encrypted(), options.ssl.enabled);
    EXPECT_EQ(connection.ping().warning_count, 0u);

    auto multi_options = options;
    multi_options.multiple_statements = true;
    auto multi_connection = mysql2::create_connection(multi_options);
    const auto multi_results = multi_connection.query_all("SELECT 11 AS first_num; SELECT 12 AS second_num");
    ASSERT_EQ(multi_results.size(), 2u);
    ASSERT_EQ(multi_results[0].rows.size(), 1u);
    ASSERT_EQ(multi_results[1].rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(multi_results[0].rows[0].at("first_num")), 11);
    EXPECT_EQ(std::get<int64_t>(multi_results[1].rows[0].at("second_num")), 12);
    EXPECT_THROW(multi_connection.query("SELECT 1 AS first_num; SELECT 2 AS second_num"), mysql2::Error);
    EXPECT_EQ(multi_connection.ping().warning_count, 0u);

    auto compressed_options = options;
    compressed_options.compress = true;
    auto compressed_connection = mysql2::create_connection(compressed_options);
    EXPECT_TRUE(compressed_connection.compressed());
    const auto compressed_result = compressed_connection.query("SELECT 13 AS compressed_num");
    ASSERT_EQ(compressed_result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(compressed_result.rows[0].at("compressed_num")), 13);

    const auto local_infile_setting = connection.query("SHOW VARIABLES LIKE 'local_infile'");
    if (!local_infile_setting.rows.empty() &&
        std::get<std::string>(local_infile_setting.rows[0].at("Value")) == "ON") {
        auto infile_options = options;
        infile_options.local_infile_handler = [](const std::string& path) {
            EXPECT_EQ(path, "polycpp-memory.csv");
            return std::vector<mysql2::Buffer>{mysql2::Buffer::from("1,Ada\n2,Lin\n")};
        };
        auto infile_connection = mysql2::create_connection(infile_options);
        infile_connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_infile (id INT, name VARCHAR(32))");
        const auto load = infile_connection.query(
            "LOAD DATA LOCAL INFILE 'polycpp-memory.csv' INTO TABLE polycpp_mysql2_infile FIELDS TERMINATED BY ',' LINES TERMINATED BY '\\n'");
        EXPECT_EQ(load.ok.affected_rows, 2u);
        const auto loaded = infile_connection.query("SELECT COUNT(*) AS count FROM polycpp_mysql2_infile");
        ASSERT_EQ(loaded.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(loaded.rows[0].at("count")), 2);
    }

    mysql2::PoolOptions pool_options;
    pool_options.connection = options;
    pool_options.connection_limit = 2;
    auto pool = mysql2::create_pool(pool_options);
    bool pool_connection_event = false;
    bool pool_acquire_event = false;
    bool pool_release_event = false;
    pool.on(mysql2::event::ConnectionCreated, [&](mysql2::Connection& pooled_connection) {
        pool_connection_event = pooled_connection.connected();
    });
    pool.on(mysql2::event::Acquire, [&](mysql2::Connection& pooled_connection) {
        pool_acquire_event = pooled_connection.connected();
    });
    pool.on(mysql2::event::Release, [&](mysql2::Connection& pooled_connection) {
        pool_release_event = pooled_connection.connected();
    });
    const auto pooled = pool.query("SELECT 5 AS five");
    ASSERT_EQ(pooled.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(pooled.rows[0].at("five")), 5);
    EXPECT_EQ(pool.total_count(), 1u);
    EXPECT_EQ(pool.idle_count(), 1u);
    EXPECT_TRUE(pool_connection_event);
    EXPECT_TRUE(pool_acquire_event);
    EXPECT_TRUE(pool_release_event);

    {
        auto pooled_connection = pool.get_connection();
        EXPECT_TRUE(static_cast<bool>(pooled_connection));
        EXPECT_TRUE(pooled_connection->connected());
        EXPECT_EQ(pool.idle_count(), 0u);
    }
    EXPECT_EQ(pool.idle_count(), 1u);

    auto pool_promise = pool.query_promise("SELECT 14 AS fourteen");
    bool pool_promise_seen = false;
    pool_promise.then([&](const mysql2::QueryResult& pool_result) {
        ASSERT_EQ(pool_result.rows.size(), 1u);
        EXPECT_EQ(std::get<int64_t>(pool_result.rows[0].at("fourteen")), 14);
        pool_promise_seen = true;
    });
    polycpp::EventLoop::instance().restart();
    polycpp::EventLoop::instance().run();
    EXPECT_TRUE(pool_promise_seen);

    auto cluster = mysql2::create_pool_cluster();
    cluster.add("primary", pool_options);
    EXPECT_EQ(cluster.node_count(), 1u);
    const auto clustered = cluster.query("SELECT 15 AS fifteen");
    ASSERT_EQ(clustered.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(clustered.rows[0].at("fifteen")), 15);
    cluster.end();

    trace_step("query timeout");
    mysql2::Connection timeout_connection(options);
    timeout_connection.connect();
    mysql2::QueryOptions timeout_query;
    timeout_query.sql = "SELECT SLEEP(2)";
    timeout_query.timeout_ms = 10;
    EXPECT_THROW(timeout_connection.query(timeout_query), mysql2::Error);
    EXPECT_FALSE(timeout_connection.connected());

    pool.end();
    EXPECT_EQ(pool.total_count(), 0u);
}
