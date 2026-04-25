#pragma once

#include <cstddef>
#include <cstdint>
#include <chrono>
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
    std::string profile;
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
    uint16_t charset_number = 224;  // UTF8MB4_UNICODE_CI, matching mysql2 default.
    std::string server_public_key_pem;
    bool enable_cleartext_plugin = false;
    bool compress = false;
    std::size_t max_prepared_statements = 16000;
    std::unordered_map<std::string, std::string> connect_attributes;
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

using Value = std::variant<std::monostate, bool, int64_t, uint64_t, double, std::string, Buffer, RawSql>;
using QueryAttributes = std::unordered_map<std::string, Value>;

enum class CursorType : uint8_t {
    None = 0,
    ReadOnly = 1,
    ForUpdate = 2,
    Scrollable = 3
};

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

struct CommandOptions {
    uint32_t timeout_ms = 0;
};

struct QueryOptions {
    std::string sql;
    QueryAttributes attributes;
    uint32_t timeout_ms = 0;
};

struct ExecuteOptions {
    std::string sql;
    std::vector<Value> values;
    QueryAttributes attributes;
    uint32_t timeout_ms = 0;
};

struct PreparedStatement {
    uint32_t id = 0;
    std::string query;
    std::vector<Field> parameters;
    std::vector<Field> columns;
};

struct StatementCursor {
    PreparedStatement statement;
    std::vector<Field> fields;
    uint16_t server_status = 0;

    bool open() const noexcept;
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

struct TraceEvent {
    std::string operation;
    std::string phase;
    std::string sql;
    std::string database;
    std::string user;
    std::string server_address;
    uint16_t server_port = 0;
    std::chrono::microseconds duration{0};
    std::string error;
};

struct RegisterSlaveOptions {
    uint32_t server_id = 0;
    std::string slave_hostname;
    std::string slave_user;
    std::string slave_password;
    uint16_t slave_port = 0;
    uint32_t replication_rank = 0;
    uint32_t master_id = 0;
};

struct BinlogDumpOptions {
    uint32_t binlog_position = 4;
    uint16_t flags = 0x01;
    uint32_t server_id = 0;
    std::string filename;
    std::size_t max_events = 1024;
};

struct BinlogEventHeader {
    uint32_t timestamp = 0;
    uint8_t event_type = 0;
    uint32_t server_id = 0;
    uint32_t event_size = 0;
    uint32_t log_position = 0;
    uint16_t flags = 0;
};

struct BinlogEvent {
    std::string name;
    BinlogEventHeader header;
    Buffer raw;
    Buffer status_vars;
    std::string schema;
    std::string query;
    std::string next_binlog;
    uint64_t next_position = 0;
    uint16_t binlog_version = 0;
    std::string server_version;
    uint32_t create_timestamp = 0;
    uint8_t event_header_length = 0;
    Buffer event_type_header_lengths;
    uint64_t xid = 0;
};

class RowStream {
public:
    RowStream();
    RowStream(std::vector<Field> fields, std::vector<Row> rows);

