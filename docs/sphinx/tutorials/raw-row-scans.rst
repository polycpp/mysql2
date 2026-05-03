Raw Row Scan Tutorial
=====================

This tutorial focuses on ``query_each_raw`` and the packet-view classes
``RawRowView`` and ``RawValueView``. Use this API for high-throughput one-pass
text-result scans when you can parse or copy values inside a callback.

Concept
-------

Normal ``query`` builds retained ``Row`` objects and typed ``Value`` variants.
That is the right default for application code. ``query_each_raw`` avoids those
allocations and conversions. It calls a callback for each text-protocol row and
exposes each field as packet-backed ``std::string_view`` bytes.

The tradeoff is ownership: raw bytes expire when the callback returns.

Class roles
-----------

.. list-table:: Raw scan types
   :header-rows: 1
   :widths: 24 38 38

   * - Type
     - Contains
     - Lifetime
   * - ``RawRowView``
     - Field metadata reference and one ``RawValueView`` per field.
     - Valid only during the callback.
   * - ``RawValueView``
     - ``is_null`` flag and raw ``bytes`` view.
     - ``bytes`` points into the current packet.
   * - ``Field``
     - Column name, type, flags, charset, and encoding.
     - Metadata reference is owned by connection internals during the scan.

Create a connection
-------------------

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "127.0.0.1";
   options.user = "root";
   options.database = "app";

   auto conn = polycpp::mysql2::create_connection(options);

Scan rows by name or index
--------------------------

``query_each_raw`` calls the callback once per row. Use ``row.at(name)`` for
clarity or ``row.at(index)`` when column order is fixed.

.. code-block:: cpp

   std::size_t rows = 0;
   conn.query_each_raw("SELECT id, email FROM users", [&](const auto& row) {
       const auto& id = row.at("id");
       const auto& email = row.at(1);
       if (!id.is_null && !email.is_null) {
           // Use or copy id.bytes and email.bytes before this callback returns.
       }
       ++rows;
   });

Copy data you need later
------------------------

Never store ``std::string_view`` values from the callback. Copy into an owning
``std::string``, ``polycpp::Buffer``, or application arena.

.. code-block:: cpp

   std::vector<std::string> emails;
   conn.query_each_raw("SELECT email FROM users", [&](const auto& row) {
       const auto& email = row.at(0);
       if (!email.is_null) {
           emails.emplace_back(email.bytes);
       }
   });

Parse numeric values yourself
-----------------------------

Raw scans do not convert text values. Parse only the columns you need:

.. code-block:: cpp

   auto parse_i64 = [](std::string_view bytes) {
       int64_t value = 0;
       const char* first = bytes.data();
       const char* last = bytes.data() + bytes.size();
       const auto parsed = std::from_chars(first, last, value);
       if (parsed.ec != std::errc{} || parsed.ptr != last) {
           throw polycpp::mysql2::Error("expected integer field");
       }
       return value;
   };

   int64_t sum = 0;
   conn.query_each_raw("SELECT id FROM users", [&](const auto& row) {
       const auto& id = row.at("id");
       if (!id.is_null) {
           sum += parse_i64(id.bytes);
       }
   });

Use QueryOptions for timeout and attributes
-------------------------------------------

Raw scans accept ``QueryOptions`` when a scan needs a timeout or query
attributes.

.. code-block:: cpp

   polycpp::mysql2::QueryOptions scan;
   scan.sql = "SELECT id FROM users";
   scan.timeout_ms = 5000;
   scan.attributes = {{"trace_id", std::string("scan-42")}};

   conn.query_each_raw(scan, [](const auto& row) {
       (void)row;
   });

If the callback throws before EOF, mysql2 closes the transport before
rethowing. This avoids reusing a partially drained protocol stream.

Choose the right row API
------------------------

.. list-table:: Row API selection
   :header-rows: 1
   :widths: 24 38 38

   * - API
     - Use it when
     - Result ownership
   * - ``query``
     - You need normal typed C++ rows and retained results.
     - ``QueryResult`` owns all rows.
   * - ``query_stream``
     - You need typed rows but do not want to retain the whole result.
     - Stream owns active command until EOF/cleanup.
   * - ``query_stream_json``
     - You need newline-delimited JSON ``Buffer`` chunks.
     - Stream owns active command until EOF/cleanup.
   * - ``query_each_raw``
     - You need one-pass speed and can parse/copy inside the callback.
     - Callback owns no data after it returns.

Limitations
-----------

``query_each_raw`` scans one text-protocol result set. It is not a replacement
for ``query_all`` or binary prepared result decoding. Use it when the query
shape is known and the application can handle raw text bytes deliberately.
