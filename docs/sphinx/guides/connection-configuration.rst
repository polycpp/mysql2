Connection Configuration
========================

This guide collects the connection options that affect protocol behavior. Use it
when translating an upstream mysql2 configuration object into typed C++ options.

Direct options
--------------

.. code-block:: cpp

   polycpp::mysql2::ConnectionOptions options;
   options.host = "127.0.0.1";
   options.port = 3306;
   options.user = "app";
   options.password = "secret";
   options.database = "app";
   options.connect_timeout_ms = 5000;

   auto conn = polycpp::mysql2::create_connection(options);

Use ``socket_path`` for Unix domain sockets. When ``socket_path`` is set, the
TCP ``host`` and ``port`` are not used for the transport:

.. code-block:: cpp

   options.socket_path = "/var/run/mysqld/mysqld.sock";

URI options
-----------

``create_connection(std::string)`` and ``parse_connection_uri`` accept
``mysql://`` and ``mysqls://`` URIs. ``mysqls://`` enables TLS.

.. code-block:: cpp

   auto conn = polycpp::mysql2::create_connection(
       "mysqls://app:secret@db.example.com:3306/app"
       "?sslProfile=Amazon%20RDS&connectTimeout=5000");

Recognized query parameters include:

- ``charset`` and ``charsetNumber``.
- ``connectTimeout`` or ``connect_timeout_ms``.
- ``maxPreparedStatements`` or ``max_prepared_statements``.
- ``multipleStatements`` or ``multiple_statements``.
- ``supportBigNumbers`` / ``bigNumberStrings`` / ``decimalNumbers``.
- ``enableKeepAlive`` and ``enableCleartextPlugin``.
- ``compress``.
- ``socketPath`` or ``socket_path``.
- ``ssl``, ``sslProfile``, ``rejectUnauthorized``, ``verifyIdentity``.
- ``ssl.ca``, ``ssl.cert``, and ``ssl.key`` file paths.

Timeouts
--------

``connect_timeout_ms`` applies to the initial TCP or Unix socket connect.
Command timeouts are per operation and close the transport on expiry so the
connection cannot be reused with a half-read packet stream.

.. code-block:: cpp

   polycpp::mysql2::QueryOptions query;
   query.sql = "SELECT SLEEP(2)";
   query.timeout_ms = 100;

   try {
       conn.query(query);
   } catch (const polycpp::mysql2::Error&) {
       // The transport has been closed. A later command reconnects through
       // the stored ConnectionOptions when that is safe for the operation.
   }

Prepared statement preparation and cursor fetch use ``CommandOptions``:

.. code-block:: cpp

   polycpp::mysql2::CommandOptions command;
   command.timeout_ms = 1000;

   auto stmt = conn.prepare("SELECT ?", command);

Compression
-----------

Set ``compress`` to request the MySQL compressed packet protocol. Compression
is negotiated with the server during handshake; check ``conn.compressed()`` if a
caller needs to know whether it became active.

.. code-block:: cpp

   options.compress = true;
   auto conn = polycpp::mysql2::create_connection(options);
   if (conn.compressed()) {
       // Server accepted CLIENT_COMPRESS.
   }

LOCAL INFILE
------------

The port never opens server-requested file paths by default. A server
``LOCAL INFILE`` request fails unless the caller provides an explicit memory
handler. The handler receives the path string sent by the server and returns
chunks to upload.

.. code-block:: cpp

   options.local_infile_handler = [](const std::string& path) {
       if (path != "allowed.csv") {
           throw polycpp::mysql2::Error("unexpected LOCAL INFILE path");
       }
       return std::vector<polycpp::mysql2::Buffer>{
           polycpp::mysql2::Buffer::from("1,Ada\n"),
           polycpp::mysql2::Buffer::from("2,Lin\n"),
       };
   };

Connection attributes
---------------------

``connect_attributes`` are sent during the initial handshake and
``change_user`` when the server advertises connect-attribute capability.

.. code-block:: cpp

   options.connect_attributes["service"] = "billing-api";
   options.connect_attributes["trace_source"] = "polycpp";

Charsets and numbers
--------------------

``charset`` names are mapped to MySQL collation ids. Non-core string conversion
uses the ``iconv-lite`` companion library.

.. code-block:: cpp

   options.charset = "utf8mb4";
   options.big_number_strings = true;
   options.decimal_numbers = false;

Use ``big_number_strings`` when exact integer text is more important than a C++
integer value. Use ``decimal_numbers`` only when converting DECIMAL to
``double`` is acceptable for the application.