    bool empty() const noexcept;
    std::size_t size() const noexcept;
    const std::vector<Field>& fields() const noexcept;
    const std::vector<Row>& rows() const noexcept;
    std::optional<Row> read();
    std::vector<Row> to_vector() const;
    std::vector<Buffer> to_json_line_buffers() const;

private:
    std::vector<Field> fields_;
    std::vector<Row> rows_;
    std::size_t offset_ = 0;
};

class ServerConnection;

struct ServerAuthInfo {
    std::string user;
    std::string database;
    std::string address;
    uint16_t port = 0;
    uint32_t client_flags = 0;
    uint16_t charset_number = 0;
    std::string auth_plugin_name;
    Buffer auth_token;
    std::unordered_map<std::string, std::string> connect_attributes;
};

struct ServerStatementExecuteInfo {
    uint32_t statement_id = 0;
    uint8_t flags = 0;
    uint32_t iteration_count = 0;
    std::vector<Value> values;
    std::string query;
    Buffer raw_payload;
};

using ServerAuthCallback = std::function<std::optional<Error>(const ServerAuthInfo&)>;

struct ServerHandshakeOptions {
    uint8_t protocol_version = 10;
    std::string server_version = "polycpp-mysql2";
    uint32_t connection_id = 1;
    uint32_t capability_flags = 0;
    uint8_t character_set = 224;
    uint16_t status_flags = 2;
    std::string auth_plugin_name = "mysql_native_password";
    ServerAuthCallback auth_callback;
};

struct ServerOptions {
    std::string host = "127.0.0.1";
    uint16_t port = 0;
    int backlog = 128;
    bool auto_handshake = true;
    ServerHandshakeOptions handshake;
};

using VoidCallback = std::function<void(std::exception_ptr)>;
using OkCallback = std::function<void(std::exception_ptr, OkPacket)>;
using QueryCallback = std::function<void(std::exception_ptr, QueryResult)>;
using QueryAllCallback = std::function<void(std::exception_ptr, std::vector<QueryResult>)>;
using PrepareCallback = std::function<void(std::exception_ptr, PreparedStatement)>;
using BinlogEventsCallback = std::function<void(std::exception_ptr, std::vector<BinlogEvent>)>;

RawSql raw(std::string sql);
std::string escape(const Value& value);
std::string escape(std::nullptr_t);
std::string escape(const std::string& value);
std::string escape(const char* value);
std::string escape(bool value);
std::string escape(double value);
std::string escape(int64_t value);
std::string escape(uint64_t value);
std::string escape_id(const std::string& identifier, bool forbid_qualified = false);
std::string format(const std::string& sql, const std::vector<Value>& values);
std::string format_named(const std::string& sql, const std::unordered_map<std::string, Value>& values);
JsonValue value_to_json(const Value& value);
std::string row_to_json_line(const Row& row, const std::vector<Field>& fields);
ConnectionOptions parse_connection_uri(const std::string& uri);
uint16_t get_charset_number(const std::string& charset);
std::string get_charset_encoding(uint16_t charset_number);
std::vector<std::string> ssl_profile_names();
std::vector<std::string> ssl_profile_ca_pems(const std::string& profile);
void set_max_parser_cache(std::size_t max) noexcept;
std::size_t max_parser_cache() noexcept;
void clear_parser_cache() noexcept;
BinlogEvent parse_binlog_event_packet(const Buffer& payload);

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
    QueryResult query(const std::string& sql, const QueryAttributes& attributes);
    QueryResult query(const QueryOptions& options);
    void query(const std::string& sql, QueryCallback callback);
    void query(const std::string& sql, const QueryAttributes& attributes, QueryCallback callback);
    void query(const QueryOptions& options, QueryCallback callback);
    Promise<QueryResult> query_promise(const std::string& sql);
    Promise<QueryResult> query_promise(const std::string& sql, const QueryAttributes& attributes);
    Promise<QueryResult> query_promise(QueryOptions options);
    std::vector<QueryResult> query_all(const std::string& sql);
    std::vector<QueryResult> query_all(const std::string& sql, const QueryAttributes& attributes);
    std::vector<QueryResult> query_all(const QueryOptions& options);
    void query_all(const std::string& sql, QueryAllCallback callback);
    void query_all(const std::string& sql, const QueryAttributes& attributes, QueryAllCallback callback);
    void query_all(const QueryOptions& options, QueryAllCallback callback);
    Promise<std::vector<QueryResult>> query_all_promise(const std::string& sql);
    Promise<std::vector<QueryResult>> query_all_promise(const std::string& sql, const QueryAttributes& attributes);
    Promise<std::vector<QueryResult>> query_all_promise(QueryOptions options);
    RowStream query_stream(const std::string& sql);
    RowStream query_stream(const QueryOptions& options);
    stream::Readable query_stream_json(const std::string& sql);
    stream::Readable query_stream_json(const QueryOptions& options);
    PreparedStatement prepare(const std::string& sql);
    PreparedStatement prepare(const std::string& sql, CommandOptions options);
    void prepare(const std::string& sql, PrepareCallback callback);
    Promise<PreparedStatement> prepare_promise(const std::string& sql);
    QueryResult execute(const PreparedStatement& statement, const std::vector<Value>& values = {});
    QueryResult execute(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes);
    QueryResult execute(const PreparedStatement& statement,
                        const std::vector<Value>& values,
                        const QueryAttributes& attributes,
                        CommandOptions options);
    QueryResult execute(const ExecuteOptions& options);
    void execute(const PreparedStatement& statement, const std::vector<Value>& values, QueryCallback callback);
    void execute(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes, QueryCallback callback);
    void execute(const ExecuteOptions& options, QueryCallback callback);
    Promise<QueryResult> execute_promise(const PreparedStatement& statement, const std::vector<Value>& values = {});
    Promise<QueryResult> execute_promise(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes);
    Promise<QueryResult> execute_promise(ExecuteOptions options);
    std::vector<QueryResult> execute_all(const PreparedStatement& statement, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes);
    std::vector<QueryResult> execute_all(const PreparedStatement& statement,
                                         const std::vector<Value>& values,
                                         const QueryAttributes& attributes,
                                         CommandOptions options);
    std::vector<QueryResult> execute_all(const ExecuteOptions& options);
    void execute_all(const PreparedStatement& statement, const std::vector<Value>& values, QueryAllCallback callback);
    void execute_all(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes, QueryAllCallback callback);
    void execute_all(const ExecuteOptions& options, QueryAllCallback callback);
    Promise<std::vector<QueryResult>> execute_all_promise(const PreparedStatement& statement, const std::vector<Value>& values = {});
    Promise<std::vector<QueryResult>> execute_all_promise(const PreparedStatement& statement, const std::vector<Value>& values, const QueryAttributes& attributes);
    Promise<std::vector<QueryResult>> execute_all_promise(ExecuteOptions options);
    QueryResult execute(const std::string& sql, const std::vector<Value>& values = {});
    QueryResult execute(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes);
    void execute(const std::string& sql, const std::vector<Value>& values, QueryCallback callback);
    void execute(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes, QueryCallback callback);
    Promise<QueryResult> execute_promise(const std::string& sql, const std::vector<Value>& values = {});
    Promise<QueryResult> execute_promise(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes);
    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values = {});
    std::vector<QueryResult> execute_all(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes);
    void execute_all(const std::string& sql, const std::vector<Value>& values, QueryAllCallback callback);
    void execute_all(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes, QueryAllCallback callback);
    Promise<std::vector<QueryResult>> execute_all_promise(const std::string& sql, const std::vector<Value>& values = {});
    Promise<std::vector<QueryResult>> execute_all_promise(const std::string& sql, const std::vector<Value>& values, const QueryAttributes& attributes);
    StatementCursor execute_cursor(const PreparedStatement& statement,
                                   const std::vector<Value>& values = {},
                                   const QueryAttributes& attributes = {},
                                   CursorType cursor_type = CursorType::ReadOnly);
    StatementCursor execute_cursor(const std::string& sql,
                                   const std::vector<Value>& values = {},
                                   const QueryAttributes& attributes = {},
                                   CursorType cursor_type = CursorType::ReadOnly);
    QueryResult fetch(StatementCursor& cursor, uint32_t row_count);
    QueryResult fetch(StatementCursor& cursor, uint32_t row_count, CommandOptions options);
    OkPacket register_slave(const RegisterSlaveOptions& options);
    void register_slave(const RegisterSlaveOptions& options, OkCallback callback);
    Promise<OkPacket> register_slave_promise(RegisterSlaveOptions options);
    std::vector<BinlogEvent> binlog_dump(const BinlogDumpOptions& options = {});
    void binlog_dump(const BinlogDumpOptions& options, BinlogEventsCallback callback);
    Promise<std::vector<BinlogEvent>> binlog_dump_promise(BinlogDumpOptions options = {});
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

