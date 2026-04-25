# Test Plan

## Unit Tests

- SQL string escaping for quotes, backslashes, control bytes, and NULL.
- Buffer escaping to hex literals.
- Identifier escaping for qualified and literal identifiers.
- Positional formatting for values and identifiers.
- Named placeholder formatting, including missing value failure.
- Row lookup by index and field name.
- Auth token fixtures for mysql_native_password and caching_sha2_password should be added from upstream-compatible vectors.
- Packet cursor fixtures for length-coded integers and malformed packet failures should be added before expanding protocol scope.
- Prepared-statement parameter encoding fixtures should cover NULL, signed/unsigned integers, double, string, and Buffer.
- Binary row parser fixtures should cover numeric, decimal, date/time/datetime, binary string/blob, JSON text, and NULL bitmap behavior.

## Integration Tests

- Environment-driven MariaDB/MySQL test using `MYSQL2_TEST_HOST`, `MYSQL2_TEST_PORT`, `MYSQL2_TEST_USER`, `MYSQL2_TEST_PASSWORD`, and `MYSQL2_TEST_DATABASE`.
- Current e2e coverage connects to MariaDB 10.6, runs ping, selects scalar values, preserves empty binary values as Buffer, creates a temporary table, inserts rows, selects them back, uses prepared statements, tests transactions, resets the connection, tests multi-result queries, and exercises the RAII pool.
- TLS e2e is controlled by `MYSQL2_TEST_SSL`, `MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED`, `MYSQL2_TEST_SSL_VERIFY_IDENTITY`, and `MYSQL2_TEST_SSL_CA_FILE`.
- MySQL 8 coverage runs against a Dockerized MySQL 8.4 server and validates the default `caching_sha2_password` path.
- Add a dedicated MySQL 8 TLS clear password auth path test if a server/user is configured to require it.
- Add server ERR packet coverage with invalid SQL and access denied scenarios.
- Add charset coverage for utf8mb4, latin1, binary, and representative non-UTF encodings through `iconv-lite`.
- Add pool contention and wait-timeout coverage.
- Add multi-result stored procedure coverage.

## Compatibility Tests Adapted From Upstream

- Adapt upstream SQL escaping and formatting fixtures.
- Adapt upstream auth plugin unit tests.
- Adapt upstream packet parser tests for length-coded numbers, OK packets, ERR packets, column definitions, text rows, and binary rows.
- Adapt upstream integration tests for simple query, insert/update, transactions, errors, charset behavior, prepared statements, TLS, and pooling.
- Defer upstream compression and stream fixtures until those features are implemented.

## Security and Fail-Closed Tests

- Unsupported auth plugin returns an error.
- `mysql_clear_password` without TLS returns an error.
- TLS certificate chain failure returns an error when `reject_unauthorized` is true.
- TLS host/IP mismatch returns an error when `verify_identity` is true.
- LOCAL INFILE request returns an error.
- Malformed length-coded packet returns an error rather than reading out of bounds.
- Missing named placeholder throws.
- Non-finite floating values escape as `NULL`.
- RSA auth path uses OAEP SHA1 to match upstream caching_sha2_password behavior.
- Single-result APIs drain additional result sets before throwing so the connection is reusable.

## Release-Blocking Behaviors

- Build and unit tests pass on a clean checkout.
- Real MariaDB e2e passes without TLS.
- Real MariaDB e2e passes with verified TLS.
- Real MySQL 8 e2e passes before claiming MySQL 8 auth parity.
- Compression and stream deferrals remain documented if not implemented.
- Third-party license notices are complete.
- Documentation builds with `python3 docs/build.py`.
- GitHub repo remains private until production-grade quality and public docs are ready.

## Current validation

Commands run on April 25, 2026:

```bash
cmake --build build -j2
ctest --test-dir build --output-on-failure
MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=43306 MYSQL2_TEST_USER=root MYSQL2_TEST_DATABASE=polycpp_mysql2_test ctest --test-dir build --output-on-failure
MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=43306 MYSQL2_TEST_USER=root MYSQL2_TEST_DATABASE=polycpp_mysql2_test MYSQL2_TEST_SSL=1 MYSQL2_TEST_SSL_REJECT_UNAUTHORIZED=1 MYSQL2_TEST_SSL_VERIFY_IDENTITY=1 MYSQL2_TEST_SSL_CA_FILE=$PWD/build/mariadb-tls/ca.pem ctest --test-dir build --output-on-failure
docker run --rm --network container:polycpp-mysql2-mysql8 -v /data/work/lib/mysql2:/work -w /work ubuntu:22.04 bash -lc 'MYSQL2_TEST_HOST=127.0.0.1 MYSQL2_TEST_PORT=43307 MYSQL2_TEST_USER=root MYSQL2_TEST_PASSWORD=secret MYSQL2_TEST_DATABASE=polycpp_mysql2_test ./build/test_smoke --gtest_filter=mysql2_integration.query_against_real_database_when_configured'
python3 docs/build.py
```

Service versions used for e2e validation:

- MariaDB 10.6 on `127.0.0.1:43306` with a generated test CA/server certificate for verified TLS.
- MySQL Community Server 8.4.6 in Docker, tested from an Ubuntu 22.04 helper container sharing the MySQL container network namespace.
