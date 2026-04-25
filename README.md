# polycpp-mysql2

C++ companion port of [mysql2](https://www.npmjs.com/package/mysql2) for [polycpp](https://github.com/enricohuang/polycpp).

## Status

Port version: `0.1.0`

Initial port based on upstream version: `3.22.2`

Compatibility note:

- This repo does not imply full parity with upstream `mysql2`.
- The first API slice is a pure C++ MySQL text-protocol client, not a wrapper around a native MySQL client library.
- Implemented and deferred behavior is tracked in `docs/research.md`, `docs/api-mapping.md`, and `docs/divergences.md`.

Implemented:

- TCP connection using `polycpp::io::TcpSocket`.
- MySQL protocol v10 handshake.
- `mysql_native_password` authentication.
- `caching_sha2_password` fast auth and RSA public-key full auth path.
- `COM_QUERY` text protocol for result sets and OK packets.
- Text row decoding into C++ variants.
- SQL `escape`, `escape_id`, positional `format`, and named placeholder formatting helpers.
- Optional real MariaDB/MySQL e2e test controlled by `MYSQL2_TEST_*` environment variables.

Deferred:

- TLS transport, compression, pools, prepared statements, binary row parsing, server mode, replication/binlog APIs, promise-style JavaScript surface, and streaming APIs.
- Full charset table parity. The port reuses the existing `iconv-lite` companion for non-core decoding but currently maps only common MySQL charset ids.

Known divergences:

- C++ API shape is synchronous and typed; it is not an EventEmitter or Promise wrapper.
- Native MySQL/MariaDB client SDKs are intentionally not linked.

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

Use formatting helpers for values:

```cpp
auto sql = polycpp::mysql2::format(
    "SELECT * FROM ?? WHERE id = ? AND name = ?",
    {std::string("users"), int64_t{7}, std::string("Ada")});
```

## License

MIT