class ServerConnection : public events::EventEmitterForwarder {
public:
    ~ServerConnection();

    ServerConnection(ServerConnection&&) noexcept;
    ServerConnection& operator=(ServerConnection&&) noexcept;

    ServerConnection(const ServerConnection&) = delete;
    ServerConnection& operator=(const ServerConnection&) = delete;

    void server_handshake(ServerHandshakeOptions options = {});
    void write_ok(OkPacket ok = {});
    void write_error(uint16_t code, const std::string& sql_state, const std::string& message);
    void write_error(const Error& error);
    void write_text_result(const QueryResult& result);
    void write_text_result(const std::vector<Row>& rows, const std::vector<Field>& fields);
    void close() noexcept;

    bool connected() const noexcept;
    const ServerAuthInfo& auth_info() const noexcept;
    std::string remote_address() const;
    uint16_t remote_port() const;

private:
    friend class ServerImpl;
    class Impl;
    explicit ServerConnection(std::shared_ptr<Impl> impl);

    std::shared_ptr<Impl> impl_;
};

class ServerImpl;

class Server : public events::EventEmitterForwarder {
public:
    explicit Server(ServerOptions options = {});
    ~Server();

    Server(Server&&) noexcept;
    Server& operator=(Server&&) noexcept;

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    void listen();
    void listen(uint16_t port);
    void listen(uint16_t port, const std::string& host);
    void close();

