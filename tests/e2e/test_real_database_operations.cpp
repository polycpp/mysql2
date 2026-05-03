#include <gtest/gtest.h>

#include <exception>
#include <string>

#include <polycpp/mysql2/mysql2.hpp>

#include "../support/mysql2_test_utils.hpp"

namespace mysql2 = polycpp::mysql2;
namespace test = polycpp::mysql2::test;

namespace {

bool load_e2e_options(mysql2::ConnectionOptions& options, std::string& skip_reason) {
    try {
        options = test::options_from_env();
        return true;
    } catch (const std::exception& error) {
        skip_reason = error.what();
        return false;
    }
}

int64_t scalar_i64(mysql2::Connection& connection, const std::string& sql, const std::string& field) {
    const auto result = connection.query(sql);
    if (result.rows.size() != 1) {
        throw mysql2::Error("test expected exactly one row");
    }
    return test::value_as_i64(result.rows[0].at(field));
}

}  // namespace

TEST(mysql2_e2e_real_database_operations, prepared_statement_type_matrix_and_cache_reuse) {
    mysql2::ConnectionOptions options;
    std::string skip_reason;
    if (!load_e2e_options(options, skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }
    auto connection = mysql2::create_connection(options);

    connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_type_matrix_e2e ("
                     "id INT PRIMARY KEY, "
                     "unsigned_value BIGINT UNSIGNED, "
                     "name VARCHAR(64), "
                     "raw_value VARBINARY(8), "
                     "score DOUBLE, "
                     "note VARCHAR(64) NULL, "
                     "payload JSON NULL)");

    const auto raw = mysql2::Buffer::from({0x00, 0x01, 0x02, 0xff});
    auto insert = connection.prepare(
        "INSERT INTO polycpp_mysql2_type_matrix_e2e "
        "(id, unsigned_value, name, raw_value, score, note, payload) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)");
    const auto inserted = connection.execute(insert,
                                             {int64_t{7},
                                              uint64_t{9},
                                              std::string("Ada"),
                                              raw,
                                              double{12.5},
                                              std::monostate{},
                                              std::string(R"({"ok":true})")});
    EXPECT_EQ(inserted.ok.affected_rows, 1u);
    connection.close_statement(insert);

    const auto selected = connection.execute(
        "SELECT id, unsigned_value, name, raw_value, score, note, payload "
        "FROM polycpp_mysql2_type_matrix_e2e WHERE id = ?",
        {int64_t{7}});
    ASSERT_EQ(selected.rows.size(), 1u);
    const auto& row = selected.rows[0];
    EXPECT_EQ(test::value_as_i64(row.at("id")), 7);
    EXPECT_EQ(test::value_as_u64(row.at("unsigned_value")), 9u);
    EXPECT_EQ(test::value_as_string(row.at("name")), "Ada");
    const auto& selected_raw = test::value_as_buffer(row.at("raw_value"));
    ASSERT_EQ(selected_raw.length(), raw.length());
    EXPECT_EQ(selected_raw[0], 0x00);
    EXPECT_EQ(selected_raw[1], 0x01);
    EXPECT_EQ(selected_raw[2], 0x02);
    EXPECT_EQ(selected_raw[3], 0xff);
    EXPECT_DOUBLE_EQ(std::get<double>(row.at("score")), 12.5);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(row.at("note")));
    const auto payload = test::value_as_string(row.at("payload"));
    EXPECT_NE(payload.find("ok"), std::string::npos);
    EXPECT_NE(payload.find("true"), std::string::npos);

    const auto cached_one = connection.execute("SELECT ? AS cached_value", {int64_t{101}});
    const auto cached_two = connection.execute("SELECT ? AS cached_value", {int64_t{102}});
    ASSERT_EQ(cached_one.rows.size(), 1u);
    ASSERT_EQ(cached_two.rows.size(), 1u);
    EXPECT_EQ(test::value_as_i64(cached_one.rows[0].at("cached_value")), 101);
    EXPECT_EQ(test::value_as_i64(cached_two.rows[0].at("cached_value")), 102);
    connection.close_statement("SELECT ? AS cached_value");
}

