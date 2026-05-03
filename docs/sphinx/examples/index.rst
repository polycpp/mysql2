Examples
========

Examples are built only when ``POLYCPP_MYSQL2_BUILD_EXAMPLES=ON`` is passed to
CMake.

Build examples
--------------

.. code-block:: bash

   cmake -B build -DPOLYCPP_MYSQL2_BUILD_EXAMPLES=ON
   cmake --build build --target simple_query raw_scan

simple_query
------------

``examples/simple_query.cpp`` connects with ``MYSQL2_TEST_*`` environment
variables and prints a scalar query result.

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=test \
   ./build/examples/simple_query

raw_scan
--------

``examples/raw_scan.cpp`` uses ``Connection::query_each_raw`` to scan a
recursive integer result set without constructing typed ``Row`` / ``Value``
objects for each row.

.. code-block:: bash

   MYSQL2_TEST_HOST=127.0.0.1 \
   MYSQL2_TEST_PORT=3306 \
   MYSQL2_TEST_USER=root \
   MYSQL2_TEST_PASSWORD=secret \
   MYSQL2_TEST_DATABASE=test \
   MYSQL2_EXAMPLE_LIMIT=1000 \
   ./build/examples/raw_scan