    bool listening() const noexcept;
    std::string address() const;
    uint16_t port() const noexcept;
    std::size_t connection_count() const noexcept;

private:
    std::shared_ptr<ServerImpl> impl_;
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
Server create_server(ServerOptions options = {});
QueryResult query(ConnectionOptions options, const std::string& sql);
Promise<QueryResult> query_promise(ConnectionOptions options, const std::string& sql);

namespace event {
inline constexpr events::TypedEvent<"connect", const ConnectionInfo&> Connect{};
inline constexpr events::TypedEvent<"error", const Error&> Error_{};
inline constexpr events::TypedEvent<"trace", const TraceEvent&> Trace{};
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
inline constexpr events::TypedEvent<"connection", ServerConnection&> ServerConnectionAccepted{};
inline constexpr events::TypedEvent<"query", ServerConnection&, const std::string&> ServerQuery{};
inline constexpr events::TypedEvent<"ping", ServerConnection&> ServerPing{};
inline constexpr events::TypedEvent<"quit", ServerConnection&> ServerQuit{};
inline constexpr events::TypedEvent<"init_db", ServerConnection&, const std::string&> ServerInitDb{};
inline constexpr events::TypedEvent<"field_list", ServerConnection&, const std::string&, const std::string&> ServerFieldList{};
inline constexpr events::TypedEvent<"stmt_prepare", ServerConnection&, const std::string&> ServerStatementPrepare{};
inline constexpr events::TypedEvent<"stmt_execute", ServerConnection&, const ServerStatementExecuteInfo&> ServerStatementExecute{};
inline constexpr events::TypedEvent<"packet", ServerConnection&, const Buffer&, bool, uint8_t> ServerPacket{};
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

namespace binlog_dump_flags {
inline constexpr uint16_t NON_BLOCK = 0x01;
}  // namespace binlog_dump_flags
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

template <>
struct ErrorEventOf<mysql2::Server> {
    static constexpr auto value = mysql2::event::Error_;
};

template <>
struct ErrorEventOf<mysql2::ServerConnection> {
    static constexpr auto value = mysql2::event::Error_;
};
}  // namespace polycpp::events
