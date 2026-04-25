Quickstart
==========

This page walks through a minimal mysql2 program end-to-end. Copy the
snippet, run it, then jump to :doc:`../tutorials/index` for task-oriented
walkthroughs or :doc:`../api/index` for the full reference.

The first quickstart should be written after a real public API slice exists.

Full example
------------

.. code-block:: cpp

   #include <polycpp/mysql2/mysql2.hpp>

   int main() {
       return 0;
   }

Compile it with the same CMake wiring from :doc:`installation`:

.. code-block:: bash

   cmake -B build -G Ninja
   cmake --build build
   ./build/my_app

Expected output:

.. code-block:: text

   (no output)

What just happened
------------------

This generated placeholder proves the documentation tree builds. Replace it with the first real user workflow before public release.

Next steps
----------

- :doc:`../tutorials/index` - step-by-step walkthroughs of common tasks.
- :doc:`../guides/index` - short how-tos for specific problems.
- :doc:`../api/index` - every public type, function, and option.
- :doc:`../examples/index` - runnable programs you can drop into a sandbox.
