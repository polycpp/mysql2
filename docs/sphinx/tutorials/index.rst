Tutorials
=========

Tutorials explain workflows and ownership decisions. They are intentionally
longer than quick recipes: each page centers on one topic and explains when to
use the related classes and APIs.

Recommended Reading Paths
-------------------------

Application developer
   Start with :doc:`api-concepts`, then read :doc:`query-and-prepared`,
   :doc:`pooling`, :doc:`/guides/connection-configuration`, and
   :doc:`/guides/lifecycle-and-safety`.

Service or backend developer
   Read :doc:`pooling`, :doc:`query-and-prepared`,
   :doc:`/guides/sql-formatting`, :doc:`/guides/tls`, and
   :doc:`/guides/testing`.

Performance-sensitive reader
   Read :doc:`raw-row-scans`, then :doc:`/guides/raw-row-scans` and
   :doc:`/guides/benchmarking`.

Protocol fixture or replication tooling
   Read :doc:`server-mode` or :doc:`binlog-replication`, then use the generated
   :doc:`/api/index` for exact event and response writer signatures.

Tutorial Map
------------

.. list-table:: Tutorial topics
   :header-rows: 1
   :widths: 28 42 30

   * - Tutorial
     - What it teaches
     - Read it when
   * - :doc:`api-concepts`
     - Class model, API selection, and ownership rules.
     - You need a mental model before writing non-trivial code.
   * - :doc:`query-and-prepared`
     - Text queries, prepared statements, query attributes, timeouts, cursors,
       multi-result handling, transactions, values, errors, and traces.
     - You are implementing ordinary application database work.
   * - :doc:`raw-row-scans`
     - ``RawRowView`` / ``RawValueView`` lifetimes, manual parsing, and row API
       selection.
     - You need one-pass scan speed and can parse/copy inside callbacks.
   * - :doc:`pooling`
     - ``Pool``, ``PoolConnection``, reset-on-release, contention policy,
       pool events, and ``PoolCluster`` selection.
     - You are building service code with reusable connections.
   * - :doc:`server-mode`
     - Adapted MySQL protocol server classes, command events, response writers,
       prepared statement fixtures, TLS, and shutdown.
     - You need a protocol fixture, proxy surface, or controlled fake server.
   * - :doc:`binlog-replication`
     - Replication command ownership, bounded reads, callback reads,
       ``BinlogStream``, ``BinlogParser``, event families, temporal values, and
       GTID usage.
     - You are consuming binary log packets or writing replication tests.

.. toctree::
   :maxdepth: 1

   api-concepts
   query-and-prepared
   raw-row-scans
   pooling
   server-mode
   binlog-replication
