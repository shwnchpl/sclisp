=============
SCLisp README
=============

A simple C LISP. Designed to be easily embeddable. For compatibility,
implemented entirely in ANSI C-89.

Building
========

An SCLisp shared library and optional tests can be built with CMake
using the following commands.

.. code-block:: shell

    mkdir ./build && cd ./build
    cmake ..
    ccmake .. # Optional, to adjust configuration
    make

Aside from this, it is also possible to embed SCLisp into a C/C++
project by simply including sclisp.h in that project's include path and
building sclisp.c with the standards compliant compiler of your choice.

License
=======

SCLisp is the work of Shawn M. Chapla and is released under the MIT
License. For more details, please consult the LICENSE file.
