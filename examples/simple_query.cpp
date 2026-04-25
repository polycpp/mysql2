#include <cstdlib>
#include <iostream>
#include <string>

#include <polycpp/mysql2/mysql2.hpp>

int main() {
    const char* host = std::getenv("MYSQL2_TEST_HOST");
    if (!host) {
        std::cerr << "Set MYSQL2_TEST_HOST, MYSQL2_TEST_USER, and optional MYSQL2_TEST_* variables.\n";
        return 2;
    }

    polycpp::mysql2::ConnectionOptions options;
    options.host = host;
    options.port = std::getenv("MYSQL2_TEST_PORT") ? static_cast<uint16_t>(std::stoi(std::getenv("MYSQL2_TEST_PORT"))) : 3306;
    options.user = std::getenv("MYSQL2_TEST_USER") ? std::getenv("MYSQL2_TEST_USER") : "root";
    options.password = std::getenv("MYSQL2_TEST_PASSWORD") ? std::getenv("MYSQL2_TEST_PASSWORD") : "";
    options.database = std::getenv("MYSQL2_TEST_DATABASE") ? std::getenv("MYSQL2_TEST_DATABASE") : "";

    auto connection = polycpp::mysql2::create_connection(options);
    auto result = connection.query("SELECT 1 AS one, 'polycpp' AS label");

    std::cout << "server=" << connection.server_version() << "\n";
    std::cout << "one=" << std::get<int64_t>(result.rows[0].at("one")) << "\n";
    std::cout << "label=" << std::get<std::string>(result.rows[0].at("label")) << "\n";
}
