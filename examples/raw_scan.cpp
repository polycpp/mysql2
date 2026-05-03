#include <charconv>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

#include <polycpp/mysql2/mysql2.hpp>

namespace {

int env_int(const char* name, int fallback) {
    const char* value = std::getenv(name);
    return value ? std::stoi(value) : fallback;
}

int64_t parse_i64(std::string_view bytes) {
    int64_t value = 0;
    const char* first = bytes.data();
    const char* last = bytes.data() + bytes.size();
    const auto parsed = std::from_chars(first, last, value);
    if (parsed.ec != std::errc{} || parsed.ptr != last) {
        throw std::runtime_error("raw_scan expected an integer column");
    }
    return value;
}

}  // namespace

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

    const int limit = env_int("MYSQL2_EXAMPLE_LIMIT", 1000);
    const std::string sql =
        "WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < " +
        std::to_string(limit) + ") SELECT n FROM seq";

    auto connection = polycpp::mysql2::create_connection(options);
    int rows = 0;
    int64_t sum = 0;
    connection.query_each_raw(sql, [&](const polycpp::mysql2::RawRowView& row) {
        const auto& value = row.at("n");
        if (!value.is_null) {
            // Raw bytes point into the current packet and must not be retained.
            sum += parse_i64(value.bytes);
        }
        ++rows;
    });

    std::cout << "rows=" << rows << "\n";
    std::cout << "sum=" << sum << "\n";
}
