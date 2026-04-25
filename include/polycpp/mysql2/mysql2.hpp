#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include <polycpp/buffer.hpp>
#include <polycpp/core/error.hpp>
#include <polycpp/core/json.hpp>
#include <polycpp/core/promise.hpp>
#include <polycpp/events.hpp>
#include <polycpp/stream.hpp>

namespace polycpp::mysql2 {

using Buffer = polycpp::buffer::Buffer;

class Error : public polycpp::Error {
public:
    explicit Error(const std::string& message);
    Error(uint16_t code, std::string sql_state, std::string message);

    uint16_t code() const noexcept;
    const std::string& sql_state() const noexcept;

private:
    uint16_t code_ = 0;
    std::string sql_state_;
};

struct SslOptions {
    bool enabled = false;
    bool reject_unauthorized = true;
    bool verify_identity = true;
    bool load_default_verify_paths = true;
    std::string ca_pem;
    std::string ca_file;
    std::string cert_pem;
    std::string cert_file;
    std::string key_pem;
    std::string key_file;
    std::string key_passphrase;
};

struct ConnectionOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 3306;
    std::string user;
    std::string password;
    std::string database;
    uint32_t connect_timeout_ms = 10000;
    bool enable_keep_alive = true;
    bool multiple_statements = false;
    bool support_big_numbers = true;
    bool big_number_strings = false;
    bool decimal_numbers = false;
    std::string charset = "utf8mb4";
    uint8_t charset_number = 224;  // UTF8MB4_UNICODE_CI, matching mysql2 default.
    std::string server_public_key_pem;
    bool enable_cleartext_plugin = false;
    bool compress = false;
    std::size_t max_prepared_statements = 16000;
    std::function<std::vector<Buffer>(const std::string& path)> local_infile_handler;
    SslOptions ssl;
};

struct Field {
    std::string catalog;
    std::string schema;
    std::string table;
    std::string org_table;
    std::string name;
    std::string org_name;
    uint16_t character_set = 0;
    std::string encoding;
    uint32_t column_length = 0;
    uint8_t column_type = 0;
    uint16_t flags = 0;
    uint8_t decimals = 0;

    bool is_unsigned() const noexcept;
    bool is_binary() const noexcept;
};

struct RawSql {
    std::string sql;
};

using Value = std::variant<std::monostate, int64_t, uint64_t, double, std::string, Buffer, RawSql>;

struct Row {
    std::vector<Value> values;
    std::unordered_map<std::string, std::size_t> index_by_name;

    const Value& at(std::size_t index) const;
    const Value& at(const std::string& name) const;
    JsonObject to_json_object(const std::vector<Field>& fields) const;
};

struct OkPacket {
    uint64_t affected_rows = 0;
    uint64_t insert_id = 0;
    uint16_t server_status = 0;
    uint16_t warning_count = 0;
    std::string info;
    uint64_t changed_rows = 0;
};

struct QueryResult {
    OkPacket ok;
    std::vector<Field> fields;
    std::vector<Row> rows;

    bool has_rows() const noexcept;
    JsonValue to_json() const;
};

struct PreparedStatement {
    uint32_t id = 0;
    std::string query;
    std::vector<Field> parameters;
    std::vector<Field> columns;
};

struct PoolOptions {
    ConnectionOptions connection;
    std::size_t connection_limit = 10;
    std::size_t max_idle = 10;
    std::size_t queue_limit = 0;
    uint32_t wait_timeout_ms = 10000;
    uint32_t idle_timeout_ms = 60000;
    bool wait_for_connections = true;
    bool reset_on_release = false;
};

enum class PoolSelector {
    RoundRobin,
    Random,
    Order
};

struct PoolClusterOptions {
    bool can_retry = true;
    std::size_t remove_node_error_count = 5;
    uint32_t restore_node_timeout_ms = 0;
    PoolSelector default_selector = PoolSelector::RoundRobin;
};

struct ConnectionInfo {
    uint32_t connection_id = 0;
    std::string server_version;
    uint32_t server_capability_flags = 0;
    bool encrypted = false;
};

using VoidCallback = std::function<void(std::exception_ptr)>;
using OkCallback = std::function<void(std::exception_ptr, OkPacket)>;
using QueryCallback = std::function<void(std::exception_ptr, QueryResult)>;
using QueryAllCallback = std::function<void(std::exception_ptr, std::vector<QueryResult>)>;
using PrepareCallback = std::function<void(std::exception_ptr, PreparedStatement)>;

