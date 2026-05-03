Benchmarking
============

Benchmarks are diagnostic tools, not correctness or release gates. Use them to
compare changes on the same machine, build type, database server, network path,
and workload settings.

Build
-----

.. code-block:: bash

   cmake -B build -DCMAKE_BUILD_TYPE=Release \
     -DPOLYCPP_MYSQL2_BUILD_TESTS=ON \
     -DPOLYCPP_MYSQL2_BUILD_BENCHMARKS=ON
   cmake --build build -j$(nproc) --target bench_mysql2

Run
---

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=polycpp_test \
   MYSQL2_BENCHMARK_ITERATIONS=1000 \
   MYSQL2_BENCHMARK_ROWS=1000 \
   MYSQL2_BENCHMARK_FETCH_REPEATS=50 \
   build/bench_mysql2

Output is CSV:

.. code-block:: text

   client,workload,iterations,total_ms,ops_per_sec
   polycpp_mysql2,text_select_1,1000,123.456,8100.052

Workloads
---------

- ``text_select_1``: repeated ``SELECT 1`` text queries.
- ``prepared_add``: repeated prepared statement execute/fetch.
- ``fetch_rows``: typed retained ``QueryResult`` fetch.
- ``fetch_rows_raw``: ``query_each_raw`` packet-view scan.
- ``fetch_rows_stream``: ``query_stream`` typed row stream.
- ``fetch_rows_materialized``: fetch plus application-side materialization.
- ``fetch_rows_prepared``: prepared/binary result fetch.

Optional native C API comparison
--------------------------------

The native MySQL C API comparison is opt-in and links only into the benchmark
executable. The production companion library must not link native MySQL or
MariaDB client SDKs.

.. code-block:: bash

   cmake -B build -DCMAKE_BUILD_TYPE=Release \
     -DPOLYCPP_MYSQL2_BUILD_TESTS=ON \
     -DPOLYCPP_MYSQL2_BUILD_BENCHMARKS=ON \
     -DPOLYCPP_MYSQL2_BENCHMARK_NATIVE_C_API=ON
   cmake --build build -j$(nproc) --target bench_mysql2

This requires locally installed ``mysqlclient`` or MariaDB Connector/C headers
and library.

Optional upstream JavaScript comparison
---------------------------------------

``benchmarks/mysql2_js_benchmark.mjs`` runs matching workload names through
upstream ``mysql2/promise``. Install the npm package outside this repo so the
checkout remains clean.

.. code-block:: bash

   mkdir -p /tmp/mysql2-js-bench
   npm install --prefix /tmp/mysql2-js-bench mysql2@3.22.3
   cd /tmp/mysql2-js-bench

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=polycpp_test \
   node /data/work/lib/mysql2/benchmarks/mysql2_js_benchmark.mjs

See ``docs/performance-analysis.md`` in the repository for the current bulk-row
fetch investigation and optimization notes.
