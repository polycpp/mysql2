Callbacks, Promises, Events, and Streams
========================================

The primary mysql2 C++ API is synchronous and typed. Callback, Promise,
EventEmitter, and stream adapters exist for code that wants Node-like control
surfaces while still using explicit C++ values.

Callbacks
---------

Callback overloads execute the same operation as the synchronous API and report
errors through ``std::exception_ptr``.

.. code-block:: cpp

   conn.query("SELECT 1 AS one",
       [](std::exception_ptr error, polycpp::mysql2::QueryResult result) {
           if (error) {
               try {
                   std::rethrow_exception(error);
               } catch (const polycpp::mysql2::Error& mysql_error) {
                   (void)mysql_error;
               }
               return;
           }
           (void)result;
       });

Promises
--------

Promise wrappers return ``polycpp::Promise<T>``. Promise callbacks are deferred
through polycpp's event loop; run the event loop for ``then`` / ``catch_``
handlers to execute.

.. code-block:: cpp

   auto promise = conn.query_promise("SELECT 1 AS one");
   promise.then([](const polycpp::mysql2::QueryResult& result) {
       (void)result;
   });

   polycpp::EventLoop::instance().run();

Promise wrappers do not make the underlying command concurrent. They adapt the
same synchronous operation into a Promise settlement shape.

Connection events
-----------------

Connections emit typed events. ``event::Trace`` is the replacement for Node's
diagnostic-channel hooks.

.. code-block:: cpp

   conn.on(polycpp::mysql2::event::Connect,
       [](const polycpp::mysql2::ConnectionInfo& info) {
           (void)info.server_version;
       });

   conn.on(polycpp::mysql2::event::Trace,
       [](const polycpp::mysql2::TraceEvent& event) {
           if (event.operation == "query" && event.phase == "error") {
               (void)event.error;
           }
       });

Pool and cluster events
-----------------------

Pools emit ``ConnectionCreated``, ``Acquire``, ``Release``, and ``Enqueue``.
Pool clusters also emit ``Warn``, ``Offline``, ``Online``, and ``Remove``.

.. code-block:: cpp

   pool.on(polycpp::mysql2::event::Acquire,
       [](polycpp::mysql2::Connection& connection) {
           (void)connection.connection_id();
       });

Typed row streams
-----------------

``query_stream`` returns a pull-based ``RowStream``. It reads result metadata
up front and decodes row packets as the stream is consumed. The connection is
reserved until EOF or stream destruction cleanup drains the remaining packets.

.. code-block:: cpp

   auto rows = conn.query_stream("SELECT id, name FROM users ORDER BY id");
   while (auto row = rows.read()) {
       (void)row->at("id");
   }

``query_stream_json`` adapts the same row stream to newline-delimited JSON
``Buffer`` chunks for byte-stream consumers.

.. code-block:: cpp

   auto chunks = conn.query_stream_json("SELECT id FROM users");
   while (auto chunk = chunks.read()) {
       (void)chunk->toString();
   }