RawSql raw(std::string sql);
std::string escape(const Value& value);
std::string escape(std::nullptr_t);
std::string escape(const std::string& value);
std::string escape(const char* value);
std::string escape(double value);
std::string escape(int64_t value);
std::string escape(uint64_t value);
std::string escape_id(const std::string& identifier, bool forbid_qualified = false);
std::string format(const std::string& sql, const std::vector<Value>& values);
std::string format_named(const std::string& sql, const std::unordered_map<std::string, Value>& values);
JsonValue value_to_json(const Value& value);
std::string row_to_json_line(const Row& row, const std::vector<Field>& fields);
ConnectionOptions parse_connection_uri(const std::string& uri);

class Connection : public events::EventEmitterForwarder {
public:
    Connection();
    explicit Connection(ConnectionOptions options);
    ~Connection();

    Connection(Connection&&) noexcept;
    Connection& operator=(Connection&&) noexcept;

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    void connect();
    void connect(VoidCallback callback);
    Promise<void> connect_promise();
    QueryResult query(const std::string& sql);
    void query(const std::string& sql, QueryCallback callback);
    Promise<QueryResult> query_promise(const std::string& sql);
    std::vector<QueryResult> query_all(const std::string& sql);
    void query_all(const std::string& sql, QueryAllCallback callback);
    Promise<std::vector<QueryResult>> query_all_promise(const std::string& sql);
    stream::Readable query_stream_json(const std::string& sql);
    PreparedStatement prepare(const std::string& sql);
    void prepare(const std::string& sql, PrepareCallback callback);
    Promise<PreparedStatement> prepare_promise(const std::string& sql);
    QueryResult execute(const PreparedStatement& statement, const std::vector<Value>& values = {});
    void execute(const PreparedStatement& statement, const std::vector<Value>& values, QueryCallback callback);
    Promise<QueryResult> execute_promise(const PreparedStatement& statement, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const PreparedStatement& statement, const std::vector<Value>& values = {});
    void execute_all(const PreparedStatement& statement, const std::vector<Value>& values, QueryAllCallback callback);
    Promise<std::vector<QueryResult>> execute_all_promise(const PreparedStatement& statement, const std::vector<Value>& values = {});
    QueryResult execute(const std::string& sql, const std::vector<Value>& values = {});
    void execute(const std::string& sql, const std::vector<Value>& values, QueryCallback callback);
    Promise<QueryResult> execute_promise(const std::string& sql, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values = {});
    void execute_all(const std::string& sql, const std::vector<Value>& values, QueryAllCallback callback);
    Promise<std::vector<QueryResult>> execute_all_promise(const std::string& sql, const std::vector<Value>& values = {});
    void close_statement(const PreparedStatement& statement);
    void close_statement(const std::string& sql);
    OkPacket begin_transaction();
    void begin_transaction(OkCallback callback);
    Promise<OkPacket> begin_transaction_promise();
    OkPacket commit();
    void commit(OkCallback callback);
    Promise<OkPacket> commit_promise();
    OkPacket rollback();
    void rollback(OkCallback callback);
    Promise<OkPacket> rollback_promise();
    OkPacket ping();
    void ping(OkCallback callback);
    Promise<OkPacket> ping_promise();
    void reset();
    void reset(VoidCallback callback);
    Promise<void> reset_promise();
    void change_user(ConnectionOptions options);
    void change_user(ConnectionOptions options, VoidCallback callback);
    Promise<void> change_user_promise(ConnectionOptions options);
    void end();
    void end(VoidCallback callback);
    Promise<void> end_promise();
    void destroy() noexcept;
    void pause() noexcept;
    void resume() noexcept;

    bool connected() const noexcept;
    bool encrypted() const noexcept;
    bool compressed() const noexcept;
    bool paused() const noexcept;
    const ConnectionOptions& options() const noexcept;
    const std::string& server_version() const noexcept;
    uint32_t connection_id() const noexcept;
    uint32_t server_capability_flags() const noexcept;

private:
    class Impl;
    Impl* impl_ = nullptr;
};

class PoolImpl;

class PoolConnection {
public:
    ~PoolConnection();

