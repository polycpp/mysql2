# API Mapping

| Upstream symbol | C++ symbol | Status | Notes |
|---|---|---|---|
| `mysql.createConnection(options)` | `polycpp::mysql2::create_connection(ConnectionOptions)` | adapted | Creates and connects a typed C++ connection. |
| `mysql.connect(options)` | `polycpp::mysql2::create_connection(ConnectionOptions)` | adapted | Alias behavior is represented by the same factory. |
| `Connection#query(sql, callback)` | `Connection::query(const std::string&)` | adapted | Synchronous single-result return. If multiple result sets are returned, packets are drained and an error tells callers to use `query_all`. |
| multiple result callback shape | `Connection::query_all(const std::string&)` | adapted | Returns `std::vector<QueryResult>` for multi-statement and stored-procedure style results. |
| `Connection#execute(sql, values, callback)` | `Connection::execute(sql, values)` | adapted | One-shot prepare/execute/close with binary protocol parameters. |
| multi-result execute | `Connection::execute_all(...)` | adapted | Returns all result sets for prepared execution. |
| `Connection#prepare(sql, callback)` | `Connection::prepare(const std::string&)` | adapted | Returns `PreparedStatement` metadata. No LRU statement cache yet. |
| `PreparedStatementInfo#execute(values)` | `Connection::execute(statement, values)` | adapted | Explicit connection-owned execution. |
| `PreparedStatementInfo#close()` | `Connection::close_statement(statement)` | adapted | Sends `COM_STMT_CLOSE`. |
| `createPool`, `Pool` | `polycpp::mysql2::create_pool(PoolOptions)`, `Pool` | adapted | Synchronous RAII pool; no async queue/EventEmitter surface. |
| pool checkout/release | `Pool::get_connection()`, `PoolConnection` | adapted | Move-only RAII checkout handle releases on destruction. |
| `Connection#beginTransaction` | `Connection::begin_transaction()` | adapted | Executes `START TRANSACTION`. |
| `Connection#commit` | `Connection::commit()` | adapted | Executes `COMMIT`. |
| `Connection#rollback` | `Connection::rollback()` | adapted | Executes `ROLLBACK`. |
| `Connection#ping(callback)` | `Connection::ping()` | adapted | Synchronous OK packet return. |
| `Connection#reset(callback)` | `Connection::reset()` | adapted | Sends `COM_RESET_CONNECTION`. |
| `Connection#end()` | `Connection::end()` | adapted | Sends COM_QUIT when connected, then closes transport. |
| `ssl` option object | `ConnectionOptions::ssl` | adapted | Supports TLS enablement, CA/cert/key files or PEM, default trust store loading, certificate verification, and host/IP identity checks. Named AWS SSL profiles are not implemented. |
| `enableCleartextPlugin` | `ConnectionOptions::enable_cleartext_plugin` | adapted | Requires TLS before `mysql_clear_password` can be used. |
| `mysql.escape(value)` | `polycpp::mysql2::escape(Value)` | adapted | Uses C++ variant values; buffers become hex literals. |
| `mysql.escapeId(identifier)` | `polycpp::mysql2::escape_id(std::string)` | adapted | Preserves qualified identifier handling. |
| `mysql.format(sql, values)` | `polycpp::mysql2::format(sql, values)` | adapted | Supports `?` values and `??` identifiers. |
| `mysql.raw(sql)` | `polycpp::mysql2::raw(sql)` | adapted | Explicit raw SQL variant bypasses escaping. |
| `namedPlaceholders` option | `polycpp::mysql2::format_named(sql, map)` | adapted | Separate helper rather than connection option. |
| `ConnectionConfig` | `ConnectionOptions` | adapted | Typed subset of host, port, user, password, database, charset, auth key, SSL, and flags. |
| `PoolConfig` | `PoolOptions` | adapted | Connection options plus connection limit and wait timeout. |
| `FieldPacket` / `ColumnDefinition` | `Field` | adapted | Public immutable metadata struct. |
| text/binary row object | `Row` | adapted | Variant vector plus name lookup; no JS object prototype behavior. |
| `ResultSetHeader` / OK packet | `OkPacket` | adapted | Affected rows, insert id, status, warnings, info. |
| `Types`, `FieldFlags` constants | `polycpp::mysql2::constants::*` | direct | Protocol constants exposed for result interpretation. |
| `promise()` / `mysql2/promise` | none | omitted | Promise API is a JavaScript runtime abstraction. |
| stream rows | none | deferred | Requires polycpp stream boundary design. |
| compression option | none | deferred | Requires zlib packet wrapper and compressed sequence handling. |
| `createPoolCluster`, `PoolCluster` | none | deferred | Needs cluster node selection and failure policy. |
| server/binlog APIs | none | deferred | Not part of the client query/execute production slice. |
| parser cache controls | none | omitted | JavaScript parser-generation optimization is not needed in C++. |

## Framework Object Boundary Review

- Upstream reads or mutates framework/request/response/context objects: no HTTP or web framework objects are involved; upstream reads and writes MySQL connection, socket, command, packet, and config objects.
- Upstream fields or methods read: connection options, SSL options, server capability flags, auth plugin names, packet buffers, field metadata, and command state.
- Upstream fields or methods written: connection authorization state, packet sequence ids, command queues, auth plugin state, parser caches, result arrays, and pool queues.
- C++ adapter boundary: public API exposes `ConnectionOptions`, `SslOptions`, `Connection`, `PreparedStatement`, `PoolOptions`, `Pool`, `QueryResult`, `Field`, `Row`, and `Value`; packet sequencing and auth state remain private in `src/mysql2.cpp`.
- Partial mutation risk on validation failure: connection setup throws before marking the connection connected; unsupported auth and malformed packets fail closed. Single-result APIs drain unexpected additional result sets before throwing so the connection remains synchronized.

No polycpp HTTP request, response, or header type should be introduced for this package. The relevant ecosystem boundary is polycpp Buffer, crypto, IO, TLS, and companion charset decoding.
