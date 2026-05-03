Raw Row Scans
=============

Use ``Connection::query_each_raw`` when a query is a one-pass scan and the
caller does not need typed ``Row`` objects or retained ``QueryResult`` storage.
It exposes text-protocol field bytes as callback-scoped ``std::string_view``
values.

When to use it
--------------

Use raw scans for:

- counting rows or computing aggregates in application code;
- copying selected columns into an application-owned arena;
- decoding a known small set of columns with custom parsing;
- high-volume ETL-style reads where ``Row`` / ``Value`` construction is too
  expensive.

Prefer ``query`` or ``query_stream`` when you need mysql2's normal typed C++
values, charset-aware strings, binary column preservation as ``Buffer``, JSON
helpers, or multi-result handling.

Lifetime rules
--------------

``RawValueView::bytes`` points into the current MySQL packet. It is valid only
until the callback returns.

Do this when retaining data:

.. code-block:: cpp

   std::vector<std::string> names;
   conn.query_each_raw("SELECT name FROM users", [&](const auto& row) {
       const auto& name = row.at("name");
       if (!name.is_null) {
           names.emplace_back(name.bytes); // copy before returning
       }
   });

Do not store ``std::string_view`` values from the callback.

Parsing numbers
---------------

Raw scans do not convert values. Parse the packet bytes yourself:

.. code-block:: cpp

   int64_t parse_i64(std::string_view bytes) {
       int64_t value = 0;
       const char* first = bytes.data();
       const char* last = bytes.data() + bytes.size();
       const auto parsed = std::from_chars(first, last, value);
       if (parsed.ec != std::errc{} || parsed.ptr != last) {
           throw std::runtime_error("expected an integer");
       }
       return value;
   }

   int64_t sum = 0;
   conn.query_each_raw("SELECT id FROM users", [&](const auto& row) {
       const auto& id = row.at("id");
       if (!id.is_null) {
           sum += parse_i64(id.bytes);
       }
   });

Error behavior
--------------

If the callback throws before the result reaches EOF, mysql2 closes the
transport before rethrowing. This is intentional: the protocol stream is only
partially drained, so reusing the same socket would risk desynchronizing later
commands. A later command reconnects through the configured connection options.

Limitations
-----------

``query_each_raw`` is a C++ extension, not an upstream JavaScript object-row
API. It currently scans one text-protocol result set. It is not a replacement
for ``query_all`` or prepared/binary result decoding.
