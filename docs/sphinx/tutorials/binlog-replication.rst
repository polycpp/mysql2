Binlog and Replication Reads
============================

mysql2 exposes bounded, callback-controlled, stream-shaped, and parser-only APIs
around MySQL replication packets. Use these APIs for protocol-level binlog
consumption, replication tests, audit tooling, and fixtures. Do not use them for
normal SQL query results.

Concept
-------

A binlog dump is a MySQL replication command, not a query. While the server is
sending replication packets, the connection is in replication packet mode and
cannot safely run ordinary commands. This port makes that ownership explicit:
replication APIs reserve the connection, and terminal paths close the transport
so a later normal command can reconnect from a clean protocol state.

Class model
-----------

.. list-table:: Replication classes
   :header-rows: 1
   :widths: 24 38 38

   * - Type
     - Contains
     - Use it for
   * - ``RegisterSlaveOptions``
     - Replication client identity sent through ``COM_REGISTER_SLAVE``.
     - Servers or tests that require classic slave registration before dump.
   * - ``BinlogDumpOptions``
     - Filename, position, flags, replication server id, optional max events,
       and optional GTID set.
     - Starting a classic or GTID binlog dump command.
   * - ``BinlogEventHeader``
     - Timestamp, event type, server id, event size, log position, and flags.
     - Auditing event metadata independent of event family.
   * - ``BinlogEvent``
     - Header, raw/body bytes, checksum data, and decoded fields for supported
       event families.
     - Application-level event handling.
   * - ``BinlogRowChange``
     - Before/after row values plus raw row slices.
     - Row event consumers that need both decoded values and audit bytes.
   * - ``BinlogParser``
     - Stateful table-map-aware parser.
     - Parsing binlog packets outside a live ``Connection``.
   * - ``BinlogStream``
     - Pull-based ``Readable<BinlogEvent>`` for a live replication read.
     - Stream-shaped consumers using ``read`` or ``stream::event::Data``.

Choose an API
-------------

.. list-table:: Binlog API choice
   :header-rows: 1
   :widths: 26 37 37

   * - API
     - Use it for
     - Important behavior
   * - ``register_slave``
     - Optional classic replication registration.
     - Returns ``OkPacket`` and does not start a stream.
   * - ``binlog_dump``
     - Finite diagnostics and tests.
     - Returns ``std::vector<BinlogEvent>``; use ``max_events`` to bound reads.
   * - ``binlog_dump_each``
     - Continuous or large reads with callback stop control.
     - Does not accumulate all events; return ``false`` to stop.
   * - ``create_binlog_stream``
     - Typed object stream consumption.
     - Returns ``BinlogStream`` over ``polycpp::stream::Readable<BinlogEvent>``.
   * - ``parse_binlog_event_packet``
     - One standalone packet.
     - Stateless helper; row decoding that needs table maps is limited.
   * - ``BinlogParser::parse``
     - Packet fixtures, saved binlog packet tests, or custom transports.
     - Parser keeps table-map state until ``clear_table_map``.
   * - ``parse_gtid_set``
     - Validating or inspecting GTID set strings.
     - Returns source UUID intervals.

Server prerequisites
--------------------

The server must have binary logging enabled. Row event decoding requires row
binlog format. For MySQL 8 and recent MariaDB versions, use the current binary
log status command; older servers may require ``SHOW MASTER STATUS``.

.. code-block:: sql

   SELECT @@log_bin AS log_bin;
   SET SESSION binlog_format = 'ROW';
   SHOW BINARY LOG STATUS;

The replication client id in ``BinlogDumpOptions::server_id`` must be unique for
the connected server. Reusing the same id as another live replication client can
cause the server to disconnect one of them.

Choose a dump position
----------------------

A typical bounded read starts from the server's current binary log file and
position.

.. code-block:: cpp

   auto status = conn.query("SHOW BINARY LOG STATUS");

   polycpp::mysql2::BinlogDumpOptions dump;
   dump.filename = std::get<std::string>(status.rows[0].at("File"));
   dump.binlog_position = std::get<uint64_t>(status.rows[0].at("Position"));
   dump.server_id = 62002;
   dump.flags = polycpp::mysql2::constants::binlog_dump_flags::NON_BLOCK;
   dump.max_events = 64;

Use ``NON_BLOCK`` when the test should return after currently available events.
For continuous reads, leave blocking behavior enabled and stop through
``max_events``, callback return, stream destruction, or process shutdown logic.

Bounded reads
-------------

``binlog_dump`` returns a vector. It is simple and useful for fixtures, but it
should be bounded.

.. code-block:: cpp

   auto events = conn.binlog_dump(dump);
   for (const auto& event : events) {
       if (event.name == "QueryEvent") {
           (void)event.schema;
           (void)event.query;
       }
   }

For blocking dump reads, keep ``max_events`` finite. An unbounded blocking
vector read is not a production consumption model.