    PoolConnection(PoolConnection&&) noexcept;
    PoolConnection& operator=(PoolConnection&&) noexcept;

    PoolConnection(const PoolConnection&) = delete;
    PoolConnection& operator=(const PoolConnection&) = delete;

    Connection& get();
    const Connection& get() const;
    Connection* operator->();
    const Connection* operator->() const;
    Connection& operator*();
    const Connection& operator*() const;
    explicit operator bool() const noexcept;
    void release();

private:
    friend class PoolImpl;
    PoolConnection(std::shared_ptr<PoolImpl> pool, std::shared_ptr<Connection> connection);

    std::shared_ptr<PoolImpl> pool_;
    std::shared_ptr<Connection> connection_;
};

class Pool : public events::EventEmitterForwarder {
public:
    explicit Pool(PoolOptions options);
    ~Pool();

    Pool(Pool&&) noexcept;
    Pool& operator=(Pool&&) noexcept;

    Pool(const Pool&) = delete;
    Pool& operator=(const Pool&) = delete;

    PoolConnection get_connection();
    void get_connection(std::function<void(std::exception_ptr, std::shared_ptr<PoolConnection>)> callback);
    Promise<std::shared_ptr<PoolConnection>> get_connection_promise();
    void release_connection(PoolConnection& connection);
    QueryResult query(const std::string& sql);
    void query(const std::string& sql, QueryCallback callback);
    Promise<QueryResult> query_promise(const std::string& sql);
    std::vector<QueryResult> query_all(const std::string& sql);
    Promise<std::vector<QueryResult>> query_all_promise(const std::string& sql);
    QueryResult execute(const std::string& sql, const std::vector<Value>& values = {});
    void execute(const std::string& sql, const std::vector<Value>& values, QueryCallback callback);
    Promise<QueryResult> execute_promise(const std::string& sql, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values = {});
    Promise<std::vector<QueryResult>> execute_all_promise(const std::string& sql, const std::vector<Value>& values = {});
    void end();
    void end(VoidCallback callback);
    Promise<void> end_promise();

    std::size_t total_count() const noexcept;
    std::size_t idle_count() const noexcept;

private:
    std::shared_ptr<PoolImpl> impl_;
};

class PoolClusterImpl;

class PoolNamespace {
public:
    PoolNamespace();
    PoolConnection get_connection();
    QueryResult query(const std::string& sql);
    std::vector<QueryResult> query_all(const std::string& sql);
    QueryResult execute(const std::string& sql, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values = {});

private:
    friend class PoolCluster;
    PoolNamespace(std::shared_ptr<PoolClusterImpl> cluster, std::string pattern, PoolSelector selector);

    std::shared_ptr<PoolClusterImpl> cluster_;
    std::string pattern_ = "*";
    PoolSelector selector_ = PoolSelector::RoundRobin;
};

class PoolCluster : public events::EventEmitterForwarder {
public:
    explicit PoolCluster(PoolClusterOptions options = {});
    ~PoolCluster();

    PoolCluster(PoolCluster&&) noexcept;
    PoolCluster& operator=(PoolCluster&&) noexcept;

    PoolCluster(const PoolCluster&) = delete;
    PoolCluster& operator=(const PoolCluster&) = delete;

    void add(PoolOptions options);
    void add(std::string id, PoolOptions options);
    void remove(const std::string& pattern);
    PoolConnection get_connection(const std::string& pattern = "*");
    PoolConnection get_connection(const std::string& pattern, PoolSelector selector);
    PoolNamespace of(const std::string& pattern = "*", PoolSelector selector = PoolSelector::RoundRobin);
    QueryResult query(const std::string& sql);
    std::vector<QueryResult> query_all(const std::string& sql);
    QueryResult execute(const std::string& sql, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values = {});
    void end();
    std::size_t node_count() const noexcept;

private:
    std::shared_ptr<PoolClusterImpl> impl_;
};

Connection create_connection(ConnectionOptions options);
Connection create_connection(const std::string& uri);
Promise<std::shared_ptr<Connection>> create_connection_promise(ConnectionOptions options);
Pool create_pool(PoolOptions options);
Pool create_pool(const std::string& uri);
PoolCluster create_pool_cluster(PoolClusterOptions options = {});
QueryResult query(ConnectionOptions options, const std::string& sql);
Promise<QueryResult> query_promise(ConnectionOptions options, const std::string& sql);

