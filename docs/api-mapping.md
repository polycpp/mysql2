# API Mapping

| Upstream symbol | C++ symbol | Status | Notes |
|---|---|---|---|
| `mysql.createConnection(options)` | `polycpp::mysql2::create_connection(ConnectionOptions)` | adapted | Creates and connects a typed C++ connection. |
| `mysql.connect(options)` | `polycpp::mysql2::create_connection(ConnectionOptions)` | adapted | Alias behavior is represented by the same factory. |
| `Connection#query(sql, callback)` | `Connection::query(const std::string&)` | adapted | Synchronous result return instead of callback/EventEmitter. |
| `Connection#ping(callback)` | `Connection::ping()` | adapted | Synchronous OK packet return. |
| `Connection#end()` | `Connection::end()` | adapted | Sends COM_QUIT when connected, then closes TCP socket. |
| `mysql.escape(value)` | `polycpp::mysql2::escape(Value)` | adapted | Uses C++ variant values; buffers become hex literals. |
| `mysql.escapeId(identifier)` | `polycpp::mysql2::escape_id(std::string)` | adapted | Preserves qualified identifier handling. |
| `mysql.format(sql, values)` | `polycpp::mysql2::format(sql, values)` | adapted | Supports `?` values and `??` identifiers. |
| `mysql.raw(sql)` | `polycpp::mysql2::raw(sql)` | adapted | Explicit raw SQL variant bypasses escaping. |
| `namedPlaceholders` option | `polycpp::mysql2::format_named(sql, map)` | adapted | Separate helper rather than connection option. |
| `ConnectionConfig` | `ConnectionOptions` | adapted | Typed subset of host, port, user, password, database, charset, auth key, and flags. |
| `FieldPacket` / `ColumnDefinition` | `Field` | adapted | Public immutable metadata struct. |
| text row object | `Row` | adapted | Variant vector plus name lookup; no JS object prototype behavior. |
| `ResultSetHeader` / OK packet | `OkPacket` | adapted | Affected rows, insert id, status, warnings, info. |
| `Types`, `FieldFlags` constants | `polycpp::mysql2::constants::*` | direct | Protocol constants exposed for result interpretation. |
| `createPool`, `Pool`, `PoolCluster` | none | deferred | Requires async command queue and lifecycle policy. |
| `prepare`, `execute`, statement cache | none | deferred | Requires binary protocol and LRU cache. |
| `promise()` / `mysql2/promise` | none | omitted | Promise API is a JavaScript runtime abstraction. |
| stream rows | none | deferred | Requires polycpp stream boundary design. |
| SSL options and profiles | none | deferred | Requires TLS stream upgrade and profile loading. |
| compression option | none | deferred | Requires zlib packet wrapper. |
| server/binlog APIs | none | deferred | Not part of first client query slice. |

## Framework object boundary review

- Upstream reads or mutates framework/request/response/context objects: no HTTP or web framework objects are involved; upstream reads and writes MySQL connection, socket, command, packet, and config objects.
- Upstream fields or methods read: connection options, server capability flags, auth plugin names, packet buffers, field metadata, and command state.
- Upstream fields or methods written: connection authorization state, packet sequence ids, command queues, auth plugin state, parser caches, and result arrays.
- C++ adapter boundary: public API exposes `ConnectionOptions`, `Connection`, `QueryResult`, `Field`, `Row`, and `Value`; packet sequencing and auth state remain private in `src/mysql2.cpp`.
- Partial mutation risk on validation failure: connection setup throws before marking the connection connected; unsupported auth and malformed packets fail closed. Query errors throw before returning a partial `QueryResult`.

No polycpp HTTP request, response, or header type should be introduced for this package. The relevant ecosystem boundary is polycpp Buffer, crypto, IO, and companion charset decoding.
