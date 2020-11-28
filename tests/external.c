/**********************************************************************
* Copyright 2020 Shawn M. Chapla
*
* Permission is hereby granted, free of charge, to any person obtaining
* a copy of this software and associated documentation files (the
* "Software"), to deal in the Software without restriction, including
* without limitation the rights to use, copy, modify, merge, publish,
* distribute, sublicense, and/or sell copies of the Software, and to
* permit persons to whom the Software is furnished to do so, subject to
* the following conditions:
*
* The above copyright notice and this permission notice shall be
* included in all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
***********************************************************************/

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>

#include "sclisp-test.h"
#include "sclisp.h"

static void dummy_dtor(void *user)
{
    printf("Called dummy_dtor: %p\n", user);
}

static int native_toupper(const struct sclisp_func_api *api, void *user)
{
    char *str, *cursor;
    int res = api->arg_string(api, 0, &str);

    (void)user;

    if (res)
        return res;

    cursor = str;
    while (*cursor) {
        *cursor = toupper(*cursor);
        ++cursor;
    }

    res = api->return_string(api, str);
    api->cb->free_func(api->cb, str);

    if (res)
        return res;

    return SCLISP_OK;
}

static int native_system(const struct sclisp_func_api *api, void *user)
{
    char *str;
    int res = api->arg_string(api, 2, &str);

    (void)user;

    if (res)
        return res;

    system(str);

    api->cb->free_func(api->cb, str);
    return SCLISP_OK;
}

static int add_two(const struct sclisp_func_api *api, void *user)
{
    int res;
    long arg0 = 0;
    double arg1 = 0.0;

    (void)user;

    res = api->arg_integer(api, 0, &arg0);
    if (res) return res;
    res = api->arg_real(api, 1, &arg1);
    if (res) return res;

    res = api->return_real(api, arg0 + arg1);
    if (res) return res;

    return SCLISP_OK;
}

void sclisp_test_external(void)
{
    struct sclisp* s;
    const struct sclisp_scope_api *api;
    long integer = 0;
    double real = 0.0;
    char *string = NULL;

    printf("\n===START EXTERNAL TESTS===\n\n");

    sclisp_init(&s, NULL);

    sclisp_register_user_func(s, native_toupper, "toupper", (void*)0xbaddad,
            dummy_dtor);
    sclisp_eval(s, "(toupper \"foo bar bas\")");
    sclisp_repr(s);
    sclisp_register_user_func(s, NULL, "toupper", NULL, NULL);
    sclisp_eval(s, "(toupper \"foo bar bas\")");
    sclisp_repr(s);
    sclisp_register_user_func(s, native_system, "system", NULL, NULL);
    sclisp_eval(s, "(system ignore1 ignore2 \"ls\")");
    sclisp_repr(s);
    sclisp_eval(s, "(system ignore1 \"ls\")");
    sclisp_repr(s);
    sclisp_register_user_func(s, add_two, "add2", NULL, NULL);
    sclisp_eval(s, "(add2 5 7.5)");
    sclisp_repr(s);
    sclisp_eval(s, "(add2 7.5 5)");
    sclisp_repr(s);
    sclisp_eval(s, "(add2)");
    sclisp_repr(s);
    sclisp_eval(s, "(set foo 35.5)");
    sclisp_eval(s, "(add2 10 foo)");
    sclisp_repr(s);

    api = sclisp_get_scope_api(s);
    api->set_integer(api, "foo", 42);
    api->set_real(api, "bar", 7.77);
    api->set_string(api, "bas", "this is bas");
    sclisp_eval(s, "foo");
    sclisp_repr(s);
    sclisp_eval(s, "bar");
    sclisp_repr(s);
    sclisp_eval(s, "bas");
    sclisp_repr(s);

    api->get_integer(api, "foo", &integer);
    api->get_real(api, "bar", &real);
    api->get_string(api, "bas", &string);

    printf("integer: %ld\n", integer);
    printf("real: %f\n", real);
    printf("string: %s\n", string ? string : "");

    api->cb->free_func(api->cb, string);
    integer = 0;
    real = 0.0;
    string = NULL;

    api->get_integer(api, "bar", &integer);
    api->get_real(api, "bas", &real);
    api->get_string(api, "foo", &string);

    printf("integer: %ld\n", integer);
    printf("real: %f\n", real);
    printf("string: %s\n", string ? string : "");

    api->cb->free_func(api->cb, string);
    string = NULL;

    api->set_string(api, "foo", "0456");
    api->set_string(api, "bar", "0xff");
    api->get_integer(api, "foo", &integer);
    printf("integer: %ld\n", integer);
    api->get_integer(api, "bar", &integer);
    printf("integer: %ld\n", integer);

    sclisp_destroy(s);
}
