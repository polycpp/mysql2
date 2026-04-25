# Test Plan

## Unit tests

- SQL string escaping for quotes, backslashes, control bytes, and NULL.
- Buffer escaping to hex literals.
- Identifier escaping for qualified and literal identifiers.
- Positional formatting for values and identifiers.
- Named placeholder formatting, including missing value failure.
- Row lookup by index and field name.
- Auth token fixtures for mysql_native_password and caching_sha2_password should be added from upstream-compatible vectors.
- Packet cursor fixtures for length-coded integers and malformed packet failures should be added before expanding protocol scope.

## Integration tests

- Environment-driven MariaDB/MySQL test using `MYSQL2_TEST_HOST`, `MYSQL2_TEST_PORT`, `MYSQL2_TEST_USER`, `MYSQL2_TEST_PASSWORD`, and `MYSQL2_TEST_DATABASE`.
- Current e2e coverage connects to MariaDB 10.6, runs ping, selects scalar values, creates a temporary table, inserts rows, and selects them back.
- Add MySQL 8 coverage for `caching_sha2_password` full auth with RSA public-key retrieval.
- Add server ERR packet coverage with invalid SQL and access denied scenarios.
- Add charset coverage for utf8mb4, latin1, and binary columns.

## Compatibility tests adapted from upstream

- Adapt upstream SQL escaping and formatting fixtures.
- Adapt upstream auth plugin unit tests.
- Adapt upstream packet parser tests for length-coded numbers, OK packets, ERR packets, column definitions, and text rows.
- Adapt upstream integration tests for simple query, insert/update, transactions, errors, and charset behavior.
- Defer upstream prepared-statement and compression fixtures until those features are implemented.

## Security and fail-closed tests

- Unsupported auth plugin returns an error.
- `mysql_clear_password` without TLS returns an error.
- LOCAL INFILE request returns an error.
- Malformed length-coded packet returns an error rather than reading out of bounds.
- Missing named placeholder throws.
- Non-finite floating values escape as `NULL`.
- RSA auth path uses OAEP SHA1 to match upstream caching_sha2_password behavior.

## Release-blocking behaviors

- Build and unit tests pass on a clean checkout.
- Real MariaDB e2e passes.
- Real MySQL 8 e2e passes before claiming auth parity.
- TLS/compression/prepared-statement deferrals remain documented if not implemented.
- Third-party license notices are complete.
- Documentation builds with `python3 docs/build.py`.
- GitHub repo remains private until production-grade quality and public docs are ready.
