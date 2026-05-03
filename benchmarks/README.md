# mysql2 benchmarks

Benchmarks are optional diagnostic tools, not release gates. They are intended to
track relative overhead for this C++ wire-protocol port across repeatable local
workloads.

## Build polycpp mysql2 benchmark

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DPOLYCPP_MYSQL2_BUILD_TESTS=ON \
  -DPOLYCPP_MYSQL2_BUILD_BENCHMARKS=ON
cmake --build build -j$(nproc) --target bench_mysql2
```

Run against a reachable MariaDB/MySQL server:

```bash
MYSQL2_TEST_HOST=127.0.0.1 \
MYSQL2_TEST_PORT=3306 \
MYSQL2_TEST_USER=root \
MYSQL2_TEST_PASSWORD=secret \
MYSQL2_TEST_DATABASE=polycpp_test \
MYSQL2_BENCHMARK_ITERATIONS=1000 \
MYSQL2_BENCHMARK_ROWS=1000 \
build/bench_mysql2
```

Output is CSV:

```text
client,workload,iterations,total_ms,ops_per_sec
polycpp_mysql2,text_select_1,1000,123.456,8100.052
```

## Optional native C API comparison

A native MySQL C API comparison is useful as a local baseline, but it must stay
opt-in because the production companion intentionally does not link native
MySQL/MariaDB client SDKs.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release \
  -DPOLYCPP_MYSQL2_BUILD_TESTS=ON \
  -DPOLYCPP_MYSQL2_BUILD_BENCHMARKS=ON \
  -DPOLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=ON
cmake --build build -j$(nproc) --target bench_mysql2
```

This requires locally installed `mysqlclient` or MariaDB Connector/C headers and
library. Do not vendor those dependencies into this repo and do not link them
from `polycpp_mysql2`.

## Current workloads

- `text_select_1`: repeated `SELECT 1` text protocol queries.
- `prepared_add`: repeated prepared statement execute/fetch for `SELECT ? + ?`.
- `fetch_rows`: one recursive CTE result-set fetch of `MYSQL2_BENCHMARK_ROWS` rows.

Keep benchmark results out of correctness claims. They are environment-sensitive
and should be compared only on the same machine, build type, database server,
network path, and benchmark parameters.
