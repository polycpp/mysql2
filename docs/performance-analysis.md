# mysql2 Performance Analysis Notes

## Scope

This note records the first investigation into the large `fetch_rows` benchmark
spread between `polycpp_mysql2`, upstream `mysql2` JavaScript, and the native
MariaDB/MySQL C API.

The native C API source reviewed is MariaDB Connector/C:

- GitHub: https://github.com/mariadb-corporation/mariadb-connector-c
- Local clone: `/data/work/upstream/mariadb-connector-c`
- Reviewed clone: branch `3.4`, commit `00657ac`
- Host runtime package used by the benchmark: `libmariadb3:amd64 1:10.6.23-0ubuntu0.22.04.1`

The source package for Ubuntu `libmariadb3` is MariaDB 10.6, while the cloned
Connector/C tree is the standalone upstream connector. The result-buffering
implementation shape is the same client-library family and is useful for
explaining the benchmark behavior.

## Current benchmark snapshot

Release build, MySQL 8.4 Docker server, container-network path, GCC 11.4,
`MYSQL2_BENCHMARK_ITERATIONS=1000`, `MYSQL2_BENCHMARK_ROWS=1000`,
`MYSQL2_BENCHMARK_FETCH_REPEATS=100`.

### Baseline before shared field index

| client | workload | processed units | total ms | throughput |
| --- | --- | ---: | ---: | ---: |
| polycpp_mysql2 | text_select_1 | 1000 queries | 93.733 | 10,668.598 q/s |
| polycpp_mysql2 | prepared_add | 1000 queries | 85.873 | 11,645.057 q/s |
| polycpp_mysql2 | fetch_rows | 100000 rows | 566.432 | 176,543.607 rows/s |
| polycpp_mysql2 | fetch_rows_materialized | 100000 rows | 570.803 | 175,191.896 rows/s |
| native_c_api | text_select_1 | 1000 queries | 67.256 | 14,868.598 q/s |
| native_c_api | prepared_add | 1000 queries | 58.814 | 17,002.818 q/s |
| native_c_api | fetch_rows | 100000 rows | 28.029 | 3,567,710.126 rows/s |
| native_c_api | fetch_rows_materialized | 100000 rows | 28.667 | 3,488,362.561 rows/s |
| mysql2_js | text_select_1 | 1000 queries | 182.855 | 5,468.812 q/s |
| mysql2_js | prepared_add | 1000 queries | 159.157 | 6,283.101 q/s |
| mysql2_js | fetch_rows | 100000 rows | 46.628 | 2,144,650.776 rows/s |
| mysql2_js | fetch_rows_materialized | 100000 rows | 44.957 | 2,224,369.332 rows/s |

### After shared field index

This revision removes parsed-row per-row `unordered_map` construction. Parsed
rows now point at one field-name index built from result metadata, while manual
rows can still use `Row::index_by_name` as a fallback.

| client | workload | processed units | total ms | throughput |
| --- | --- | ---: | ---: | ---: |
| polycpp_mysql2 | text_select_1 | 1000 queries | 95.190 | 10,505.351 q/s |
| polycpp_mysql2 | prepared_add | 1000 queries | 86.564 | 11,552.114 q/s |
| polycpp_mysql2 | fetch_rows | 100000 rows | 538.586 | 185,671.522 rows/s |
| polycpp_mysql2 | fetch_rows_materialized | 100000 rows | 532.858 | 187,667.257 rows/s |
| native_c_api | text_select_1 | 1000 queries | 65.866 | 15,182.445 q/s |
| native_c_api | prepared_add | 1000 queries | 57.518 | 17,385.747 q/s |
| native_c_api | fetch_rows | 100000 rows | 27.743 | 3,604,514.149 rows/s |
| native_c_api | fetch_rows_materialized | 100000 rows | 28.347 | 3,527,695.230 rows/s |

The shared-index change improved the one-column fetch workload by roughly
5-7%. That was useful, but it proved the per-row name map was not the dominant
cost for this benchmark.

### After packet views and single-packet fast path

This revision changed `PacketCursor` so lvalue packet buffers are viewed rather
than copied, text row parsing uses packet-backed `std::string_view` for
length-encoded values, numeric text fields parse directly from packet bytes, and
uncompressed packet reads avoid vector-to-`Buffer` copy plus `Buffer::concat()`
for ordinary single-packet frames.

| client | workload | processed units | total ms | throughput |
| --- | --- | ---: | ---: | ---: |
| polycpp_mysql2 | text_select_1 | 1000 queries | 94.347 | 10,599.171 q/s |
| polycpp_mysql2 | prepared_add | 1000 queries | 87.555 | 11,421.342 q/s |
| polycpp_mysql2 | fetch_rows | 100000 rows | 472.757 | 211,524.961 rows/s |
| polycpp_mysql2 | fetch_rows_materialized | 100000 rows | 495.490 | 201,820.462 rows/s |
| native_c_api | text_select_1 | 1000 queries | 69.713 | 14,344.536 q/s |
| native_c_api | prepared_add | 1000 queries | 60.300 | 16,583.618 q/s |
| native_c_api | fetch_rows | 100000 rows | 29.390 | 3,402,517.632 rows/s |
| native_c_api | fetch_rows_materialized | 100000 rows | 29.678 | 3,369,528.017 rows/s |

The packet and parser changes improved the fetch workload by about 14% over the
shared-index result and about 20% over the original baseline. That confirmed
unnecessary packet/value copies mattered, but still did not explain the order of
magnitude gap.

### After buffered transport reads

This revision added an internal 64 KiB read-ahead buffer for synchronous client
reads. `read_exact_into(...)` now satisfies small packet header/payload reads
from already-buffered bytes when possible and refills with `asyncReadSome()`.
This avoids scheduling an event-loop read for every 4-byte MySQL header and
every small row payload.

| client | workload | processed units | total ms | throughput |
| --- | --- | ---: | ---: | ---: |
| polycpp_mysql2 | text_select_1 | 1000 queries | 90.989 | 10,990.301 q/s |
| polycpp_mysql2 | prepared_add | 1000 queries | 81.640 | 12,248.930 q/s |
| polycpp_mysql2 | fetch_rows | 100000 rows | 46.406 | 2,154,880.205 rows/s |
| polycpp_mysql2 | fetch_rows_materialized | 100000 rows | 44.956 | 2,224,387.490 rows/s |
| native_c_api | text_select_1 | 1000 queries | 70.923 | 14,099.891 q/s |
| native_c_api | prepared_add | 1000 queries | 61.407 | 16,284.854 q/s |
| native_c_api | fetch_rows | 100000 rows | 29.125 | 3,433,432.777 rows/s |
| native_c_api | fetch_rows_materialized | 100000 rows | 28.837 | 3,467,707.099 rows/s |

The read-ahead change is the dominant fix: one-column fetch throughput now
matches the earlier upstream `mysql2` JavaScript result and is within roughly
1.6x of the native C API on this benchmark. Small-query throughput also improves
slightly because fewer read operations are scheduled while consuming result
metadata and row packets.

`fetch_rows_materialized` explicitly copies parsed first-column integers into an
application vector/array and computes a checksum. For this one-int-column query,
that extra work is still small compared with each driver's core row handling.

## Native C API implementation observations

MariaDB Connector/C's text-result path is intentionally low-level:

- `mysql_store_result()` delegates result buffering to `db_read_rows(...)` and
  then returns a `MYSQL_RES` that owns the buffered `MYSQL_DATA`.
- `mthd_my_read_rows(...)` allocates rows from an arena-style `MA_MEM_ROOT`,
  copies each packet field byte range into one contiguous allocation for the row,
  and stores a `char*` pointer per column.
- `mysql_fetch_row()` for a buffered result does not parse or allocate a typed
  object. It returns the current row's `char**` and advances a linked-list cursor.

Relevant source locations in the local clone:

- `/data/work/upstream/mariadb-connector-c/libmariadb/mariadb_lib.c:1229`
- `/data/work/upstream/mariadb-connector-c/libmariadb/mariadb_lib.c:1238`
- `/data/work/upstream/mariadb-connector-c/libmariadb/mariadb_lib.c:1267`
- `/data/work/upstream/mariadb-connector-c/libmariadb/mariadb_lib.c:3005`
- `/data/work/upstream/mariadb-connector-c/libmariadb/mariadb_lib.c:3125`

This means the native C API benchmark is not constructing C++ objects, maps, or
variant values. Even the materialized variant only parses one integer after the
C API has already buffered raw row bytes.

## polycpp mysql2 implementation observations

`polycpp_mysql2` currently does significantly more per row:

- `read_query_result(...)` appends a full `Row` object to `QueryResult::rows` for
  every packet.
- `parse_text_row(...)` converts numeric MySQL text fields into typed
  `int64_t`/`uint64_t`/`double` variants when possible and decodes string fields
  into owning `std::string` values.
- Parsed rows share one result-level field-name index for `Row::at("name")`;
  manually constructed rows can still use the per-row `index_by_name` fallback.
- `Row` stores `std::vector<Value>`, so every retained row still owns a dynamic
  values container even for a one-column result.
- The synchronous client now has a transport read-ahead buffer. Without that
  buffer, every row packet caused separate event-loop read operations for its
  header and payload, which was the dominant cost in the original benchmark.

Relevant source locations:

- `src/mysql2.cpp` (`PacketCursor`, `parse_text_value`, `parse_text_row`)
- `src/mysql2.cpp` (`read_exact_into`, `read_uncompressed_packet`)
- `src/mysql2.cpp` (`read_query_result` row loop)
- `include/polycpp/mysql2/mysql2.hpp:149` (`Row` layout)

## Main conclusion

The original bulk-row gap had two different causes:

- The largest fixable cause was transport shape: without a client-side read
  buffer, the synchronous wrapper scheduled an event-loop read for every small
  MySQL packet header and row payload.
- The remaining gap to native C API is mostly explained by semantic mismatch and
  allocation shape:

- Native C API returns raw `char**` views into an arena-buffered result.
- mysql2 JS returns already materialized JS row objects and appears highly
  optimized for this one-column text result path.
- polycpp mysql2 eagerly constructs typed C++ row values in retained `Row`
  objects.

The native C API is still a useful lower bound, but it is not an apples-to-apples
comparison for the public `QueryResult` API.

## Optimization candidates

1. Add a raw or lightweight row view API for benchmark-sensitive scans. Example:
   a `query_rows_raw(...)`/`query_each(...)` path that exposes field buffers or
   typed values without retaining every row in a full `QueryResult`.
2. Benchmark binary prepared-result rows separately from text rows. Binary rows
   avoid decimal text parsing for numeric fields and may be a better high-volume
   path.
3. Consider an arena or slab allocator for retained `QueryResult` rows/values.
   This would move polycpp closer to the native C API's allocation shape without
   exposing raw `char**` as the default API.
4. Consider a row container optimization for one-column or small fixed-column
   results if profiling shows `std::vector<Value>` allocation is still material.

Completed optimizations:

- Parsed rows share one result-level field-name index.
- `PacketCursor` views lvalue packet buffers instead of copying them.
- Text row parsing uses length-encoded packet views and parses numeric text
  fields without constructing intermediate strings.
- Uncompressed packet reads use stack headers, direct `Buffer` reads, and a
  single-packet fast path.
- The synchronous client uses a 64 KiB transport read-ahead buffer for socket,
  pipe, and TLS reads.

## Benchmark methodology changes made

- Added upstream mysql2 JavaScript benchmark script.
- Added `fetch_rows_materialized` to C++, native C API, and JS benchmarks.
- Added `MYSQL2_BENCHMARK_FETCH_REPEATS` so fetch workloads process enough rows
  for stable timing.
