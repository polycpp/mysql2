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

    auto connection = mysql2::create_connection(options);
    EXPECT_TRUE(connection.connected());
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

    const auto ddl = connection.query("CREATE TEMPORARY TABLE polycpp_mysql2_t (id INT PRIMARY KEY, name VARCHAR(64))");
    EXPECT_FALSE(ddl.has_rows());
    const auto insert = connection.query("INSERT INTO polycpp_mysql2_t VALUES (1, 'alice'), (2, 'bob')");
    EXPECT_EQ(insert.ok.affected_rows, 2u);
    const auto rows = connection.query("SELECT id, name FROM polycpp_mysql2_t ORDER BY id");
    ASSERT_EQ(rows.rows.size(), 2u);
    EXPECT_EQ(std::get<int64_t>(rows.rows[1].at("id")), 2);
    EXPECT_EQ(std::get<std::string>(rows.rows[1].at("name")), "bob");
}