namespace event {
inline constexpr events::TypedEvent<"connect", const ConnectionInfo&> Connect{};
inline constexpr events::TypedEvent<"error", const Error&> Error_{};
inline constexpr events::TypedEvent<"end"> End{};
inline constexpr events::TypedEvent<"close"> Close{};
inline constexpr events::TypedEvent<"enqueue"> Enqueue{};
inline constexpr events::TypedEvent<"connection", Connection&> ConnectionCreated{};
inline constexpr events::TypedEvent<"acquire", Connection&> Acquire{};
inline constexpr events::TypedEvent<"release", Connection&> Release{};
inline constexpr events::TypedEvent<"online", const std::string&> Online{};
inline constexpr events::TypedEvent<"offline", const std::string&> Offline{};
inline constexpr events::TypedEvent<"remove", const std::string&> Remove{};
inline constexpr events::TypedEvent<"warn", const Error&> Warn{};
}  // namespace event

namespace constants {
namespace column_type {
inline constexpr uint8_t DECIMAL = 0x00;
inline constexpr uint8_t TINY = 0x01;
inline constexpr uint8_t SHORT = 0x02;
inline constexpr uint8_t LONG = 0x03;
inline constexpr uint8_t FLOAT = 0x04;
inline constexpr uint8_t DOUBLE = 0x05;
inline constexpr uint8_t NULL_TYPE = 0x06;
inline constexpr uint8_t TIMESTAMP = 0x07;
inline constexpr uint8_t LONGLONG = 0x08;
inline constexpr uint8_t INT24 = 0x09;
inline constexpr uint8_t DATE = 0x0a;
inline constexpr uint8_t TIME = 0x0b;
inline constexpr uint8_t DATETIME = 0x0c;
inline constexpr uint8_t YEAR = 0x0d;
inline constexpr uint8_t NEWDATE = 0x0e;
inline constexpr uint8_t VARCHAR = 0x0f;
inline constexpr uint8_t BIT = 0x10;
inline constexpr uint8_t VECTOR = 0xf2;
inline constexpr uint8_t JSON = 0xf5;
inline constexpr uint8_t NEWDECIMAL = 0xf6;
inline constexpr uint8_t ENUM = 0xf7;
inline constexpr uint8_t SET = 0xf8;
inline constexpr uint8_t TINY_BLOB = 0xf9;
inline constexpr uint8_t MEDIUM_BLOB = 0xfa;
inline constexpr uint8_t LONG_BLOB = 0xfb;
inline constexpr uint8_t BLOB = 0xfc;
inline constexpr uint8_t VAR_STRING = 0xfd;
inline constexpr uint8_t STRING = 0xfe;
inline constexpr uint8_t GEOMETRY = 0xff;
}  // namespace column_type

namespace field_flags {
inline constexpr uint16_t NOT_NULL = 1;
inline constexpr uint16_t PRI_KEY = 2;
inline constexpr uint16_t UNIQUE_KEY = 4;
inline constexpr uint16_t MULTIPLE_KEY = 8;
inline constexpr uint16_t BLOB = 16;
inline constexpr uint16_t UNSIGNED = 32;
inline constexpr uint16_t ZEROFILL = 64;
inline constexpr uint16_t BINARY = 128;
inline constexpr uint16_t ENUM = 256;
inline constexpr uint16_t AUTO_INCREMENT = 512;
inline constexpr uint16_t TIMESTAMP = 1024;
inline constexpr uint16_t SET = 2048;
inline constexpr uint16_t NO_DEFAULT_VALUE = 4096;
inline constexpr uint16_t ON_UPDATE_NOW = 8192;
inline constexpr uint16_t NUM = 32768;
}  // namespace field_flags
}  // namespace constants

}  // namespace polycpp::mysql2

namespace polycpp::events {
template <>
struct ErrorEventOf<mysql2::Connection> {
    static constexpr auto value = mysql2::event::Error_;
};

template <>
struct ErrorEventOf<mysql2::Pool> {
    static constexpr auto value = mysql2::event::Error_;
};

template <>
struct ErrorEventOf<mysql2::PoolCluster> {
    static constexpr auto value = mysql2::event::Error_;
};
}  // namespace polycpp::events
