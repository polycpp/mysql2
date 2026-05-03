Binlog and Replication Reads
============================

mysql2 exposes bounded and streaming wrappers around MySQL replication commands.
Use these APIs for protocol-level binlog consumption, not for normal SQL query
results.

Server prerequisites
--------------------

The server must have binary logging enabled. Row event decoding requires row
binlog format.

.. code-block:: sql

   SELECT @@log_bin AS log_bin;
   SET SESSION binlog_format = 'ROW';
   SHOW BINARY LOG STATUS;

Older servers may require ``SHOW MASTER STATUS`` instead of
``SHOW BINARY LOG STATUS``.

Choose a dump position
----------------------

.. code-block:: cpp

   auto status = conn.query("SHOW BINARY LOG STATUS");

   polycpp::mysql2::BinlogDumpOptions dump;
   dump.filename = std::get<std::string>(status.rows[0].at("File"));
   dump.binlog_position = std::get<uint64_t>(status.rows[0].at("Position"));
   dump.server_id = 62002;
   dump.flags = polycpp::mysql2::constants::binlog_dump_flags::NON_BLOCK;
   dump.max_events = 64;

Bounded reads
-------------

``binlog_dump`` returns a vector and therefore requires ``max_events`` for
blocking reads. Use it for finite diagnostics or tests.

.. code-block:: cpp

   auto events = conn.binlog_dump(dump);
   for (const auto& event : events) {
       if (event.name == "QueryEvent") {
           (void)event.query;
       }
   }

Callback-controlled reads
-------------------------

Use ``binlog_dump_each`` to avoid accumulating an unbounded vector. Return
``false`` to stop and close the replication command stream.

.. code-block:: cpp

   dump.max_events = 0;
   conn.binlog_dump_each(dump,
       [](const polycpp::mysql2::BinlogEvent& event) {
           if (event.name == "WriteRowsEventV2") {
               for (const auto& change : event.row_changes) {
                   (void)change.after;
               }
           }
           return true;
       });

Typed binlog stream
-------------------

``create_binlog_stream`` returns ``BinlogStream``, a pull-based
``polycpp::stream::Readable<BinlogEvent>``. Consume chunks with ``read`` or
``polycpp::stream::event::Data``.

.. code-block:: cpp

   auto stream = conn.create_binlog_stream(dump);
   while (auto event = stream.read()) {
       if (event->name == "RotateEvent") {
           (void)event->next_binlog;
       }
   }

.. code-block:: cpp

   auto stream = conn.create_binlog_stream(dump);
   stream.on(polycpp::stream::event::Data,
       [](const polycpp::mysql2::BinlogEvent& event) {
           (void)event.name;
       });

Lifecycle rules
---------------

A replication command stream reserves the connection. Normal commands are
rejected until EOF, ``max_events``, callback stop, or stream destroy/drop
cleanup ends the replication read.

Every terminal replication-stream path closes the transport. A later normal
command reconnects through the stored connection options. This avoids reusing a
socket that is still in, or has just left, replication packet mode.

Decoded event families
----------------------

Typed parsing covers Query, Rotate, FormatDescription, Xid, GTID,
PreviousGTIDs, TableMap, and common WriteRows/UpdateRows/DeleteRows events.
TIME2, DATETIME2, and TIMESTAMP2 row values decode into ``BinlogTime``,
``BinlogDateTime``, and ``BinlogTimestamp``. Unsupported event families retain
raw body bytes for audit.
