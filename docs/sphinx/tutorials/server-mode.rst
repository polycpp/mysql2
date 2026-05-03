Server Protocol Mode
====================

Server mode exposes an adapted MySQL protocol listener. Use it for integration
tests, protocol fixtures, controlled fake servers, and proxy surfaces. It is not
a SQL engine. A ``ServerConnection`` handler decides how every accepted client
command should be answered.

This distinction matters: server mode understands MySQL packets and can write
valid MySQL responses, but it does not parse SQL into tables or execute query
plans. If a fixture receives ``SELECT 1``, your handler must choose to return a
one-column result, an OK packet, or an ERR packet.

Class model
-----------

.. list-table:: Server-side classes
   :header-rows: 1
   :widths: 24 38 38

   * - Type
     - Contains
     - Use it for
   * - ``ServerOptions``
     - Host, port, Unix socket path, backlog, auto-handshake flag, handshake
       settings, and TLS material.
     - Describing how the protocol listener should start.
   * - ``ServerHandshakeOptions``
     - Server version string, connection id, capability flags, charset, status,
       auth plugin name, and optional auth callback.
     - Controlling the greeting packet and authentication gate.
   * - ``ServerAuthInfo``
     - Parsed client user, database, address, client flags, charset, auth token,
       plugin name, and connect attributes.
     - Making auth decisions or recording client metadata in fixtures.
   * - ``Server``
     - The listener and accepted connection set.
     - Starting/stopping TCP or Unix socket protocol endpoints.
   * - ``ServerConnection``
     - One accepted client protocol session.
     - Receiving command events and writing explicit protocol responses.
   * - ``ServerStatementExecuteInfo``
     - Statement id, flags, iteration count, decoded bound values, original
       query when known, and raw payload.
     - Handling prepared-statement execute packets in fixtures.

Create a loopback server
------------------------

A server usually starts with ``create_server`` and a connection-accepted event.
Per-client command handlers are installed on the accepted ``ServerConnection``.

