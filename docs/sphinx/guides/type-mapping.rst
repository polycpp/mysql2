Type Mapping
============

This guide describes the C++ values you receive from query APIs and the values
you may pass into prepared statements, SQL formatting helpers, server-mode
responses, and binlog tests. The public value surface is intentionally typed:
callers should check the variant alternative they expect rather than relying on
implicit JavaScript-style coercion.

Value alternatives
------------------

Rows and prepared parameters use ``Value`` variants:

- ``std::monostate`` for SQL ``NULL``.
- ``bool`` for explicit boolean input values and JSON conversion.
- ``int64_t`` or ``uint64_t`` for integer columns when they fit the selected
  policy.
- ``double`` for float and double columns.
- ``std::string`` for decoded text, decimal strings, dates, times, datetimes,
  and JSON text.
- ``polycpp::Buffer`` for binary string/blob values.
- ``RawSql`` only as formatter input through ``raw``.
- ``BinlogDateTime``, ``BinlogTime``, and ``BinlogTimestamp`` for decoded
  binary-log temporal row values.

``RawSql`` is never a safe general query parameter. Prepared statement
execution rejects ``RawSql`` values; it exists only for deliberate
``format``/``format_named`` raw fragments.

Reading rows
------------

``QueryResult`` retains field metadata and materialized ``Row`` values. A row
can be read by zero-based index or by field name:

.. code-block:: cpp

   auto result = conn.query("SELECT 42 AS id, NULL AS marker");
   const auto& row = result.rows[0];

   auto id = std::get<int64_t>(row.at("id"));
   if (std::holds_alternative<std::monostate>(row.at("marker"))) {
       // SQL NULL.
   }

   auto same_id = std::get<int64_t>(row.at(0));
   (void)id;
   (void)same_id;

Use the field vector when conversion depends on server metadata:

.. code-block:: cpp

   for (std::size_t i = 0; i < result.fields.size(); ++i) {
       const auto& field = result.fields[i];
       const auto& value = result.rows[0].at(i);
       if (field.is_binary() && std::holds_alternative<polycpp::mysql2::Buffer>(value)) {
           // Keep bytes as bytes.
       }
   }

``Row::at(name)`` uses the result field names sent by the server. If a query
returns duplicate column names, prefer index-based access or alias the columns
to unique names in SQL.

Binary fields
-------------

Columns with binary charset or binary string/blob metadata are returned as
``polycpp::Buffer``. This matches mysql2's default behavior for binary values
and avoids accidental lossy text decoding.

JSON columns are decoded as UTF-8 text instead of ``Buffer``. Parse the string
with an application JSON parser when structured JSON access is needed.

Large numbers and decimals
--------------------------

``ConnectionOptions::big_number_strings`` returns ``LONGLONG`` values as text.
``ConnectionOptions::decimal_numbers`` parses decimal values as ``double``;
otherwise decimals remain strings to avoid precision loss.

Use these options deliberately:

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.big_number_strings = true;   // Preserve BIGINT/LONGLONG text.
   options.decimal_numbers = false;     // Preserve DECIMAL text.

   auto conn = polycpp::mysql2::create_connection(options);

Integer columns other than ``LONGLONG`` are parsed as signed or unsigned
integral variants when the server metadata marks them unsigned. If parsing does
not fit the target integer type, the text value is preserved as
``std::string``.

Dates and times
---------------

Date, time, datetime, and timestamp values are currently returned as strings.
This keeps timezone behavior explicit and avoids silently inventing a C++ date
policy that differs from upstream JavaScript behavior.

Binary-log temporal values are different because they represent decoded row
event payloads rather than SQL result strings. ``BinlogDateTime`` and
``BinlogTime`` expose component fields and ``to_string()`` helpers.
``BinlogTimestamp`` stores epoch seconds plus microseconds and also formats
with ``to_string()``.

JSON conversion
---------------

``value_to_json``, ``Row::to_json_object``, ``row_to_json_line``, and
``QueryResult::to_json`` preserve the same value policy:

- ``std::monostate`` becomes JSON ``null``.
- Safe-range integers become JSON numbers.
- Integers outside JavaScript's safe integer range become strings.
- ``Buffer`` values use the polycpp ``Buffer`` JSON representation.
- Binlog temporal values become strings through ``to_string()``.

``query_stream_json`` uses ``row_to_json_line`` and emits newline-delimited
JSON ``Buffer`` chunks. It keeps the underlying ``RowStream`` ownership rules:
the connection remains reserved until EOF or stream cleanup.

Writing values
--------------

Prepared execution is the preferred way to send user-controlled data:

.. code-block:: cpp

   conn.execute("INSERT INTO users(id, name, avatar) VALUES (?, ?, ?)",
                {int64_t{42},
                 std::string("Ada"),
                 polycpp::mysql2::Buffer::from({0x01, 0x02})});

``std::monostate`` sends SQL ``NULL``. ``Buffer`` sends binary data.
``BinlogDateTime`` / ``BinlogTime`` / ``BinlogTimestamp`` are encoded as their
string forms when used as prepared values.

For dynamic SQL text, use :doc:`sql-formatting` and prefer ``escape_id`` for
identifiers. ``raw(...)`` should be reserved for SQL fragments produced by
trusted application code, not for user input.
