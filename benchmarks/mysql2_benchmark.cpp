#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <polycpp/mysql2/mysql2.hpp>

#if defined(POLYCPP_MYSQL2_HAS_NATIVE_C_API)
#if __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#elif __has_include(<mysql.h>)
#include <mysql.h>
#else
#error "POLYCPP_MYSQL2_HAS_NATIVE_C_API is set but mysql.h is not available"
#endif
#endif

namespace mysql2 = polycpp::mysql2;

namespace {

struct Options {
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user = "root";
    std::string password;
    std::string database;
    int iterations = 1000;
    int fetch_rows = 1000;
};

const char* env(const char* name) {
    return std::getenv(name);
}

int env_int(const char* name, int fallback) {
    const char* value = env(name);
    return value ? std::stoi(value) : fallback;
}

Options options_from_env() {
    Options options;
    if (env("MYSQL2_TEST_HOST")) options.host = env("MYSQL2_TEST_HOST");
    if (env("MYSQL2_TEST_PORT")) options.port = static_cast<unsigned int>(std::stoul(env("MYSQL2_TEST_PORT")));
    if (env("MYSQL2_TEST_USER")) options.user = env("MYSQL2_TEST_USER");
    if (env("MYSQL2_TEST_PASSWORD")) options.password = env("MYSQL2_TEST_PASSWORD");
    if (env("MYSQL2_TEST_DATABASE")) options.database = env("MYSQL2_TEST_DATABASE");
    options.iterations = env_int("MYSQL2_BENCHMARK_ITERATIONS", options.iterations);
    options.fetch_rows = env_int("MYSQL2_BENCHMARK_ROWS", options.fetch_rows);
    return options;
}

mysql2::ConnectionOptions connection_options(const Options& options) {
    mysql2::ConnectionOptions connection;
    connection.host = options.host;
    connection.port = static_cast<uint16_t>(options.port);
    connection.user = options.user;
    connection.password = options.password;
    connection.database = options.database;
    connection.connect_timeout_ms = 5000;
    return connection;
}

template <typename Fn>
double measure_ms(Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    fn();
    const auto stop = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(stop - start).count();
}

void print_result(const std::string& client, const std::string& workload, int iterations, double total_ms) {
    const double ops_per_sec = total_ms > 0.0 ? (static_cast<double>(iterations) * 1000.0 / total_ms) : 0.0;
    std::cout << client << ',' << workload << ',' << iterations << ',' << std::fixed << std::setprecision(3)
              << total_ms << ',' << ops_per_sec << '\n';
}

void run_polycpp(const Options& options) {
    auto connection = mysql2::create_connection(connection_options(options));
    connection.ping();

    const auto text_ms = measure_ms([&] {
        for (int i = 0; i < options.iterations; ++i) {
            const auto result = connection.query("SELECT 1 AS one");
            if (result.rows.size() != 1) throw std::runtime_error("polycpp text query returned wrong row count");
        }
    });
    print_result("polycpp_mysql2", "text_select_1", options.iterations, text_ms);

    auto statement = connection.prepare("SELECT ? + ? AS sum_value");
    const auto prepared_ms = measure_ms([&] {
        for (int i = 0; i < options.iterations; ++i) {
            const auto result = connection.execute(statement, {int64_t{i}, int64_t{1}});
            if (result.rows.size() != 1) throw std::runtime_error("polycpp prepared query returned wrong row count");
        }
    });
    print_result("polycpp_mysql2", "prepared_add", options.iterations, prepared_ms);
    connection.close_statement(statement);

    const auto fetch_sql = "WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < " +
        std::to_string(options.fetch_rows) + ") SELECT n FROM seq";
    const auto fetch_ms = measure_ms([&] {
        const auto result = connection.query(fetch_sql);
        if (static_cast<int>(result.rows.size()) != options.fetch_rows) {
            throw std::runtime_error("polycpp fetch row count mismatch");
        }
    });
    print_result("polycpp_mysql2", "fetch_rows", options.fetch_rows, fetch_ms);
}

#if defined(POLYCPP_MYSQL2_HAS_NATIVE_C_API)

class NativeConnection {
public:
    explicit NativeConnection(const Options& options) {
        mysql_ = mysql_init(nullptr);
        if (!mysql_) throw std::runtime_error("mysql_init failed");
        const unsigned int timeout = 5;
        mysql_options(mysql_, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
        mysql_options(mysql_, MYSQL_SET_CHARSET_NAME, "utf8mb4");
        if (!mysql_real_connect(mysql_,
                                options.host.c_str(),
                                options.user.c_str(),
                                options.password.c_str(),
                                options.database.empty() ? nullptr : options.database.c_str(),
                                options.port,
                                nullptr,
                                0)) {
            throw std::runtime_error(std::string("mysql_real_connect failed: ") + mysql_error(mysql_));
        }
    }

    ~NativeConnection() {
        if (mysql_) mysql_close(mysql_);
    }

    NativeConnection(const NativeConnection&) = delete;
    NativeConnection& operator=(const NativeConnection&) = delete;

    MYSQL* get() { return mysql_; }

private:
    MYSQL* mysql_ = nullptr;
};

void native_query(MYSQL* mysql, const std::string& sql) {
    if (mysql_query(mysql, sql.c_str()) != 0) {
        throw std::runtime_error(std::string("mysql_query failed: ") + mysql_error(mysql));
    }
    MYSQL_RES* result = mysql_store_result(mysql);
    if (result) {
        while (mysql_fetch_row(result)) {
        }
        mysql_free_result(result);
    } else if (mysql_field_count(mysql) != 0) {
        throw std::runtime_error(std::string("mysql_store_result failed: ") + mysql_error(mysql));
    }
}

void run_native(const Options& options) {
    NativeConnection connection(options);
    native_query(connection.get(), "SELECT 1 AS one");

    const auto text_ms = measure_ms([&] {
        for (int i = 0; i < options.iterations; ++i) {
            native_query(connection.get(), "SELECT 1 AS one");
        }
    });
    print_result("native_c_api", "text_select_1", options.iterations, text_ms);

    MYSQL_STMT* statement = mysql_stmt_init(connection.get());
    if (!statement) throw std::runtime_error("mysql_stmt_init failed");
    const char* sql = "SELECT ? + ? AS sum_value";
    if (mysql_stmt_prepare(statement, sql, static_cast<unsigned long>(std::char_traits<char>::length(sql))) != 0) {
        const std::string error = mysql_stmt_error(statement);
        mysql_stmt_close(statement);
        throw std::runtime_error("mysql_stmt_prepare failed: " + error);
    }

    long long left = 0;
    long long right = 1;
    long long sum = 0;
    MYSQL_BIND params[2]{};
    params[0].buffer_type = MYSQL_TYPE_LONGLONG;
    params[0].buffer = &left;
    params[1].buffer_type = MYSQL_TYPE_LONGLONG;
    params[1].buffer = &right;
    MYSQL_BIND result_bind[1]{};
    result_bind[0].buffer_type = MYSQL_TYPE_LONGLONG;
    result_bind[0].buffer = &sum;

    if (mysql_stmt_bind_param(statement, params) != 0 || mysql_stmt_bind_result(statement, result_bind) != 0) {
        const std::string error = mysql_stmt_error(statement);
        mysql_stmt_close(statement);
        throw std::runtime_error("mysql_stmt_bind failed: " + error);
    }

    const auto prepared_ms = measure_ms([&] {
        for (int i = 0; i < options.iterations; ++i) {
            left = i;
            if (mysql_stmt_execute(statement) != 0 || mysql_stmt_store_result(statement) != 0) {
                throw std::runtime_error(std::string("mysql_stmt_execute failed: ") + mysql_stmt_error(statement));
            }
            const int fetch_status = mysql_stmt_fetch(statement);
            if (fetch_status != 0 && fetch_status != MYSQL_DATA_TRUNCATED) {
                throw std::runtime_error(std::string("mysql_stmt_fetch failed: ") + mysql_stmt_error(statement));
            }
            mysql_stmt_free_result(statement);
        }
    });
    print_result("native_c_api", "prepared_add", options.iterations, prepared_ms);
    mysql_stmt_close(statement);

    const auto fetch_sql = "WITH RECURSIVE seq(n) AS (SELECT 1 UNION ALL SELECT n + 1 FROM seq WHERE n < " +
        std::to_string(options.fetch_rows) + ") SELECT n FROM seq";
    const auto fetch_ms = measure_ms([&] { native_query(connection.get(), fetch_sql); });
    print_result("native_c_api", "fetch_rows", options.fetch_rows, fetch_ms);
}

#endif

}  // namespace

int main() {
    try {
        const auto options = options_from_env();
        std::cout << "client,workload,iterations,total_ms,ops_per_sec\n";
        run_polycpp(options);
#if defined(POLYCPP_MYSQL2_HAS_NATIVE_C_API)
        run_native(options);
#else
        std::cerr << "native C API comparison not compiled; configure with "
                     "-DPOLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=ON to enable it.\n";
#endif
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