.. code-block:: cpp

   polycpp::mysql2::ServerOptions options;
   options.handshake.server_version = "polycpp-mysql2-fixture";

   auto server = polycpp::mysql2::create_server(options);
   server.on(polycpp::mysql2::event::ServerConnectionAccepted,
       [](polycpp::mysql2::ServerConnection& connection) {
           connection.on(polycpp::mysql2::event::ServerQuery,
               [](polycpp::mysql2::ServerConnection& conn,
                  const std::string& sql) {
                   if (sql != "SELECT 42 AS answer") {
                       conn.write_error(1064, "42000", "unsupported fixture SQL");
                       return;
                   }

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
A port of ``0`` asks the OS to choose an ephemeral port, which is useful for
parallel tests.

Choose a listen transport
-------------------------

.. list-table:: Listen choices
   :header-rows: 1
   :widths: 28 36 36

   * - API
     - Use it when
     - Notes
   * - ``server.listen()``
     - ``ServerOptions`` already contains host/port or socket path.
     - Uses ``socket_path`` when it is non-empty; otherwise TCP.
   * - ``server.listen(port)``
     - Loopback TCP fixture with a known or ephemeral port.
     - Uses the host configured in ``ServerOptions``.
   * - ``server.listen(port, host)``
     - TCP fixture bound to a specific interface.
     - Prefer loopback for tests unless remote clients must connect.
   * - ``server.listen(path)``
     - Unix socket fixture.
     - Uses polycpp Unix IPC primitives and bypasses TCP.

Reject authentication
---------------------

The auth callback receives parsed client handshake data and can fail closed by
returning a MySQL error. Returning ``std::nullopt`` accepts the client.
Configure it before constructing or listening with the server.

.. code-block:: cpp

   options.handshake.auth_callback =
       [](const polycpp::mysql2::ServerAuthInfo& auth)
           -> std::optional<polycpp::mysql2::Error> {
           if (auth.user != "fixture") {
               return polycpp::mysql2::Error(1045, "28000", "access denied");
           }
           if (auth.database != "test_fixture") {
               return polycpp::mysql2::Error(1049, "42000", "unknown database");
           }
           return std::nullopt;
       };

Use ``auth.connect_attributes`` when a test needs to assert client-side connect
attributes. Use ``auth.auth_plugin_name`` when testing auth plugin negotiation.

Understand command events
-------------------------

Server mode dispatches typed events for the commands this port understands. It
also exposes a packet observation event for audit and diagnostics.

.. list-table:: Server command events
   :header-rows: 1
   :widths: 30 35 35

   * - Event
     - Payload
     - Typical response
   * - ``event::ServerQuery``
     - ``ServerConnection&`` and SQL string.
     - ``write_text_result``, ``write_ok``, or ``write_error``.
   * - ``event::ServerPing``
     - ``ServerConnection&``.
     - ``write_ok``.
   * - ``event::ServerQuit``
     - ``ServerConnection&``.
     - Usually close or no response after quit handling.
   * - ``event::ServerInitDb``
     - ``ServerConnection&`` and database name.
     - ``write_ok`` or unknown-database ``write_error``.
   * - ``event::ServerFieldList``
     - ``ServerConnection&``, table, and wildcard.
     - ``write_columns`` plus EOF, or ``write_error``.
   * - ``event::ServerStatementPrepare``
     - ``ServerConnection&`` and SQL string.
     - ``write_statement_prepare_ok`` or ``write_error``.
   * - ``event::ServerStatementExecute``
     - ``ServerConnection&`` and ``ServerStatementExecuteInfo``.
     - ``write_binary_result``, ``write_ok``, or ``write_error``.
   * - ``event::ServerPacket``
     - ``ServerConnection&``, raw packet, compressed flag, sequence id.
     - Observe only; command handlers still own responses.

Write explicit responses
------------------------

Handlers are responsible for protocol responses. This makes fixtures precise and
prevents accidental behavior from looking like a real SQL engine.

.. list-table:: Response writers
   :header-rows: 1
   :widths: 30 35 35

   * - Writer
     - Sends
     - Use it for
   * - ``write_ok``
     - OK packet with affected rows, insert id, warnings, and status.
     - Successful command with no result set.
   * - ``write_error``
     - ERR packet with code, SQL state, and message.
     - Unsupported fixture SQL, auth failure, or protocol error simulation.
   * - ``write_columns``
     - Result column metadata.
     - Manual result packet construction or field-list responses.
   * - ``write_text_row``
     - One text-protocol row.
     - Manual text result construction.
   * - ``write_binary_row``
     - One binary-protocol row.
     - Prepared statement result construction.
   * - ``write_eof``
     - EOF terminator.
     - Manual metadata/result termination when needed.
   * - ``write_text_result``
     - Complete text result from rows and fields, or a ``QueryResult``.
     - Most ``COM_QUERY`` result fixtures.
   * - ``write_binary_result``
     - Complete binary result from rows and fields, or a ``QueryResult``.
     - Most prepared-statement execute fixtures.
   * - ``write_statement_prepare_ok``
     - Prepared statement id and parameter/result metadata.
     - ``COM_STMT_PREPARE`` fixtures.

Return text query results
-------------------------

A text result requires field metadata and row values. Use MySQL column type
constants so clients can decode the result consistently.

.. code-block:: cpp

   connection.on(polycpp::mysql2::event::ServerQuery,
       [](polycpp::mysql2::ServerConnection& conn, const std::string& sql) {
           if (sql == "SELECT name FROM users") {
               polycpp::mysql2::Field field;
               field.name = "name";
               field.column_type = polycpp::mysql2::constants::column_type::VAR_STRING;
               field.character_set = 224;
               field.encoding = "utf8";

               polycpp::mysql2::Row ada;
               ada.values = {std::string("Ada")};
               polycpp::mysql2::Row lin;
               lin.values = {std::string("Lin")};

               conn.write_text_result({ada, lin}, {field});
               return;
           }

           conn.write_error(1064, "42000", "fixture query not supported");
       });

Return OK and ERR packets
-------------------------

Use OK packets for successful non-result commands. Fill ``OkPacket`` when tests
need affected rows or insert ids.

.. code-block:: cpp

   connection.on(polycpp::mysql2::event::ServerQuery,
       [](polycpp::mysql2::ServerConnection& conn, const std::string& sql) {
           if (sql.rfind("INSERT", 0) == 0) {
               polycpp::mysql2::OkPacket ok;
               ok.affected_rows = 1;
               ok.insert_id = 42;
               conn.write_ok(ok);
               return;
           }
           conn.write_error(1064, "42000", "expected insert");
       });

Prepared statement protocol
---------------------------

Prepared statement fixtures are a two-step protocol. ``ServerStatementPrepare``
returns a statement id plus metadata. ``ServerStatementExecute`` receives the id
and decoded parameter values.

.. code-block:: cpp

   connection.on(polycpp::mysql2::event::ServerStatementPrepare,
       [](polycpp::mysql2::ServerConnection& conn, const std::string& sql) {
           if (sql != "SELECT ? AS answer") {
               conn.write_error(1064, "42000", "unsupported statement");
               return;
           }

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
           if (info.statement_id != 7 || info.values.empty()) {
               conn.write_error(1243, "HY000", "unknown statement handler");
               return;
           }

           polycpp::mysql2::Field field;
           field.name = "answer";
           field.column_type = polycpp::mysql2::constants::column_type::LONGLONG;

           polycpp::mysql2::Row row;
           row.values = {info.values[0]};
           conn.write_binary_result({row}, {field});
       });

TLS server mode
---------------

Set ``ServerOptions::tls`` to advertise ``CLIENT_SSL`` and upgrade with the
MySQL in-protocol TLS flow after SSLRequest.

.. code-block:: cpp

   options.tls.enabled = true;
   options.tls.cert_file = "server-cert.pem";
   options.tls.key_file = "server-key.pem";

This is the MySQL protocol TLS upgrade. It is not a general ``tls.createServer``
replacement; use polycpp TLS server primitives directly for non-MySQL protocols.

Lifecycle and shutdown
----------------------

Call ``server.close()`` to stop accepting new clients and close existing
connections. ``ServerConnection::close()`` closes one accepted client. A server
object is move-only, so tests can keep one owner for deterministic cleanup.

.. code-block:: cpp

   auto server = polycpp::mysql2::create_server(options);
   server.listen(0, "127.0.0.1");
   auto port = server.port();

   // Run client fixture work against 127.0.0.1:port.

   server.close();

When to use server mode
-----------------------

Use server mode when you need a MySQL-speaking fixture or proxy surface that is
faster and more controllable than starting a full database. Use a real
MySQL/MariaDB server for SQL semantics, optimizer behavior, storage behavior,
privileges, replication topology, or compatibility testing against a vendor
implementation.
