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

#include "sclisp-test.h"
#include "sclisp.h"

/* Directly include SCLisp source code so it is possible to access all
   internal functions. */
#include "sclisp.c"

/***************************************************
 * Internal test utility functions/callbacks
 **************************************************/

static struct Object* builtin_printargs(struct Object *args, void *user)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *car;

    while ((car = internal_car(args)) || args) {
        sc_printf(s->cb, SCLISP_STDOUT, "GOT ARG: %p\n", car);
        args = internal_cdr(args);
    }

    return NULL;
}

static struct Object* builtin_printx(struct Object *args, void *user)
{
    struct Object *x;
    struct sclisp *s = (struct sclisp *)user;
    int res = scope_query(s->scope, "x", &x);

    (void)args;

    if (res) {
        printf("UNEXPECTED: builtin_printx - res=%d\n", res);
        return NULL;
    }

    printf("x: %s\n", internal_repr(s, x));

    return NULL;
}

static const char* TokenTag_to_str(enum TokenTag tag)
{
    switch (tag) {
        case TOK_INTEGER: return "TOK_INTEGER";
        case TOK_REAL: return "TOK_REAL";
        case TOK_STRING: return "TOK_STRING";
        case TOK_SYMBOL: return "TOK_SYMBOL";
        case TOK_NIL: return "TOK_NIL";
        case TOK_LPAREN: return "TOK_LPAREN";
        case TOK_RPAREN: return "TOK_RPAREN";
        case TOK_QUOTE: return "TOK_QUOTE";
        case TOK_UNKOWN: return "TOK_UNKOWN";
    }

    return "UNDEFINED";
}

static const char* TokenContents_to_str(struct Token *tok)
{
    static char buf[128];

    switch (tok->tag) {
        case TOK_INTEGER:
            snprintf(buf, sizeof(buf), "%ld", tok->data.integer);
            return buf;
        case TOK_REAL:
            snprintf(buf, sizeof(buf), "%f", tok->data.real);
            return buf;
        case TOK_STRING:
            snprintf(buf, sizeof(buf), "\"%s\"", tok->data.str);
            return buf;
        case TOK_SYMBOL:
            return tok->data.str;
        case TOK_NIL:
            return "nil";
        case TOK_LPAREN:
            return "(";
        case TOK_RPAREN:
            return ")";
        case TOK_QUOTE:
            return "'";
        case TOK_UNKOWN:
            return "(unknown)";
    }

    return "UNDEFINED";
}

/***************************************************
 * Internal test main function
 **************************************************/

