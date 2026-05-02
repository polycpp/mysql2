#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <string>
#include <thread>

#include <polycpp/mysql2/mysql2.hpp>

#include "../support/mysql2_test_utils.hpp"

namespace mysql2 = polycpp::mysql2;
namespace test = polycpp::mysql2::test;

TEST(mysql2_e2e_real_database, charset_matrix_binary_and_iconv_decoding_when_configured) {
    mysql2::ConnectionOptions options;
    try {
        options = test::options_from_env();
    } catch (const std::exception& error) {
        GTEST_SKIP() << error.what();
    }
    auto connection = mysql2::create_connection(options);

    const auto utf8 = connection.query("SELECT 'snowman ☃' AS utf8_value");
    ASSERT_EQ(utf8.rows.size(), 1u);
    EXPECT_EQ(test::value_as_string(utf8.rows[0].at("utf8_value")), "snowman ☃");

    const auto binary = connection.query("SELECT CAST(X'0001FF' AS BINARY(3)) AS raw_value");
    ASSERT_EQ(binary.rows.size(), 1u);
    const auto& raw = test::value_as_buffer(binary.rows[0].at("raw_value"));
    ASSERT_EQ(raw.length(), 3u);
    EXPECT_EQ(raw[0], 0x00);
    EXPECT_EQ(raw[1], 0x01);
    EXPECT_EQ(raw[2], 0xff);

    const auto latin1 = connection.query("SELECT CONVERT(_binary X'E9' USING latin1) AS latin1_value");
    ASSERT_EQ(latin1.rows.size(), 1u);
    EXPECT_EQ(test::value_as_string(latin1.rows[0].at("latin1_value")), "é");

    const auto sjis_charset = connection.query("SHOW CHARACTER SET LIKE 'sjis'");
    if (!sjis_charset.rows.empty()) {
        const auto sjis = connection.query("SELECT CONVERT(_binary X'82A0' USING sjis) AS sjis_value");
        ASSERT_EQ(sjis.rows.size(), 1u);
        EXPECT_EQ(test::value_as_string(sjis.rows[0].at("sjis_value")), "あ");
    }
}

TEST(mysql2_e2e_real_database, stored_procedure_multi_result_sets_are_drained_and_typed) {
    mysql2::ConnectionOptions options;
    try {
        options = test::options_from_env();
    } catch (const std::exception& error) {
        GTEST_SKIP() << error.what();
    }
    auto connection = mysql2::create_connection(options);

    try {
        connection.query("DROP PROCEDURE IF EXISTS polycpp_mysql2_multi_result_e2e");
        connection.query(
            "CREATE PROCEDURE polycpp_mysql2_multi_result_e2e() "
            "BEGIN "
            "SELECT 31 AS first_num; "
            "SELECT 'second' AS second_value; "
            "END");
    } catch (const mysql2::Error& error) {
        GTEST_SKIP() << "stored procedure e2e requires CREATE ROUTINE privilege: " << error.what();
    }

    const auto results = connection.query_all("CALL polycpp_mysql2_multi_result_e2e()");
    ASSERT_GE(results.size(), 2u);
    ASSERT_EQ(results[0].rows.size(), 1u);
    ASSERT_EQ(results[1].rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(results[0].rows[0].at("first_num")), 31);
    EXPECT_EQ(std::get<std::string>(results[1].rows[0].at("second_value")), "second");
    EXPECT_NO_THROW((void)connection.query("SELECT 1 AS reusable_after_call"));

    connection.query("DROP PROCEDURE IF EXISTS polycpp_mysql2_multi_result_e2e");
}

TEST(mysql2_e2e_real_database, pool_contention_wait_timeout_and_recovery) {
    mysql2::ConnectionOptions options;
    try {
        options = test::options_from_env();
    } catch (const std::exception& error) {
        GTEST_SKIP() << error.what();
    }

    mysql2::PoolOptions pool_options;
    pool_options.connection = options;
    pool_options.connection_limit = 1;
    pool_options.max_idle = 1;
    pool_options.wait_for_connections = true;
    pool_options.wait_timeout_ms = 100;
    pool_options.queue_limit = 1;

    auto pool = mysql2::create_pool(pool_options);
    std::atomic<bool> enqueue_seen{false};
    pool.on(mysql2::event::Enqueue, [&] {
        enqueue_seen = true;
    });

    auto held = pool.get_connection();
    ASSERT_TRUE(static_cast<bool>(held));
    EXPECT_EQ(pool.total_count(), 1u);
    EXPECT_EQ(pool.idle_count(), 0u);

    std::atomic<bool> timeout_seen{false};
    std::thread waiter([&] {
        try {
            auto second = pool.get_connection();
            (void)second;
        } catch (const mysql2::Error& error) {
            timeout_seen = std::string(error.what()).find("timed out waiting") != std::string::npos;
        }
    });
    waiter.join();
    EXPECT_TRUE(enqueue_seen);
    EXPECT_TRUE(timeout_seen);

    held.release();
    EXPECT_EQ(pool.idle_count(), 1u);
    const auto result = pool.query("SELECT 41 AS recovered");
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0].at("recovered")), 41);
    pool.end();
    EXPECT_EQ(pool.total_count(), 0u);
}
