Server Protocol Mode
====================

Server mode exposes an adapted MySQL protocol listener. It is useful for tests,
protocol fixtures, proxy surfaces, and controlled fake servers. It is not a SQL
engine.

Create a loopback server
------------------------

.. code-block:: cpp

   polycpp::mysql2::ServerOptions options;
   options.handshake.server_version = "polycpp-mysql2-fixture";

   auto server = polycpp::mysql2::create_server(options);
   server.on(polycpp::mysql2::event::ServerConnectionAccepted,
       [](polycpp::mysql2::ServerConnection& connection) {
           connection.on(polycpp::mysql2::event::ServerQuery,
               [](polycpp::mysql2::ServerConnection& conn,
                  const std::string& sql) {
                   (void)sql;

                   polycpp::mysql2::Field field;
                   field.name = "answer";
                   field.column_type = polycpp::mysql2::constants::column_type::LONG;
                   field.character_set = 224;
                   field.encoding = "utf8";

                   polycpp::mysql2::Row row;
                   row.values = {int64_t{42}};

                   conn.write_text_result({row}, {field});
               });
       });

   server.listen();

``server.address()`` and ``server.port()`` expose the selected listen endpoint.
Use ``server.listen(path)`` or ``ServerOptions::socket_path`` for Unix socket
listeners.

Reject authentication
---------------------

The auth callback can inspect parsed client handshake data and fail closed with
an ERR packet.

.. code-block:: cpp

   options.handshake.auth_callback =
       [](const polycpp::mysql2::ServerAuthInfo& auth)
           -> std::optional<polycpp::mysql2::Error> {
           if (auth.user != "fixture") {
               return polycpp::mysql2::Error(1045, "28000", "access denied");
           }
           return std::nullopt;
       };

Write errors and OK packets
---------------------------

Handlers are responsible for writing explicit protocol responses.

.. code-block:: cpp

   connection.on(polycpp::mysql2::event::ServerPing,
       [](polycpp::mysql2::ServerConnection& conn) {
           conn.write_ok();
       });

   connection.on(polycpp::mysql2::event::ServerQuery,
       [](polycpp::mysql2::ServerConnection& conn, const std::string& sql) {
           if (sql != "SELECT 1") {
               conn.write_error(1064, "42000", "unsupported fixture SQL");
               return;
           }
           conn.write_ok();
       });

Prepared statement protocol
---------------------------

Server mode can emit statement prepare/execute events and write prepared
statement metadata. The handler still owns the fixture behavior.

.. code-block:: cpp

   connection.on(polycpp::mysql2::event::ServerStatementPrepare,
       [](polycpp::mysql2::ServerConnection& conn, const std::string& sql) {
           (void)sql;
           polycpp::mysql2::Field parameter;
           parameter.name = "param";
           parameter.column_type = polycpp::mysql2::constants::column_type::LONGLONG;

           polycpp::mysql2::Field field;
           field.name = "answer";
           field.column_type = polycpp::mysql2::constants::column_type::LONGLONG;

           conn.write_statement_prepare_ok(7, {parameter}, {field});
       });

   connection.on(polycpp::mysql2::event::ServerStatementExecute,
       [](polycpp::mysql2::ServerConnection& conn,
          const polycpp::mysql2::ServerStatementExecuteInfo& info) {
           (void)info.values;
           polycpp::mysql2::Field field;
           field.name = "answer";
           field.column_type = polycpp::mysql2::constants::column_type::LONGLONG;
           polycpp::mysql2::Row row;
           row.values = {int64_t{42}};
           conn.write_binary_result({row}, {field});
       });

TLS server mode
---------------

Set ``ServerOptions::tls`` to advertise ``CLIENT_SSL`` and upgrade with MySQL
in-protocol TLS after SSLRequest.

.. code-block:: cpp

   options.tls.enabled = true;
   options.tls.cert_file = "server-cert.pem";
   options.tls.key_file = "server-key.pem";

This is the MySQL protocol TLS upgrade. It is not a general ``tls.createServer``
replacement.