void sclisp_test_internal(void)
{
    /* TODO: Internal testing. Refactor into several functions. */

    /* FIXME: There is virtually NO memory management in this function.
       It's okay for now since it's just test code, but really this
       should be fixed. */

    struct Object *obj = NULL;
    int res;
    struct Object *one;
    struct Object *two;
    struct Object *three;
    struct Object *list;
    struct Object *builtin_pa;
    struct Object *builtin_px;
    struct Token *tok;
    struct Object *nil_list;
    struct Object *four;
    struct Object *func;
    struct Object *parsed_obj;
    char *repr;
    struct sclisp *_s = NULL;

    sclisp_init(&_s, NULL);
    if (!_s) {
        printf("internal tests: sclisp_init failed\n");
        return;
    }

    scope_set(_s, _s->scope, "foo", NULL);

    res = scope_query(_s->scope, "foo", &obj);
    sc_printf(_s->cb, SCLISP_STDOUT, "'foo' -> (%d, %p)\n", res, obj);
    res = scope_query(_s->scope, "bar", &obj);
    sc_printf(_s->cb, SCLISP_STDOUT, "'bar' -> (%d, %p)\n", res, obj);
    res = scope_query(_s->scope, "bas", &obj);
    sc_printf(_s->cb, SCLISP_STDOUT, "'bas' -> (%d, %p)\n", res, obj);
    res = scope_query(_s->scope, "foo", &obj);
    sc_printf(_s->cb, SCLISP_STDOUT, "'foo' -> (%d, %p)\n", res, obj);

    three = some_integer(_s, 3);
    two = some_integer(_s, 2);
    one = some_integer(_s, 1);

    list = internal_cons(_s, three, NULL);
    list = internal_cons(_s, two, list);
    list = internal_cons(_s, one, list);

    sc_printf(_s->cb, SCLISP_STDOUT, "%p %p\n", one, internal_car(list));
    sc_printf(_s->cb, SCLISP_STDOUT, "%p %p\n", two, internal_car(internal_cdr(list)));
    sc_printf(_s->cb, SCLISP_STDOUT, "%p %p\n", three, internal_car(internal_cdr(internal_cdr(list))));
    sc_printf(_s->cb, SCLISP_STDOUT, "%p\n", internal_car(internal_cdr(internal_cdr(internal_cdr(list)))));
    sc_printf(_s->cb, SCLISP_STDOUT, "%p %p\n", one, internal_car(one));

    builtin_pa = some_builtin(_s, builtin_printargs, _s, NULL);
    list = internal_cons(_s, builtin_pa, list);
    internal_eval(_s, list);

    /* TODO: variable used here to make warnings shut up; unclear
       why it isn't needed elsewhere. */
    repr = internal_repr(_s, list);
    printf("list with builtin: %s\n", repr ? repr : "NULL");

    scope_set(_s, _s->scope, "printargs", builtin_pa);
    list = internal_cons(_s, some_symbol(_s, "printargs"), internal_cdr(list));
    internal_eval(_s, list);

    func = some_function(_s, NULL, internal_cons(_s, list, internal_cons(_s, list, NULL)));
    printf("User defined function: %p\n", (void*)func);
    internal_eval(_s, internal_cons(_s, func, NULL));

    printf("START PX TEST\n");
    builtin_px = some_builtin(_s, builtin_printx, _s, NULL);
    do {
        struct Object *fortytwo = some_integer(_s, 42);
        struct Object *sixpointnine = some_real(_s, 6.9);
        struct Object *pxwith = some_function(_s, internal_cons(_s, some_symbol(_s, "x"), NULL),
                internal_cons(_s, internal_cons(_s, builtin_px, NULL), internal_cons(_s, fortytwo, NULL)));
        struct Object *obj_res;

        scope_set(_s, _s->scope, "pxwith", pxwith);
        obj_res = internal_eval(_s, internal_cons(_s, pxwith, fortytwo));
        printf("Obj res was: %s\n", internal_repr(_s, obj_res));
        obj_res = internal_eval(_s, internal_cons(_s, pxwith, sixpointnine));
        printf("Obj res was: %s\n", internal_repr(_s, obj_res));
        obj_res = internal_eval(_s, internal_cons(_s, pxwith, NULL));
        printf("Obj res was: %s\n", internal_repr(_s, obj_res));

        obj_res = internal_eval(_s, parse_expr(_s, "(pxwith (pxwith (pxwith 33)))"));
        printf("Obj res was: %s\n", internal_repr(_s, obj_res));
    } while (0);
    printf("END PX TEST\n");

    tok = lex_expr(_s, "(foo bar () nil 3.5 55 ' \"\" '(bas) \"one ( two       3))))\"315.3e7)");
    while (tok) {
        sc_printf(_s->cb, SCLISP_STDOUT, "    GOT TOK: %s - %s\n", TokenTag_to_str(tok->tag), TokenContents_to_str(tok));
        tok = tok->next;
    }

    printf("printargs list: %s\n", internal_repr(_s, list));
    printf("one list: %s\n", internal_repr(_s, one));
    nil_list = internal_cons(_s, NULL, NULL);
    printf("nil list: %s\n", internal_repr(_s, nil_list));
    printf("list beginning with nil list: %s\n", internal_repr(_s, internal_cons(_s, nil_list, list)));

    four = some_integer(_s, 4);
    four = internal_cons(_s, four, nil_list);
    printf("four cons nil list: %s\n", internal_repr(_s, four));
    list = internal_cons(_s, four, list);
    printf("list within a list: %s\n", internal_repr(_s, list));
    list = internal_cons(_s, NULL, list);
    printf("list starting with nil: %s\n", internal_repr(_s, list));
    list = internal_cons(_s, some_real(_s, 420.69), list);
    list = internal_cons(_s, some_string(_s, "foo bar bas"), list);
    printf("big ol' list: %s\n", internal_repr(_s, list));
    printf("improper list: %s\n",
            internal_repr(_s, internal_cons(_s, some_integer(_s, 42),
                    internal_cons(_s, some_integer(_s, 420), some_integer(_s, 69)))));

    parsed_obj = parse_expr(_s, "(printargs one two three () (printargs nil) nil two)");
    printf("parsed_obj=%p\n", (void*)parsed_obj);
    printf("parsed_obj repr: %s\n", internal_repr(_s, parsed_obj));
    internal_eval(_s, parsed_obj);

    sclisp_destroy(_s);
}
