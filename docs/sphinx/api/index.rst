API Reference
=============

The public API lives in ``include/polycpp/mysql2/mysql2.hpp``.

This page is a compact symbol-oriented reference. If you are choosing which
class owns a workflow, start with :doc:`../tutorials/api-concepts`; it explains
the class model and API selection path before listing symbols.

Core types
----------

``ConnectionOptions``
    Host, port, credentials, database, charset, auth, connect attributes,
    multiple-statement, and TLS options.

``CommandOptions``, ``QueryOptions``, ``ExecuteOptions``
    Per-command SQL, values, attributes, and timeout configuration.

``SslOptions``
    TLS enablement, CA/certificate/key material, trust store loading, and
    certificate identity checks.

``Error``
    MySQL-aware error type with numeric code and SQL state when the server
    returned an ERR packet.

``Connection``
    Synchronous connection object for connect, query, prepared execution,
    transaction, ping, reset, and shutdown operations.

``PreparedStatement``
    Server-side prepared statement id plus parameter and column metadata.

``StatementCursor``
    Server-side prepared-statement cursor metadata used with ``fetch``.

``QueryAttributes``
    Named query metadata values sent when the server advertises support.

``ConnectionInfo``, ``TraceEvent``
    Typed connection and command trace event payloads.

``PoolOptions``, ``Pool``, ``PoolConnection``
    Synchronous RAII connection pooling.

``PoolClusterOptions``, ``PoolCluster``, ``PoolNamespace``
    Named pool cluster with wildcard selection, retry/offline/remove policy,
    and typed cluster events.

``ServerOptions``, ``ServerHandshakeOptions``, ``ServerAuthInfo``
    Adapted server protocol configuration and parsed client handshake data.

``ServerTlsOptions``
    Server-side certificate/key material for MySQL in-protocol TLS upgrade.

``Server``, ``ServerConnection``
    TCP server listener and per-client protocol connection with typed command
    events and OK/ERR/text-result writers.

``BinlogDumpOptions``, ``BinlogEvent``, ``BinlogStream``, ``BinlogParser``
    Replication command options, typed binlog event records, pull-based binlog
    stream, and table-map-aware parser state.

``BinlogDateTime``, ``BinlogTime``, ``BinlogTimestamp``
    Typed values used by decoded temporal binlog row events.

``QueryResult``, ``OkPacket``, ``Field``, ``Row``, ``Value``
    Result metadata and typed row values.

``RawRowView``, ``RawValueView``
    Callback-scoped packet byte views returned by ``Connection::query_each_raw``
    for high-throughput one-pass text-result scans. The byte views must not be
    retained after the callback returns.

``RawSql``
    Explicit marker used by ``raw`` to bypass escaping in formatter input.

Free functions
--------------

``create_connection``
    Construct and connect a ``Connection``.

``create_connection_promise``
    Promise wrapper for constructing and connecting a ``Connection``.

``create_pool``
    Construct a ``Pool``.

``create_pool_cluster``
    Construct a ``PoolCluster``.

``create_server``
    Construct an adapted MySQL protocol server.

``query``
    One-shot connect/query/end helper.

``query_promise``
    Promise wrapper for one-shot connect/query/end.

``escape``, ``escape_id``, ``format``, ``format_named``, ``raw``
    SQL formatting helpers.

``value_to_json``, ``row_to_json_line``
    JSON conversion helpers used by ``QueryResult::to_json`` and
    ``query_stream_json``.

``parse_connection_uri``
    Convert a ``mysql://`` or ``mysqls://`` URI into ``ConnectionOptions``.

``get_charset_number``, ``get_charset_encoding``
    Helpers for MySQL charset/collation ids and encoding names.

``ssl_profile_names``, ``ssl_profile_ca_pems``
    Access bundled TLS profile CA data, including the ``Amazon RDS`` profile.

``set_max_parser_cache``, ``max_parser_cache``, ``clear_parser_cache``
    Parser-cache compatibility hooks. C++ uses static parsers, so
    ``clear_parser_cache`` has no generated JavaScript parser cache to clear.

``parse_binlog_event_packet``, ``parse_gtid_set``
    Standalone binlog packet and GTID-set parsing helpers.

Generated namespace reference
-----------------------------

.. doxygennamespace:: polycpp::mysql2
   :members:
