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

The shared-index change improves the one-column fetch workload by roughly 5-7%.
That is useful, but it proves the per-row name map was not the dominant cost for
this benchmark. The remaining gap is mostly in typed value construction,
per-row `Row`/`std::vector<Value>` allocation, text decoding, and result
retention semantics.

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
- `parse_text_row(...)` decodes every field buffer, calls `parse_text_value(...)`,
  and converts numeric MySQL fields into typed `int64_t`/`uint64_t`/`double`
  variants when possible.
- `parse_text_row(...)` also inserts every field name into a per-row
  `std::unordered_map<std::string, std::size_t>` for `Row::at("name")` lookup.
- `Row` stores `std::vector<Value>` plus the per-row name map, so every row has
  at least two dynamic containers even for a one-column result.

Relevant source locations:

- `src/mysql2.cpp:2297` (`parse_text_value`)
- `src/mysql2.cpp:2355` (`parse_text_row`)
- `src/mysql2.cpp:5160` (`read_query_result` row loop)
- `include/polycpp/mysql2/mysql2.hpp:149` (`Row` layout)

## Main conclusion

The bulk-row gap is mostly explained by semantic mismatch and allocation shape:

- Native C API returns raw `char**` views into an arena-buffered result.
- mysql2 JS returns already materialized JS row objects and appears highly
  optimized for this one-column text result path.
- polycpp mysql2 eagerly constructs typed C++ row values and per-row name maps.

The native C API is still a useful lower bound, but it is not an apples-to-apples
comparison for the public `QueryResult` API.

## Optimization candidates

1. Reserve `QueryResult::rows` when the OK/metadata path can know or estimate row
   counts. For text protocol this may require a growth heuristic rather than an
   exact count.
2. Add a raw or lightweight row view API for benchmark-sensitive scans. Example:
   a `query_rows_raw(...)`/`query_each(...)` path that exposes field buffers or
   typed values without retaining every row in a full `QueryResult`.
3. Add a result option for index-only rows when callers do not need
   `Row::at(name)`. This keeps the current ergonomic API but lets hot paths avoid
   name-map work.
4. Profile `PacketCursor::decode_buffer(...)` and numeric parsing separately.
   For ASCII-compatible numeric fields, parse directly from packet bytes without
   constructing an intermediate `std::string` when charset conversion is not
   needed.
5. Benchmark binary prepared-result rows separately from text rows. Binary rows
   avoid decimal text parsing for numeric fields and may be a better high-volume
   path.
6. Consider an arena or slab allocator for retained `QueryResult` rows/values.
   This would move polycpp closer to the native C API's allocation shape without
   exposing raw `char**` as the default API.

## Benchmark methodology changes made

- Added upstream mysql2 JavaScript benchmark script.
- Added `fetch_rows_materialized` to C++, native C API, and JS benchmarks.
- Added `MYSQL2_BENCHMARK_FETCH_REPEATS` so fetch workloads process enough rows
  for stable timing.