Callback-controlled reads
-------------------------

Use ``binlog_dump_each`` when events can be processed immediately and should not
be accumulated. Returning ``false`` stops the read and closes the replication
command stream.

.. code-block:: cpp

   dump.max_events = 0;
   std::size_t seen = conn.binlog_dump_each(dump,
       [](const polycpp::mysql2::BinlogEvent& event) {
           if (event.name == "WriteRowsEventV2") {
               for (const auto& change : event.row_changes) {
                   (void)change.after;
               }
           }
           return true;
       });

   (void)seen;

Typed binlog stream
-------------------

``create_binlog_stream`` returns ``BinlogStream``, a pull-based
``polycpp::stream::Readable<BinlogEvent>``. Consume chunks with ``read`` or the
polycpp typed stream data event.

.. code-block:: cpp

   auto stream = conn.create_binlog_stream(dump);
   while (auto event = stream.read()) {
       if (event->name == "RotateEvent") {
           (void)event->next_binlog;
           (void)event->next_position;
       }
   }

.. code-block:: cpp

   auto stream = conn.create_binlog_stream(dump);
   stream.on(polycpp::stream::event::Data,
       [](const polycpp::mysql2::BinlogEvent& event) {
           (void)event.name;
       });

Use this form when the surrounding code already expects a typed polycpp stream.
For simpler loops, ``binlog_dump_each`` is often easier to reason about.

Parse saved packets
-------------------

``BinlogParser`` is useful when tests or tools already have packet payloads from
another source. It preserves table-map state, which row events need in order to
interpret column types and metadata.

.. code-block:: cpp

   polycpp::mysql2::BinlogParser parser;
   for (const auto& packet : saved_packets) {
       auto event = parser.parse(packet);
       if (event.name == "TableMapEvent") {
           (void)event.table;
       }
   }

   parser.clear_table_map();

Use ``parse_binlog_event_packet`` only when one stateless packet is enough.

Handle decoded event families
-----------------------------

Typed parsing covers the event families most useful to tests and downstream
replication consumers:

- ``QueryEvent``: schema, query text, and status variables.
- ``RotateEvent``: next binlog filename and position.
- ``FormatDescriptionEvent``: binlog version, server version, and event header
  length metadata.
- ``XidEvent``: transaction id for commit boundaries.
- ``GTIDEvent`` and ``PreviousGTIDsEvent``: GTID source and interval metadata.
- ``TableMapEvent``: table id, schema, table, column types, metadata, and null
  bitmap.
- Common ``WriteRows`` / ``UpdateRows`` / ``DeleteRows`` events: row changes
  with decoded before/after values where the column family is supported.

Unsupported event families retain raw body bytes for audit. Unsupported lossy
row column types fail closed rather than inventing approximate values.

Temporal row values
-------------------

Row event decoding maps MySQL temporal row encodings to explicit C++ types when
those values appear in binlog rows:

- ``BinlogTime`` for TIME2 row values.
- ``BinlogDateTime`` for DATETIME2 row values.
- ``BinlogTimestamp`` for TIMESTAMP2 row values.

These types are part of the public ``Value`` variant. Normal query date/time
columns are still returned as strings; see :doc:`/guides/type-mapping` for the
query-result type policy.

Use GTID dumps
--------------

Set ``use_gtid`` and provide a GTID set when the server should start from GTID
state instead of a filename/position pair.

.. code-block:: cpp

   polycpp::mysql2::BinlogDumpOptions dump;
   dump.use_gtid = true;
   dump.gtid_set = "3E11FA47-71CA-11E1-9E33-C80AA9429562:1-19";
   dump.server_id = 62003;
   dump.max_events = 128;

   auto sources = polycpp::mysql2::parse_gtid_set(dump.gtid_set);
   auto events = conn.binlog_dump(dump);

   (void)sources;
   (void)events;

Lifecycle rules
---------------

A replication read reserves the connection. Normal commands are rejected while a
binlog stream is live. EOF, ``max_events``, callback stop, stream destruction,
or stream drop ends the replication read and closes the transport. A later
normal command reconnects through the stored connection options.

This behavior is intentionally stricter than ordinary query streams because a
MySQL replication command stream is not a normal command/response phase.

Testing strategy
----------------

Use real database e2e tests for replication behavior. A useful test matrix
includes:

- classic filename/position dump with ``NON_BLOCK``;
- GTID dump when the server supports GTID mode;
- row events for inserts, updates, and deletes;
- table-map changes across multiple tables;
- transaction boundaries through ``XidEvent``;
- temporal rows for TIME2, DATETIME2, and TIMESTAMP2;
- malformed or unsupported event packets through ``BinlogParser`` fixtures.

See :doc:`/guides/testing` and ``tests/e2e/README.md`` for the repository's
real-database test commands.
