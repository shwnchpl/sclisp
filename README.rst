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

TODO: This actually probably isn't possible right now, without defining
a few extra variables for sclisp.c. Look into what exactly these
are (probably just the same variables used by the internal test code)
and document how they can/should be defined in order to use SCLisp in
this way.

Examples
========

An sclisp-repl session, (non-exhaustively) demonstrating language
capabilities:

.. code-block::

    SCLisp repl. Copyright 2020 Shawn M. Chapla.
    Linked against SCLisp version 0.2.0 (2000)

    sclisp> (set (map l f) (cond ((nil? l) nil) (#t (cons (f (car l)) (map (cdr l) f)))))
    <func>
    sclisp> (map (list 1.0 2 3.0) (lambda (x) (+ x 100)))
    (101.0 102 103.0)
    sclisp> (set some-variable (prompt "Anything you want: "))
    Anything you want: Like this?
    "Like this?"
    sclisp> some-variable
    "Like this?"
    sclisp> (println some-variable)
    Like this?
    nil
    sclisp> (* (+ 3 5) (- 3 4 5 6 (/ 1 7.0)))
    -97.142857

Invoking mktemp, open, write, and close from SCLisp:

.. code-block:: c

    #include "sclisp.h"

    #include <fcntl.h>
    #include <stdlib.h>
    #include <sys/stat.h>
    #include <unistd.h>

    /* Error checking omitted for brevity. */

    static int native_mktemp(const struct sclisp_func_api *api, void *user)
    {
        char *template;

        api->arg_string(api, 0, &template);
        api->return_string(api, mktemp(template));
        api->cb->free_func(api->cb, template);

        return SCLISP_OK;
    }

    static int native_open(const struct sclisp_func_api *api, void *user)
    {
        char *pathname;
        long flags, mode;

        api->arg_integer(api, 1, &flags);
        api->arg_integer(api, 2, &mode);
        api->arg_string(api, 0, &pathname);
        api->return_integer(api, open(pathname, flags, mode));
        api->cb->free_func(api->cb, pathname);

        return SCLISP_OK;
    }

    static int native_write(const struct sclisp_func_api *api, void *user)
    {
        long fd, count;
        char *buf;

        api->arg_integer(api, 0, &fd);
        api->arg_integer(api, 2, &count);
        api->arg_string(api, 1, &buf);
        api->return_integer(api, write(fd, buf, count));
        api->cb->free_func(api->cb, buf);

        return SCLISP_OK;
    }

    static int native_close(const struct sclisp_func_api *api, void *user)
    {
        long fd;

        api->arg_integer(api, 0, &fd);
        api->return_integer(api, close(fd));

        return SCLISP_OK;
    }

   int main(void)
   {
       struct sclisp* s;
       const struct sclisp_scope_api *api;
       int i = 0;
       const char *prog[] = {
           "(set path (mktemp \"/tmp/sclisp-XXXXXX\"))",
           "(println path)",
           "(set fd (open path (| O_WRONLY O_APPEND O_CREAT) 0644))",
           "(write fd \"Hello, World!\" 13)",
           "(close fd)",
           NULL
       };

       sclisp_init(&s, NULL);
       api = sclisp_get_scope_api(s);

       sclisp_register_user_func(s, native_mktemp, "mktemp", NULL, NULL);
       sclisp_register_user_func(s, native_open, "open", NULL, NULL);
       sclisp_register_user_func(s, native_write, "write", NULL, NULL);
       sclisp_register_user_func(s, native_close, "close", NULL, NULL);

       api->set_integer(api, "O_WRONLY", O_WRONLY);
       api->set_integer(api, "O_APPEND", O_APPEND);
       api->set_integer(api, "O_CREAT", O_CREAT);

       while (prog[i])
           sclisp_eval(s, prog[i++]);

       sclisp_destroy(s);

       return 0;
   }

Output from the above program:

.. code-block:: shell

   $ ./example
   /tmp/sclisp-dwpT5h
   $ cat /tmp/sclisp-dwpT5h && echo
   Hello, World!

License
=======

SCLisp is the work of Shawn M. Chapla and all shared library code,
tests, and other code, unless otherwise noted, is released under the MIT
License. For more details, please consult the LICENSE file.

The SCLisp REPL (found in the repl/ directory of this tree) links the
GNU Readline library and as such is released under the GNU General
Public License as published by the Free Software Foundation; either
version 2 of the License, or (at your option) any later version.

Since the code for multiple applications and a shared library all reside
in this repository, in the interest of clarity, all source files contain
explicit reference to the most permissive license under which they are
released.
