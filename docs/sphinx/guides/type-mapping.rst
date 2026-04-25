Type Mapping
============

Rows contain ``Value`` variants:

- ``std::monostate`` for SQL ``NULL``.
- ``int64_t`` or ``uint64_t`` for integer columns when they fit the selected policy.
- ``double`` for float and double columns.
- ``std::string`` for decoded text, decimal strings, dates, times, datetimes, and JSON text.
- ``polycpp::Buffer`` for binary string/blob values.
- ``RawSql`` only as formatter input through ``raw``.

Binary fields
-------------

Columns with binary charset or binary string/blob metadata are returned as
``polycpp::Buffer``. This matches mysql2's default behavior for binary values
and avoids accidental lossy text decoding.

Large numbers and decimals
--------------------------

``ConnectionOptions::big_number_strings`` returns ``LONGLONG`` values as text.
``ConnectionOptions::decimal_numbers`` parses decimal values as ``double``;
otherwise decimals remain strings to avoid precision loss.

Dates and times
---------------

Date, time, datetime, and timestamp values are currently returned as strings.
This keeps timezone behavior explicit and avoids silently inventing a C++ date
policy that differs from upstream JavaScript behavior.
