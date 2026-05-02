#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <optional>
#include <string>
#include <thread>

#include <polycpp/mysql2/mysql2.hpp>

#include "../support/mysql2_test_utils.hpp"

namespace mysql2 = polycpp::mysql2;
namespace test = polycpp::mysql2::test;

TEST(mysql2_e2e_server_protocol, query_error_packet_is_exposed_as_mysql_error) {
    std::atomic<bool> query_seen{false};

    auto server = mysql2::create_server();
    server.on(mysql2::event::ServerConnectionAccepted, [&](mysql2::ServerConnection& connection) {
        connection.on(mysql2::event::ServerQuery, [&](mysql2::ServerConnection& conn, const std::string& sql) {
            query_seen = sql == "SELECT broken";
            conn.write_error(1064, "42000", "syntax error from e2e server");
        });
    });
    server.listen();

    mysql2::ConnectionOptions options;
    options.host = server.address();
    options.port = server.port();
    options.user = "polycpp";
    options.connect_timeout_ms = 5000;

    auto client = mysql2::create_connection(options);
    try {
        (void)client.query("SELECT broken");
        FAIL() << "expected server ERR packet to throw";
    } catch (const mysql2::Error& error) {
        EXPECT_EQ(error.code(), 1064);
        EXPECT_EQ(error.sql_state(), "42000");
        EXPECT_NE(std::string(error.what()).find("syntax error from e2e server"), std::string::npos);
    }

    client.end();
    server.close();
    EXPECT_TRUE(query_seen);
}

TEST(mysql2_e2e_server_protocol, tls_clear_password_auth_sends_cleartext_only_when_enabled) {
    const char* cert_file = std::getenv("MYSQL2_TEST_SERVER_TLS_CERT_FILE");
    const char* key_file = std::getenv("MYSQL2_TEST_SERVER_TLS_KEY_FILE");
    if (!cert_file || !key_file) {
        GTEST_SKIP() << "set MYSQL2_TEST_SERVER_TLS_CERT_FILE and MYSQL2_TEST_SERVER_TLS_KEY_FILE";
    }

    std::atomic<bool> auth_seen{false};
    std::atomic<bool> query_seen{false};

    mysql2::ServerOptions server_options;
    server_options.handshake.server_version = "polycpp-mysql2-clear-password-test";
    server_options.handshake.auth_plugin_name = "mysql_clear_password";
    server_options.handshake.auth_callback = [&](const mysql2::ServerAuthInfo& info) -> std::optional<mysql2::Error> {
        auth_seen = true;
        const auto token = info.auth_token.toString("latin1");
        if (token != std::string("secret\0", 7)) {
            return mysql2::Error(1045, "28000", "unexpected clear password token");
        }
        return std::nullopt;
    };
    server_options.tls.enabled = true;
    server_options.tls.cert_file = cert_file;
    server_options.tls.key_file = key_file;

    auto server = mysql2::create_server(server_options);
    server.on(mysql2::event::ServerConnectionAccepted, [&](mysql2::ServerConnection& connection) {
        connection.on(mysql2::event::ServerQuery, [&](mysql2::ServerConnection& conn, const std::string& sql) {
            query_seen = sql == "SELECT 1 AS ok";
            mysql2::Field field;
            field.name = "ok";
            field.column_type = mysql2::constants::column_type::LONG;
            mysql2::Row row;
            row.values = {int64_t{1}};
            conn.write_text_result({row}, {field});
        });
    });
    server.listen();

    mysql2::ConnectionOptions insecure_options;
    insecure_options.host = server.address();
    insecure_options.port = server.port();
    insecure_options.user = "polycpp";
    insecure_options.password = "secret";
    insecure_options.connect_timeout_ms = 5000;
    insecure_options.ssl.enabled = true;
    insecure_options.ssl.reject_unauthorized = false;
    insecure_options.ssl.verify_identity = false;
    EXPECT_THROW((void)mysql2::create_connection(insecure_options), mysql2::Error);

    mysql2::ConnectionOptions options = insecure_options;
    options.enable_cleartext_plugin = true;
    auto client = mysql2::create_connection(options);
    EXPECT_TRUE(client.encrypted());
    const auto result = client.query("SELECT 1 AS ok");
    ASSERT_EQ(result.rows.size(), 1u);
    EXPECT_EQ(std::get<int64_t>(result.rows[0].at("ok")), 1);
    client.end();

    for (int i = 0; i < 50 && (!auth_seen.load() || !query_seen.load()); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    server.close();

    EXPECT_TRUE(auth_seen);
    EXPECT_TRUE(query_seen);
}

