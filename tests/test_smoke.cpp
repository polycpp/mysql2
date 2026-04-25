#include <gtest/gtest.h>

#include <cstdlib>
#include <string>

#include <polycpp/mysql2/mysql2.hpp>

namespace mysql2 = polycpp::mysql2;

TEST(sql_escape, scalar_values) {
    EXPECT_EQ(mysql2::escape(nullptr), "NULL");
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
    mysql2::Connection connection(options);
    EXPECT_EQ(connection.options().charset_number, 8);
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

    auto connection = mysql2::create_connection(options);
    EXPECT_TRUE(connection.connected());
    EXPECT_EQ(connection.encrypted(), options.ssl.enabled);
    EXPECT_GT(connection.connection_id(), 0u);
    EXPECT_FALSE(connection.server_version().empty());

    const auto pong = connection.ping();
    EXPECT_EQ(pong.warning_count, 0u);

    const auto result = connection.query("SELECT 1 AS one, 'two' AS two, NULL AS none");
    ASSERT_TRUE(result.has_rows());
    ASSERT_EQ(result.fields.size(), 3u);
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0].at("one")), 1);
    EXPECT_EQ(std::get<std::string>(result.rows[0].at("two")), "two");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(result.rows[0].at("none")));

    const auto empty_first = connection.query("SELECT '' AS empty_string, X'' AS empty_blob");
    ASSERT_EQ(empty_first.rows.size(), 1u);
    EXPECT_EQ(std::get<std::string>(empty_first.rows[0].at("empty_string")), "");
    EXPECT_EQ(std::get<mysql2::Buffer>(empty_first.rows[0].at("empty_blob")).length(), 0u);

    const auto ddl = connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_t (id INT PRIMARY KEY, name VARCHAR(64))");
    EXPECT_FALSE(ddl.has_rows());
    const auto insert = connection.query("INSERT INTO polycpp_mysql2_t VALUES (1, 'alice'), (2, 'bob')");
    EXPECT_EQ(insert.ok.affected_rows, 2u);
    const auto rows = connection.query("SELECT id, name FROM polycpp_mysql2_t ORDER BY id");
    ASSERT_EQ(rows.rows.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(rows.rows[1].at("id")), 2);
    EXPECT_EQ(std::get<std::string>(rows.rows[1].at("name")), "bob");

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

    mysql2::PoolOptions pool_options;
    pool_options.connection = options;
    pool_options.connection_limit = 2;
    auto pool = mysql2::create_pool(pool_options);
    const auto pooled = pool.query("SELECT 5 AS five");
    ASSERT_EQ(pooled.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(pooled.rows[0].at("five")), 5);
    EXPECT_EQ(pool.total_count(), 1u);
    EXPECT_EQ(pool.idle_count(), 1u);

    {
        auto pooled_connection = pool.get_connection();
        EXPECT_TRUE(static_cast<bool>(pooled_connection));
        EXPECT_TRUE(pooled_connection->connected());
        EXPECT_EQ(pool.idle_count(), 0u);
    }
    EXPECT_EQ(pool.idle_count(), 1u);
    pool.end();
    EXPECT_EQ(pool.total_count(), 0u);
}
