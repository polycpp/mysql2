#include <gtest/gtest.h>

#include <string>

#include <polycpp/mysql2/mysql2.hpp>

#include "../support/mysql2_test_utils.hpp"

namespace mysql2 = polycpp::mysql2;
namespace test = polycpp::mysql2::test;

TEST(mysql2_e2e_replication, binlog_stream_decodes_temporal_rows_and_closes_transport) {
    if (!test::env_enabled("MYSQL2_TEST_REPLICATION")) {
        GTEST_SKIP() << "Set MYSQL2_TEST_REPLICATION=1 with a binary-log-enabled server to run replication e2e";
    }

    mysql2::ConnectionOptions options;
    try {
        options = test::options_from_env();
    } catch (const std::exception& error) {
        GTEST_SKIP() << error.what();
    }

    mysql2::Connection writer(options);
    writer.connect();
    const auto log_bin = writer.query("SELECT @@log_bin AS log_bin");
    ASSERT_EQ(log_bin.rows.size(), 1u);
    if (test::value_as_u64(log_bin.rows[0].at("log_bin")) == 0) {
        GTEST_SKIP() << "Server binary logging is disabled";
    }
    try {
        writer.query("SET SESSION binlog_format = 'ROW'");
    } catch (const mysql2::Error&) {
    }
    const auto binlog_format = writer.query("SELECT @@session.binlog_format AS binlog_format");
    ASSERT_EQ(binlog_format.rows.size(), 1u);
    if (test::value_as_string(binlog_format.rows[0].at("binlog_format")) != "ROW") {
        GTEST_SKIP() << "Replication e2e requires ROW binlog_format";
    }

    writer.query("SET time_zone = '+00:00'");
    writer.query("DROP TABLE IF EXISTS polycpp_mysql2_binlog_e2e_matrix");
    writer.query("CREATE TABLE polycpp_mysql2_binlog_e2e_matrix ("
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
    dump.filename = test::value_as_string(status.rows[0].at("File"));
    dump.binlog_position = test::value_as_u64(status.rows[0].at("Position"));
    dump.flags = mysql2::constants::binlog_dump_flags::NON_BLOCK;
    dump.server_id = 62002;
    dump.max_events = 64;

    writer.query("INSERT INTO polycpp_mysql2_binlog_e2e_matrix "
                 "VALUES (1, 'Ada', '2009-02-13 23:31:30.123456', "
                 "'2024-05-06 07:08:09.123456', '12:34:56.123456'), "
                 "(2, 'Neo', '2009-02-13 23:31:31.654321', "
                 "'2025-06-07 08:09:10.654321', '-00:00:00.010000')");

    mysql2::Connection reader(options);
    auto stream = reader.create_binlog_stream(dump);
    EXPECT_THROW(reader.query("SELECT 1"), mysql2::Error);

    bool saw_table_map = false;
    bool saw_write_rows = false;
    while (auto event = stream.read()) {
        if (event->name == "TableMapEvent" && event->table == "polycpp_mysql2_binlog_e2e_matrix") {
            saw_table_map = true;
        }
        if ((event->name == "WriteRowsEventV1" || event->name == "WriteRowsEventV2") &&
            event->table == "polycpp_mysql2_binlog_e2e_matrix") {
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
    abandon_dump.filename = test::value_as_string(after_status.rows[0].at("File"));
    abandon_dump.binlog_position = test::value_as_u64(after_status.rows[0].at("Position"));
    abandon_dump.flags = mysql2::constants::binlog_dump_flags::NON_BLOCK;
    abandon_dump.server_id = 62004;
    abandon_dump.max_events = 0;
    mysql2::Connection abandon_reader(options);
    auto abandoned = abandon_reader.create_binlog_stream(abandon_dump);
    abandoned.destroy();
    EXPECT_FALSE(abandon_reader.connected());
}