TEST(mysql2_e2e_real_database_operations, transaction_savepoint_error_and_compression_recovery) {
    mysql2::ConnectionOptions options;
    std::string skip_reason;
    if (!load_e2e_options(options, skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }
    auto connection = mysql2::create_connection(options);
    connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_txn_e2e (id INT PRIMARY KEY, name VARCHAR(32))");

    connection.begin_transaction();
    connection.query("INSERT INTO polycpp_mysql2_txn_e2e VALUES (1, 'rolled_back')");
    connection.rollback();
    EXPECT_EQ(scalar_i64(connection, "SELECT COUNT(*) AS count FROM polycpp_mysql2_txn_e2e", "count"), 0);

    connection.begin_transaction();
    connection.query("INSERT INTO polycpp_mysql2_txn_e2e VALUES (1, 'kept')");
    connection.query("SAVEPOINT polycpp_mysql2_savepoint");
    connection.query("INSERT INTO polycpp_mysql2_txn_e2e VALUES (2, 'removed')");
    connection.query("ROLLBACK TO SAVEPOINT polycpp_mysql2_savepoint");
    connection.commit();
    EXPECT_EQ(scalar_i64(connection, "SELECT COUNT(*) AS count FROM polycpp_mysql2_txn_e2e", "count"), 1);
    EXPECT_EQ(scalar_i64(connection, "SELECT COUNT(*) AS count FROM polycpp_mysql2_txn_e2e WHERE id = 2", "count"), 0);

    try {
        (void)connection.query("SELECT * FROM definitely_not_a_valid_mysql2_e2e_statement");
        FAIL() << "expected SQL error";
    } catch (const mysql2::Error& error) {
        EXPECT_NE(std::string(error.what()).find("definitely_not_a_valid_mysql2_e2e_statement"), std::string::npos);
    }
    EXPECT_EQ(scalar_i64(connection, "SELECT COUNT(*) AS count FROM polycpp_mysql2_txn_e2e", "count"), 1);

    auto compressed_options = options;
    compressed_options.compress = true;
    auto compressed_connection = mysql2::create_connection(compressed_options);
    EXPECT_TRUE(compressed_connection.compressed());
    EXPECT_EQ(scalar_i64(compressed_connection, "SELECT 13 AS compressed_num", "compressed_num"), 13);
}

TEST(mysql2_e2e_real_database_operations, local_infile_policy_and_memory_handler) {
    mysql2::ConnectionOptions options;
    std::string skip_reason;
    if (!load_e2e_options(options, skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }
    auto probe = mysql2::create_connection(options);
    const auto local_infile_setting = probe.query("SHOW VARIABLES LIKE 'local_infile'");
    if (local_infile_setting.rows.empty() || test::value_as_string(local_infile_setting.rows[0].at("Value")) != "ON") {
        GTEST_SKIP() << "server local_infile is not enabled";
    }

    auto no_handler = mysql2::create_connection(options);
    no_handler.query("CREATE TEMPORARY TABLE polycpp_mysql2_infile_no_handler_e2e (id INT, name VARCHAR(32))");
    EXPECT_THROW((void)no_handler.query(
                     "LOAD DATA LOCAL INFILE 'polycpp-memory.csv' "
                     "INTO TABLE polycpp_mysql2_infile_no_handler_e2e "
                     "FIELDS TERMINATED BY ',' LINES TERMINATED BY '\\n'"),
                 mysql2::Error);
    no_handler.destroy();

    auto infile_options = options;
    infile_options.local_infile_handler = [](const std::string& path) {
        EXPECT_EQ(path, "polycpp-memory.csv");
        return std::vector<mysql2::Buffer>{mysql2::Buffer::from("1,Ada\n"), mysql2::Buffer::from("2,Lin\n")};
    };
    auto infile_connection = mysql2::create_connection(infile_options);
    infile_connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_infile_e2e (id INT, name VARCHAR(32))");
    const auto load = infile_connection.query(
        "LOAD DATA LOCAL INFILE 'polycpp-memory.csv' "
        "INTO TABLE polycpp_mysql2_infile_e2e "
        "FIELDS TERMINATED BY ',' LINES TERMINATED BY '\\n'");
    EXPECT_EQ(load.ok.affected_rows, 2u);
    EXPECT_EQ(scalar_i64(infile_connection, "SELECT COUNT(*) AS count FROM polycpp_mysql2_infile_e2e", "count"), 2);
    const auto rows = infile_connection.query("SELECT id, name FROM polycpp_mysql2_infile_e2e ORDER BY id");
    ASSERT_EQ(rows.rows.size(), 2u);
    EXPECT_EQ(test::value_as_string(rows.rows[0].at("name")), "Ada");
    EXPECT_EQ(test::value_as_string(rows.rows[1].at("name")), "Lin");
}

TEST(mysql2_e2e_real_database_operations, pool_reset_on_release_clears_session_state) {
    mysql2::ConnectionOptions options;
    std::string skip_reason;
    if (!load_e2e_options(options, skip_reason)) {
        GTEST_SKIP() << skip_reason;
    }
    mysql2::PoolOptions pool_options;
    pool_options.connection = options;
    pool_options.connection_limit = 1;
    pool_options.max_idle = 1;
    pool_options.reset_on_release = true;

    auto pool = mysql2::create_pool(pool_options);
    auto first = pool.get_connection();
    ASSERT_TRUE(static_cast<bool>(first));
    first->query("SET @polycpp_mysql2_reset_marker = 123");
    first.release();

    auto second = pool.get_connection();
    ASSERT_TRUE(static_cast<bool>(second));
    const auto marker = second->query("SELECT @polycpp_mysql2_reset_marker AS marker");
    ASSERT_EQ(marker.rows.size(), 1u);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(marker.rows[0].at("marker")));
    second.release();

    pool.end();
    EXPECT_EQ(pool.total_count(), 0u);
}
