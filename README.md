# polycpp-mysql2

C++ companion port of [mysql2](https://www.npmjs.com/package/mysql2) for [polycpp](https://github.com/enricohuang/polycpp).

## Status

Port version: `0.1.0`

Initial port based on upstream version: `3.22.2`

Compatibility note:

- This repo does not imply full parity with upstream `mysql2`.
- The implementation is a pure C++ MySQL/MariaDB wire-protocol client, not a wrapper around a native MySQL client library.
- Implemented behavior, intentional C++ adaptations, and unsupported runtime-only behavior are tracked in `docs/research.md`, `docs/api-mapping.md`, and `docs/divergences.md`.

Implemented:

- TCP connection using `polycpp::io::TcpSocket`.
- Unix socket path connection using `ConnectionOptions::socket_path` and `polycpp::io::PipeSocket`.
- Optional TLS transport upgrade using `polycpp::io::TlsStream` after MySQL SSLRequest.
- MySQL protocol v10 handshake.
- `mysql_native_password`, `caching_sha2_password`, `sha256_password`, and TLS-or-socket-path-gated `mysql_clear_password` auth behavior.
- `COM_QUERY` text protocol for result sets and OK packets.
- Prepared statements using `COM_STMT_PREPARE`, `COM_STMT_EXECUTE`, binary rows, and `COM_STMT_CLOSE`.
- Prepared-statement execution cache with explicit `close_statement(sql)` / `close_statement(statement)` invalidation.
- Query attributes for `COM_QUERY` and `COM_STMT_EXECUTE` when the server advertises `CLIENT_QUERY_ATTRIBUTES`.
- Server-side prepared-statement cursors through `execute_cursor(...)` and `fetch(...)`.
- Explicit multi-result APIs with `query_all` and `execute_all`; single-result APIs drain and throw if multiple result sets are returned.
- Text and binary row decoding into C++ variants, including binary string/blob preservation as `polycpp::Buffer`.
- MySQL charset/collation id mapping with non-core string conversion delegated to the existing `iconv-lite` companion.
- SQL `escape`, `escape_id`, positional `format`, and named placeholder formatting helpers.
- Connection URI parsing through `polycpp::url`.
- Connect timeout and per-command inactivity timeout enforcement through `polycpp::io::Timer`.
- Connection attributes in the initial handshake and `COM_CHANGE_USER`.
- `QueryOptions` / `ExecuteOptions` timeout wrappers, callback overloads, `polycpp::Promise` wrappers, typed `polycpp::events::EventEmitter` integration, trace events, pull-based `RowStream` row streams over `polycpp::stream::Readable<Row>`, lazy JSON line `polycpp::stream::Readable<Buffer>` query output, and scan-oriented `query_each_raw(...)` packet byte views.
- MySQL compressed protocol using `polycpp::zlib`.
- Explicit-policy LOCAL INFILE uploads through `ConnectionOptions::local_infile_handler`.
- `COM_CHANGE_USER`, transaction helpers, ping, reset, graceful end, synchronous RAII pools, and pool clusters.
- AWS RDS TLS profile CA data from `aws-ssl-profiles@1.1.2` via `SslOptions::profile = "Amazon RDS"`.
- Parser-cache compatibility controls as no-op/static-parser audit hooks.
- Bounded `COM_REGISTER_SLAVE`, `COM_BINLOG_DUMP`, and `COM_BINLOG_DUMP_GTID` support with CRC32 binlog-checksum negotiation, typed parsing for Query, Rotate, FormatDescription, Xid, GTID, PreviousGTIDs, TableMap, and common row events, typed TIME2/DATETIME2/TIMESTAMP2 row values, plus raw payload retention for audit and unsupported event types.
- `BinlogStream`, a pull-based `polycpp::stream::Readable<BinlogEvent>` returned by `Connection::create_binlog_stream(...)`; callers consume events through `polycpp::stream::event::Data`.
- `BinlogParser` for stateful table-map-aware row decoding and `Connection::binlog_dump_each(...)` for callback-controlled replication reads without accumulating an unbounded vector.
- Adapted server protocol mode with `create_server`, `Server`, `ServerConnection`, TCP or Unix socket listening, optional MySQL in-protocol TLS upgrade, server-side protocol handshake/auth callback, typed query/ping/quit/init-db/field-list/statement command events, statement-prepare OK writers, and OK/ERR/text/binary result response writers.
- Optional real MariaDB/MySQL e2e tests controlled by `MYSQL2_TEST_*` environment variables.

Known divergences:

- C++ API shape is synchronous and typed first; callback and Promise wrappers execute the same typed operations and settle through polycpp primitives.
- Native MySQL/MariaDB client SDKs are intentionally not linked.
- Node diagnostic channels are adapted to typed `event::Trace` events.
- Node object-mode row streaming maps to `RowStream`, a `polycpp::stream::Readable<Row>` subclass. `query_stream(...)` reads result metadata up front and decodes row packets on `read()`/flowing consumption; the connection stays busy until the stream reaches EOF or is destroyed/dropped. `query_stream_json()` is a lazy newline-delimited JSON `Readable<Buffer>` adapter over the same row stream.
- `query_each_raw(...)` is a C++-specific scan extension. `RawRowView` / `RawValueView` bytes are packet-backed `std::string_view` values and are valid only during the callback.
- Server mode is adapted to a C++ server object. It supports TCP and Unix socket listening, MySQL in-protocol TLS upgrade when configured, handshake/auth inspection, command dispatch, packet observation, statement prepare OK packets, and OK/ERR/text/binary result writers; a full SQL engine is intentionally not implied.
- Parser cache controls are compatibility no-ops because C++ uses static parsers.
- Query attributes use `std::unordered_map`, so attribute wire order is not a public contract.
- Binlog streams use typed C++ `BinlogStream` chunks instead of JavaScript object prototypes. A live `create_binlog_stream(...)` reserves the connection until EOF, `max_events`, or destroy/drop cleanup. Every terminal replication-stream path closes the transport because a MySQL replication command stream is not safely reusable as a normal command channel; the next command reconnects through the configured options.
- Bounded and callback-controlled binlog dump reads close the transport when EOF, `max_events`, or callback stop ends the replication command stream. Use `create_binlog_stream(...)` or `binlog_dump_each(...)` when the caller wants stream-shaped or callback-controlled continuous consumption.

## Prerequisites

- C++20 compiler
- CMake 3.20+
- A local `polycpp` checkout or network access for FetchContent
- Optional: MariaDB/MySQL server for e2e tests

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DPOLYCPP_MYSQL2_BUILD_TESTS=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
```

Additional hardening and e2e suites live under `tests/e2e/`. See
`tests/e2e/README.md` for environment variables and reproducible Docker
commands.

Run the real database e2e test:

```bash
MYSQL2_TEST_HOST=127.0.0.1 \
MYSQL2_TEST_PORT=3306 \
MYSQL2_TEST_USER=root \
MYSQL2_TEST_PASSWORD=secret \
MYSQL2_TEST_DATABASE=test \
ctest --test-dir build --output-on-failure -R mysql2_e2e_real_database
```

Run the replication/binlog e2e path against a server started with binary logging and row format:

```bash
MYSQL2_TEST_REPLICATION=1 \
MYSQL2_TEST_HOST=127.0.0.1 \
MYSQL2_TEST_PORT=3306 \
MYSQL2_TEST_USER=root \
MYSQL2_TEST_PASSWORD=secret \
MYSQL2_TEST_DATABASE=test \
ctest --test-dir build --output-on-failure -R mysql2_e2e_replication
```

Run the TLS e2e path:

```bash
MYSQL2_TEST_HOST=127.0.0.1 \
MYSQL2_TEST_PORT=3306 \
MYSQL2_TEST_USER=root \
MYSQL2_TEST_PASSWORD=secret \
MYSQL2_TEST_DATABASE=test \
MYSQL2_TEST_SSL=1 \
MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED=1 \
MYSQL2_TEST_SSL_VERIFY_IDENTITY=1 \
MYSQL2_TEST_SSL_CA_FILE=/path/to/ca.pem \
ctest --test-dir build --output-on-failure
```

Run loopback server protocol hardening, including ERR packet handling:

```bash
ctest --test-dir build --output-on-failure -R mysql2_e2e_server_protocol
```

Optional benchmarks are available under `benchmarks/`. The native MySQL C API
comparison is opt-in and is linked only into the benchmark executable, never
into `polycpp_mysql2`.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DPOLYCPP_MYSQL2_BUILD_BENCHMARKS=ON \
  -DPOLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=OFF
cmake --build build -j$(nproc) --target bench_mysql2
```

## Usage

```cpp
#include <iostream>
#include <polycpp/mysql2/mysql2.hpp>

int main() {
    polycpp::mysql2::ConnectionOptions options;
    options.host = "127.0.0.1";
    options.port = 3306;
    options.user = "root";
    options.password = "secret";
    options.database = "app";

    auto conn = polycpp::mysql2::create_connection(options);
    auto result = conn.query("SELECT 1 AS one, 'two' AS label");

    std::cout << std::get<int64_t>(result.rows[0].at("one")) << "\n";
    std::cout << std::get<std::string>(result.rows[0].at("label")) << "\n";
}
```

Prepared statement:

```cpp
auto stmt = conn.prepare("SELECT id, name FROM users WHERE id > ?");
auto result = conn.execute(stmt, {int64_t{10}});
conn.close_statement(stmt);
```

Query attributes and cursors:

```cpp
auto traced = conn.query(
    "SELECT 1 AS one",
    {{"trace_id", std::string("audit-123")}, {"sampled", true}});

auto cursor = conn.execute_cursor("SELECT id FROM users ORDER BY id");
while (cursor.open()) {
    auto batch = conn.fetch(cursor, 100);
    for (const auto& row : batch.rows) {
        (void)row;
    }
}
conn.close_statement(cursor.statement);
```

Bounded binlog dump:

```cpp
polycpp::mysql2::BinlogDumpOptions dump;
dump.flags = polycpp::mysql2::constants::binlog_dump_flags::NON_BLOCK;
dump.server_id = 12345;
dump.max_events = 1000;

auto events = conn.binlog_dump(dump);
for (const auto& event : events) {
    if (event.name == "QueryEvent") {
        (void)event.query;
    }
}
```

Callback-controlled binlog read with table-map-aware parsing:

```cpp
polycpp::mysql2::BinlogDumpOptions stream_dump;
stream_dump.flags = 0;
stream_dump.max_events = 0;
stream_dump.server_id = 12345;

conn.binlog_dump_each(stream_dump, [](const polycpp::mysql2::BinlogEvent& event) {
    if (event.name == "WriteRowsEventV2") {
        for (const auto& change : event.row_changes) {
            (void)change.after;
        }
    }
    return true; // return false to close the replication command stream
});
```

Typed binlog stream:

```cpp
polycpp::mysql2::BinlogDumpOptions stream_options;
stream_options.flags = 0;
stream_options.max_events = 0;
stream_options.server_id = 12345;

auto binlog = conn.create_binlog_stream(stream_options);
binlog.on(polycpp::stream::event::Data, [](const polycpp::mysql2::BinlogEvent& event) {
    if (event.name == "RotateEvent") {
        (void)event.next_binlog;
    }
});
binlog.on(polycpp::stream::event::End, [] {});
binlog.on(polycpp::stream::event::Error_, [](const polycpp::Error& error) {
    (void)error;
});
```

Adapted server mode:

```cpp
polycpp::mysql2::ServerOptions server_options;
server_options.handshake.server_version = "polycpp-mysql2";

auto server = polycpp::mysql2::create_server(server_options);
server.on(polycpp::mysql2::event::ServerConnectionAccepted,
    [](polycpp::mysql2::ServerConnection& connection) {
        connection.on(polycpp::mysql2::event::ServerQuery,
            [](polycpp::mysql2::ServerConnection& conn, const std::string& sql) {
                (void)sql;

                polycpp::mysql2::Field field;
                field.name = "one";
                field.column_type = polycpp::mysql2::constants::column_type::LONG;

                polycpp::mysql2::Row row;
                row.values = {int64_t{1}};

                conn.write_text_result({row}, {field});
            });
    });
server.listen(3307);
```

TLS:

```cpp
polycpp::mysql2::ConnectionOptions options;
options.host = "db.example.com";
options.user = "app";
options.password = "secret";
options.ssl.enabled = true;
options.ssl.ca_file = "/etc/ssl/certs/db-ca.pem";

auto conn = polycpp::mysql2::create_connection(options);
```

AWS RDS TLS profile:

```cpp
polycpp::mysql2::ConnectionOptions options;
options.host = "db.example.com";
options.ssl.enabled = true;
options.ssl.profile = "Amazon RDS";
```

Pool:

```cpp
polycpp::mysql2::PoolOptions pool_options;
pool_options.connection = options;
pool_options.connection_limit = 10;

auto pool = polycpp::mysql2::create_pool(pool_options);
auto result = pool.query("SELECT 1 AS one");
```

Callback, Promise, event, and stream adapters:

```cpp
conn.on(polycpp::mysql2::event::Connect, [](const polycpp::mysql2::ConnectionInfo&) {});
conn.on(polycpp::mysql2::event::Trace, [](const polycpp::mysql2::TraceEvent& event) {
    (void)event;
});

conn.query("SELECT 1 AS one", [](std::exception_ptr err, polycpp::mysql2::QueryResult result) {
    if (err) return;
    (void)result;
});

auto promise = conn.query_promise("SELECT 1 AS one");
polycpp::mysql2::QueryOptions query_options;
query_options.sql = "SELECT id, name FROM users";
query_options.timeout_ms = 5000;

auto rows = conn.query_stream(query_options);
while (auto row = rows.read()) {
    (void)*row;
}
auto stream = conn.query_stream_json(query_options);

conn.query_each_raw(query_options, [](const polycpp::mysql2::RawRowView& row) {
    if (!row.at(0).is_null) {
        auto bytes = row.at(0).bytes; // valid only during this callback
        (void)bytes;
    }
});
```

Use `query_each_raw(...)` only for one-pass scans where packet-backed bytes are
enough. Copy `RawValueView::bytes` before storing it beyond the callback.

Formatting helpers:

```cpp
auto sql = polycpp::mysql2::format(
    "SELECT * FROM ?? WHERE id = ? AND name = ?",
    {std::string("users"), int64_t{7}, std::string("Ada")});
```

## License

MIT
