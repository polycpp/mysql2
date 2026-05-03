Raw Row Scan Tutorial
=====================

This tutorial shows how to use ``query_each_raw`` for a high-throughput
one-pass read.

Create a connection
-------------------

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "127.0.0.1";
   options.user = "root";
   options.database = "app";

   auto conn = polycpp::mysql2::create_connection(options);

Scan rows
---------

``query_each_raw`` calls the callback once per row. The view contains field
metadata and one raw value per field.

.. code-block:: cpp

   std::size_t rows = 0;
   conn.query_each_raw("SELECT id, email FROM users", [&](const auto& row) {
       const auto& id = row.at("id");
       const auto& email = row.at("email");
       if (!id.is_null && !email.is_null) {
           // Use or copy id.bytes and email.bytes before this callback returns.
       }
       ++rows;
   });

Copy data you need later
------------------------

The packet byte views expire when the callback returns. Copy values before
storing them in containers.

.. code-block:: cpp

   std::vector<std::string> emails;
   conn.query_each_raw("SELECT email FROM users", [&](const auto& row) {
       const auto& email = row.at(0);
       if (!email.is_null) {
           emails.emplace_back(email.bytes);
       }
   });

Choose the right row API
------------------------

- Use ``query`` for normal typed results retained in ``QueryResult``.
- Use ``query_stream`` to avoid retaining the whole result but still receive
  typed ``Row`` objects.
- Use ``query_each_raw`` for scan loops where callback-scoped packet bytes are
  sufficient and performance matters.
