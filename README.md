# polycpp-mysql2

C++ companion port of [mysql2](https://www.npmjs.com/package/mysql2) for [polycpp](https://github.com/enricohuang/polycpp).

## Status

Port version: `0.1.0`

Initial port based on upstream version: `3.22.2`

Compatibility note:

- This repo does not imply full parity with upstream `mysql2`.
- The implementation is a pure C++ MySQL/MariaDB wire-protocol client, not a wrapper around a native MySQL client library.
- Implemented and deferred behavior is tracked in `docs/research.md`, `docs/api-mapping.md`, and `docs/divergences.md`.

Implemented:

- TCP connection using `polycpp::io::TcpSocket`.
- Optional TLS transport upgrade using `polycpp::io::TlsStream` after MySQL SSLRequest.
- MySQL protocol v10 handshake.
- `mysql_native_password`, `caching_sha2_password`, `sha256_password`, and TLS-gated `mysql_clear_password` auth behavior.
- `COM_QUERY` text protocol for result sets and OK packets.
- Prepared statements using `COM_STMT_PREPARE`, `COM_STMT_EXECUTE`, binary rows, and `COM_STMT_CLOSE`.
- Explicit multi-result APIs with `query_all` and `execute_all`; single-result APIs drain and throw if multiple result sets are returned.
- Text and binary row decoding into C++ variants, including binary string/blob preservation as `polycpp::Buffer`.
- SQL `escape`, `escape_id`, positional `format`, and named placeholder formatting helpers.
- Transaction helpers, ping, reset, graceful end, and a synchronous RAII connection pool.
- Optional real MariaDB/MySQL e2e tests controlled by `MYSQL2_TEST_*` environment variables.

Deferred:

- Compression, server mode, replication/binlog APIs, promise-style JavaScript surface, EventEmitter/callback API, and streaming row APIs.
- Full charset table parity. The port reuses the existing `iconv-lite` companion for non-core decoding but currently maps only common MySQL charset ids.
- Prepared statement LRU cache and named TLS profile data from `aws-ssl-profiles`.
- LOCAL INFILE until an explicit file access callback policy exists.

Known divergences:

- C++ API shape is synchronous and typed; it is not an EventEmitter or Promise wrapper.
- Native MySQL/MariaDB client SDKs are intentionally not linked.
- Connection URI parsing is not implemented; callers fill `ConnectionOptions` directly.

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

Run the real database e2e test:

```bash
MYSQL2_TEST_HOST=127.0.0.1 \
MYSQL2_TEST_PORT=3306 \
MYSQL2_TEST_USER=root \
MYSQL2_TEST_PASSWORD=secret \
MYSQL2_TEST_DATABASE=test \
ctest --test-dir build --output-on-failure
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

Pool:

```cpp
polycpp::mysql2::PoolOptions pool_options;
pool_options.connection = options;
pool_options.connection_limit = 10;

auto pool = polycpp::mysql2::create_pool(pool_options);
auto result = pool.query("SELECT 1 AS one");
```

Formatting helpers:

```cpp
auto sql = polycpp::mysql2::format(
    "SELECT * FROM ?? WHERE id = ? AND name = ?",
    {std::string("users"), int64_t{7}, std::string("Ada")});
```

## License

MIT
