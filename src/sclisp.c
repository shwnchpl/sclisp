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

#include "sclisp.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if SCLISP_FMOD_SUPPORT
    #include <math.h>
#endif

/********************************************************************
 * Master TODO List
 ********************************************************************
 * NICE TO HAVE:
 *  - Implement builtins for bitwise operations.
 *  - Implement more functional builtins (ie, map, foldl, foldr,
 *    filter, etc.).
 *  - Verify that user-builtins and  user scope API functions set
 *    (or don't set) last error in a consistent way.
 *  - Add "del" builtin to remove a scope binding. Maybe setting a
 *    binding to "nil" should also have this functionality?
 *  - Implement macros, possibly just as functions whose arguments
 *    are not evaluated and who then auto-eval returned code?
 *  - Support coercion from string to integer or float for math
 *    builtins.
 *  - Make comparison promotion (integer->real->float) sane. For
 *    instance, currently 3 == 3.0 and 3.0 == "3.0" but 3 != "3.0".
 *  - Add closure support for lambdas.
 *  - Add some form of boxed type to allow for interior mutability
 *    (ie, immutable container, mutable contents; something like a
 *    Rust RefCell).
 *  - Make lasterror and error message available within LISP code.
 *  - Add support for comments (with ;). In a way, this already works
 *    because everything after the first s-expression is ignored when
 *    parsing.
 *  - Add variadic function support and/or incorrect argc handling.
 *  - Check function argument syntax on definition.
 *  - Add/improve unit tests.
 *  - Clean up memory management in internal tests.
 *  - Add some sort of assert mechanism?
 *  - Token/builtin to get result of last expression (similar to
 *    Python's _).
 *  - Run through Valgrind or something (or at least profile with
 *    custom allocators) to ensure there are no unknown leaks bopping
 *    around.
 *  - Error handling for apply_builtins.
 *  - Clean up parsing/lexing, including handling incomplete parens
 *    etc.
 *  - Determine if nil should be allowed as an argument to user
 *    builtin functions.
 *
 * EVENTUALLY:
 *  - Document everything.
 *  - Add support for escape sequences when printing (at least \n).
 *  - Improved builtin data structures (ie, array and hashmap). The
 *    hashmap implementation in my old sJSON code could/should do the
 *    trick and could probably be used to improve performance on
 *    scopes as well.
 *  - Add support for tab-completion for symbols in scope within
 *    the repl.
 *  - Provide some mechanism for accessing builtins even if they are
 *    completely out of scope. Perhaps builtins should exist in an
 *    immutable parent scope? Perhaps there is a "builtin" builtin
 *    that exists in that scope and that can be passed a string
 *    (or something?) to get a reference to manually overwritten
 *    builtins? Then again, if you want to shoot yourself in the foot,
 *    maybe it's better if I just let you.
 *  - Better error reporting for user defined functions (ie. don't
 *    clobber internal error messages that have been set in wrapper
 *    etc.).
 *  - Make it possible to call sclisp functions *from* C code (while
 *    passing C style arguments), somehow.
 *  - Implement builtins for string construction and manipulation.
 ********************************************************************/

/***************************************************
 * Utility macros/constants
 **************************************************/

#define MIN_(x, y)  ((x) < (y) ? (x) : (y))
#define PPSTR_(s)   #s
#define PPSTR(s)    PPSTR_(s)

#define SC_UPPERCASE_integer    INTEGER
#define SC_UPPERCASE_string     STRING
#define SC_UPPERCASE(_s)        SC_UPPERCASE_##_s

#define SCLISP_STATIC_MAGIC     (-0x50c1ab1e)
#define SCLISP_TRANSIENT_MAGIC  (-0xcabfaded)

/***************************************************
 * Data structures
 **************************************************/

struct Object;

struct Function {
    /* TODO: Add some kind of lexical scope capture to make
       these into closures. Lexical scope should be captured
       as immutable, while the global scope should remain
       mutable. Labmdas should not have access to lexical
       scope where they are being invoked; only global scope
       and the lexical scope they were created with. This
       can also be used to implement currying by having
       partial functions return a new closure where the
       args that have been applied are stored in the
       immutable lexical capture. */
    struct Object *args;
    struct Object *body;
};

struct Builtin {
    struct Object* (*func)(struct Object *, void *);
    void *user;
    void (*dtor)(void*);
};

enum AtomTag {
    INTEGER,
    REAL,
    STRING,
    SYMBOL,
    FUNCTION,
    BUILTIN
};

struct Atom {
    enum AtomTag tag;
    union {
        long integer;
        double real;
        char *string;
        char *symbol;
        struct Function function;
        struct Builtin builtin;
    } a;
};

struct Cell {
    struct Object *car;
    struct Object *cdr;
};

enum ObjectTag {
    ATOM,
    CELL
};

struct Object {
    enum ObjectTag tag;
    union {
        struct Atom atom;
        struct Cell cell;
    } o;
    long ref;
};

struct Binding {
    char *symbol;
    struct Object *object;
    struct Binding *next;
};

struct Scope {
    struct Scope *parent;
    struct Binding *binding;
};

struct UserFunc {
    int (*func)(const struct sclisp_func_api *api, void *user);
    void (*dtor)(void *user);
    void *user;
    struct sclisp *s;
};

struct sclisp {
    struct sclisp_cb *cb;
    struct Scope *scope;
    struct Object *lr; /* last result */
    int le; /* last error */
    const char *errmsg;
    struct sclisp_scope_api usapi;
};

/***************************************************
 * Error reporting macros
 **************************************************/

#define SCLISP_REPORT_ERR(_s, _e, _msg) \
    do {                                \
        (_s)->le = _e;                  \
        (_s)->errmsg = _msg;            \
    } while (0)
#define SCLISP_REPORT_BUG(_s,_msg)      SCLISP_REPORT_ERR(_s, SCLISP_BUG, _msg)
#define SCLISP_ERR_REPORTED(_s)         ((_s)->le)

#define ON_ERR_UNREF1_THEN(_s, _a1, _stmt)  \
    do {                                    \
        if (SCLISP_ERR_REPORTED(_s)) {      \
            object_unref(_s->cb, _a1);      \
            _stmt;                          \
        }                                   \
    } while (0)

#define ON_ERR_UNREF2_THEN(_s, _a1, _a2, _stmt) \
    do {                                        \
        if (SCLISP_ERR_REPORTED(_s)) {          \
            object_unref(_s->cb, _a1);          \
            object_unref(_s->cb, _a2);          \
            _stmt;                              \
        }                                       \
    } while (0)

/***************************************************
 * Identity macros
 **************************************************/

#define is_atom(p)          ((p) && (p)->tag == ATOM)
#define is_cell(p)          ((p) && (p)->tag == CELL)
#define is_nil(p)           (!(p))
#define is_integer(p)       (is_atom((p)) && (((p)->o.atom.tag == INTEGER)))
#define is_real(p)          (is_atom((p)) && (((p)->o.atom.tag == REAL)))
#define is_numeric_atom(p)  \
    (is_atom((p)) && ((p)->o.atom.tag == INTEGER || \
                      (p)->o.atom.tag == REAL))
#define is_numeric_zero(p)  \
    (is_atom((p)) && (((p)->o.atom.tag == INTEGER &&    \
                       !(p)->o.atom.a.integer) ||       \
                      ((p)->o.atom.tag == REAL &&       \
                       (p)->o.atom.a.real == 0.0)))
#define is_symbol(p)        (is_atom(p) && (p)->o.atom.tag == SYMBOL)
#define is_string(p)        (is_atom(p) && (p)->o.atom.tag == STRING)
#define is_false(p)         (is_nil(p) || is_numeric_zero(p))
#define is_true(p)          (!is_false(p))
#define is_dynamic_obj(p)   \
    ((p) && (p)->ref != SCLISP_STATIC_MAGIC && (p)->ref != SCLISP_STATIC_MAGIC)

/***************************************************
 * Static instances
 **************************************************/

#define _static_atom_list() \
/* SC_STATIC_TRUE */        _static_atom(integer, TRUE, 1);                 \
/* SC_STATIC_FALSE */       _static_atom(integer, FALSE, 0);                \
/* SC_STATIC_CELL_STR */    _static_atom(string, CELL_STR, "cell");         \
/* SC_STATIC_INTEGER_STR */ _static_atom(string, INTEGER_STR, "integer");   \
/* SC_STATIC_REAL_STR */    _static_atom(string, REAL_STR, "real");         \
/* SC_STATIC_STRING_STR */  _static_atom(string, STRING_STR, "string");     \
/* SC_STATIC_SYMBOL_STR */  _static_atom(string, SYMBOL_STR, "symbol");     \
/* SC_STATIC_FUNCTION_STR */_static_atom(string, FUNCTION_STR, "function"); \
/* SC_STATIC_BUILTIN_STR */ _static_atom(string, BUILTIN_STR, "builtin");   \
/* SC_STATIC_NIL_STR */     _static_atom(string, NIL_STR, "nil")

#define _static_atom(_type, _name, _val)    \
    static struct Object sc_##_name##_instance = {  \
        ATOM,                                       \
        {{                                          \
            SC_UPPERCASE(_type),                    \
            { (long) _val }                         \
        }},                                         \
        SCLISP_STATIC_MAGIC                         \
    };                                              \
    static struct Object *const SC_STATIC_##_name = &sc_##_name##_instance

_static_atom_list();

#undef _static_atom

static void sc_lazy_static(void)
{
    static int once = 0;

    /* XXX: Nasty hack to get around the fact that C89 doesn't
       have any support for statically initializing anything but
       the first union field, and the standard *technically* doesn't
       guarantee that assigning to one union field will make the
       same data available in another (although, in practice, it
       more or less implicitly does, since those fields must share
       the same address). To be extra safe, we waste cycles by
       calling this function whenever an API is invoked. */
    if (once)
        return;

    #define _static_atom(_type, _name, _val)    \
        SC_STATIC_##_name->o.atom.a._type = _val

    _static_atom_list();

    #undef _static_atom
}

#undef _static_atom_list

/***************************************************
 * Memory management functions
 **************************************************/

static struct Object* object_ref(struct Object *obj)
{
    if (!is_dynamic_obj(obj))
        /* FIXME: Unsafe code; clone if transient. */
        return obj;

    obj->ref += 1;

    return obj;
}

static void object_unref(struct sclisp_cb *cb, struct Object *obj)
{
    if (!is_dynamic_obj(obj))
        return;

    if (obj->ref <= 0) {
        /* TODO: Assert in some way, this should never happen. */
        return;
    }

    --obj->ref;

    if (!obj->ref) {
        if (is_atom(obj)) switch (obj->o.atom.tag) {
            case STRING:
                cb->free_func(cb, obj->o.atom.a.string);
                break;
            case SYMBOL:
                cb->free_func(cb, obj->o.atom.a.symbol);
                break;
            case FUNCTION:
                object_unref(cb, obj->o.atom.a.function.args);
                object_unref(cb, obj->o.atom.a.function.body);
                break;
            case BUILTIN:
                if (obj->o.atom.a.builtin.dtor)
                    obj->o.atom.a.builtin.dtor(obj->o.atom.a.builtin.user);
                break;
            default:
                break;
        } else {
            object_unref(cb, obj->o.cell.car);
            object_unref(cb, obj->o.cell.cdr);
        }
        cb->free_func(cb, obj);
    }
}

/***************************************************
 * Utility functions
 **************************************************/

static void sc_printf(struct sclisp_cb *cb, int fd, const char *fmt, ...)
{
    char buf[128];
    va_list args;

    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    cb->print_func(cb, fd, buf);
}

static char* sc_strdup(struct sclisp_cb *cb, const char *str)
{
    char *dupstr = cb->zalloc_func(cb, strlen(str) + 1);
    if (dupstr)
        strcpy(dupstr, str);
    return dupstr;
}

static char* sc_getline(struct sclisp *s)
{
    char c;
    unsigned long off = 0, sz = 64;
    char *buf;

    /* getchar_func is optional */
    if (!s->cb->getchar_func) {
        SCLISP_REPORT_ERR(s, SCLISP_UNSUPPORTED, NULL);
        return NULL;
    }

    buf = s->cb->zalloc_func(s->cb, sz);
    if (!buf) {
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
        return NULL;
    }

    /* TODO: Handle other types of newlines. Also, is EOF check even
       necessary? */
    while ((c = s->cb->getchar_func(s->cb)) != '\n' && c != EOF) {
        buf[off++] = c;
        if (off >= sz) {
            char *nbuf = s->cb->zalloc_func(s->cb, sz * 2);
            if (!nbuf) {
                s->cb->free_func(s->cb, buf);
                SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
                return NULL;
            }
            memcpy(nbuf, buf, sz * 2);
            s->cb->free_func(s->cb, buf);
            buf = nbuf;
            sz *= 2;
        }
    }

    return buf;
}

int sc_scan_integer(const char *buf, long *out)
{
    int used = 0;
    return sscanf(buf, "%li%n", out, &used) == 1 && used == (int)strlen(buf);
}

int sc_scan_real(const char *buf, double *out)
{
    int used = 0;
    return sscanf(buf, "%lf%n", out, &used) == 1 && used == (int)strlen(buf);
}

/***************************************************
 * Scope handling functions
 **************************************************/

static int scope_query(struct Scope *scope, const char *sym,
        struct Object **obj)
{
    for (; scope; scope = scope->parent) {
        struct Binding *b;
        for (b = scope->binding; b; b = b->next)
            if (!strcmp(b->symbol, sym)) {
                *obj = object_ref(b->object);
                return SCLISP_OK;
            }
    }
    return SCLISP_ERR;
}

static void scope_set(struct sclisp *s, struct Scope *scope,
        const char *sym, struct Object *obj)
{
    struct Binding *binding;

    /* Only innermost scope is mutable. Parent scopes cannot be
       modified in any way. */
    for (binding = scope->binding; binding; binding = binding->next)
        if (!strcmp(binding->symbol, sym)) {
            object_unref(s->cb, binding->object);
            binding->object = object_ref(obj);
            return;
        }

    binding = s->cb->zalloc_func(s->cb, sizeof(*binding));
    if (!binding) {
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
        return;
    }

    binding->symbol = sc_strdup(s->cb, sym);
    if (!binding->symbol) {
        s->cb->free_func(s->cb, binding);
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
        return;
    }

    binding->object = object_ref(obj);
    binding->next = scope->binding;
    scope->binding = binding;
}

void scope_free(struct sclisp_cb *cb, struct Scope *scope)
{
    struct Binding *binding = scope->binding;

    while (binding) {
        struct Binding *next = binding->next;
        object_unref(cb, binding->object);
        cb->free_func(cb, binding->symbol);
        cb->free_func(cb, binding);
        binding = next;
    }

    cb->free_func(cb, scope);
}

/* TODO: Add scope_unset, which will be needed for "del" builtin
   and potential (set foo nil) optimization, etc. */

static struct Object* internal_car(struct Object *obj);
static struct Object* internal_cdr(struct Object *obj);
static struct Object* internal_eval(struct sclisp *s, struct Object *obj);

static void scope_enter_with(struct sclisp *s, struct Object *symbols,
        struct Object *bindings)
{
    struct Scope *child = s->cb->zalloc_func(s->cb, sizeof(*child));
    struct Object *s_car, *b_car, *s_cdr, *b_cdr;

    if (!child) {
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
        return;
    }

    for (s_car = internal_car(symbols), s_cdr = internal_cdr(symbols),
            b_car = internal_car(bindings), b_cdr = internal_cdr(bindings);
            (s_car != NULL || s_cdr != NULL) &&
                    (b_car != NULL || b_cdr != NULL);
            s_car = internal_car(s_cdr), s_cdr = internal_cdr(s_cdr),
            b_car = internal_car(b_cdr), b_cdr = internal_cdr(b_cdr)) {
        if (!is_atom(s_car) || s_car->o.atom.tag != SYMBOL) {
            scope_free(s->cb, child);
            SCLISP_REPORT_BUG(s, "BUG - requested binding to non-symbol");
            return;
        }

        scope_set(s, child, s_car->o.atom.a.symbol, internal_eval(s, b_car));
        if (SCLISP_ERR_REPORTED(s)) {
            /* Big error in little China. */
            scope_free(s->cb, child);
            return;
        }
    }

    /* TODO: Error out on too many arguments and do something to
       indicate a curry here if too few. Implement currying here
       if symbols indicates a function of an arity greater than
       the number of arguments provided. */

    child->parent = s->scope;
    s->scope = child;
}

static void scope_pop_to_parent(struct sclisp *s, struct Scope **scope)
{
    struct Scope *tmp;

    if (!(*scope)->parent) {
        SCLISP_REPORT_BUG(s, "BUG - attempted to pop root scope");
    }

    tmp = *scope;
    *scope = (*scope)->parent;
    scope_free(s->cb, tmp);
}

/***************************************************
 * Atomic constructors
 **************************************************/

static struct Object* some_integer(struct sclisp *s, long val)
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    if (obj) {
        obj->tag = ATOM;
        obj->o.atom.tag = INTEGER;
        obj->o.atom.a.integer = val;
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

static struct Object* some_real(struct sclisp *s, double val)
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    if (obj) {
        obj->tag = ATOM;
        obj->o.atom.tag = REAL;
        obj->o.atom.a.real = val;
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

static struct Object* some_string(struct sclisp *s, const char* val)
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    if (obj) {
        char* dv = sc_strdup(s->cb, val);
        if (!dv) {
            s->cb->free_func(s->cb, obj);
            SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
            return NULL;
        }

        obj->tag = ATOM;
        obj->o.atom.tag = STRING;
        obj->o.atom.a.string = dv;
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

static struct Object* some_symbol(struct sclisp *s, const char* val)
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    if (obj) {
        char* dv = sc_strdup(s->cb, val);
        if (!dv) {
            s->cb->free_func(s->cb, obj);
            SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
            return NULL;
        }

        obj->tag = ATOM;
        obj->o.atom.tag = SYMBOL;
        obj->o.atom.a.symbol = dv;
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

static struct Object* some_function(struct sclisp *s, struct Object *args,
        struct Object *body)
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    /* TODO: Check that args is either nil or a list of symbols. */

    if (obj) {
        obj->tag = ATOM;
        obj->o.atom.tag = FUNCTION;
        obj->o.atom.a.function.args = object_ref(args);
        obj->o.atom.a.function.body = object_ref(body);
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

static struct Object* some_builtin(struct sclisp *s,
        struct Object* (*func)(struct Object *, void *), void *user,
        void (*dtor)(void*))
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    if (obj) {
        obj->tag = ATOM;
        obj->o.atom.tag = BUILTIN;
        obj->o.atom.a.builtin.func = func;
        obj->o.atom.a.builtin.user = user;
        obj->o.atom.a.builtin.dtor = dtor;
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

/***************************************************
 * Object coercion functions
 **************************************************/

#define OBJECT_AS_NUMERIC_FUNC(_n, _t)  \
    static int object_as_##_n(struct Object *obj, _t *out)                  \
    {                                                                       \
        if (!out)                                                           \
            return SCLISP_BADARG;                                           \
                                                                            \
        if (is_nil(obj)) {                                                  \
            *out = 0;                                                       \
            return SCLISP_OK;                                               \
        }                                                                   \
                                                                            \
        if (is_integer(obj)) {                                              \
            *out = obj->o.atom.a.integer;                                   \
            return SCLISP_OK;                                               \
        }                                                                   \
                                                                            \
        if (is_real(obj)) {                                                 \
            *out = obj->o.atom.a.real;                                      \
            return SCLISP_OK;                                               \
        }                                                                   \
                                                                            \
        if (is_string(obj)) {                                               \
            double real = 0.0;                                              \
            long integer = 0;                                               \
                                                                            \
            if (sc_scan_integer(obj->o.atom.a.string, &integer)) {          \
                *out = integer;                                             \
                return SCLISP_OK;                                           \
            }                                                               \
                                                                            \
            if (sc_scan_real(obj->o.atom.a.string, &real)) {                \
                *out = real;                                                \
                return SCLISP_OK;                                           \
            }                                                               \
                                                                            \
            return SCLISP_UNSUPPORTED;                                      \
        }                                                                   \
                                                                            \
        return SCLISP_UNSUPPORTED;                                          \
    }

OBJECT_AS_NUMERIC_FUNC(integer, long)
OBJECT_AS_NUMERIC_FUNC(real, double)

#undef OBJECT_AS_NUMERIC_FUNC

static char* internal_repr(struct sclisp *s, const struct Object *obj);

static int object_as_string(struct sclisp *s, struct Object *obj, char **out)
{
    char *out_str = NULL;

    if (!out)
        return SCLISP_BADARG;

    s->le = SCLISP_OK;
    s->errmsg = NULL;

    /* If it's a string, just call strdup. Otherwise, call repr. */
    if (is_string(obj)) {
        out_str = sc_strdup(s->cb, obj->o.atom.a.string);
        if (!out_str)
            SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
    } else
        out_str = internal_repr(s, obj);

    if (SCLISP_ERR_REPORTED(s)) {
        s->cb->free_func(s->cb, out_str);
        return s->le;
    }

    *out = out_str;

    return SCLISP_OK;
}

/***************************************************
 * Cell constructors/accessors
 **************************************************/

static struct Object* internal_cons(struct sclisp *s, struct Object *car,
        struct Object *cdr)
{
    struct Object *obj = s->cb->alloc_func(s->cb, sizeof(*obj));

    if (obj) {
        obj->tag = CELL;
        obj->o.cell.car = object_ref(car);
        obj->o.cell.cdr = object_ref(cdr);
        obj->ref = 1;
    } else
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);

    return obj;
}

static struct Object* internal_car(struct Object *obj)
{
    if (is_cell(obj))
        return obj->o.cell.car;

    return obj;
}

static struct Object* internal_cdr(struct Object *obj)
{
    if (is_cell(obj))
        return obj->o.cell.cdr;

    return NULL;
}

static struct Object* internal_reverse(struct sclisp *s, struct Object *list)
{
    struct Object *reversed = NULL, *car, *cdr;

    if (is_nil(list))
        return NULL;
    else if (is_atom(list))
        return list;

    car = internal_car(list);
    cdr = internal_cdr(list);
    if (!is_nil(car) && is_atom(cdr)) {
        return internal_cons(s, cdr, car);
    }

    for (cdr = list; (car = internal_car(cdr)) || cdr; cdr = internal_cdr(cdr))
        reversed = internal_cons(s, car, reversed);

    return reversed;
}

/***************************************************
 * Eval function
 **************************************************/

static struct Object* eval_function(struct sclisp *s,
        struct Object *symbols, struct Object *args, struct Object *body)
{
    struct Object *car, *cdr, *expr_res = NULL;

    /* TODO: Somehow support variadic functions. This could/should
       probably actually be supported in scope_enter_with instead.
       Basically, we could have automatic unpacking so the last
       argument is just the cdr (or whatever is left). Arguments that
       aren't filled out, in this case, would end up being nil. That
       flies in the face of plans to curry, although more special
       casing could be added for that. */

    scope_enter_with(s, symbols, args);
    if (SCLISP_ERR_REPORTED(s)) {
        return NULL;
    }

    for (car = internal_car(body), cdr = internal_cdr(body);
            car != NULL || cdr != NULL;
            car = internal_car(cdr), cdr = internal_cdr(cdr)) {
        expr_res = internal_eval(s, car);
        ON_ERR_UNREF1_THEN(s, expr_res, return NULL);
    }

    scope_pop_to_parent(s, &s->scope);
    ON_ERR_UNREF1_THEN(s, expr_res, return NULL);

    return expr_res;
}

static struct Object* internal_eval(struct sclisp *s, struct Object *obj)
{
    if (is_nil(obj))
        return NULL;

    if (is_cell(obj)) {
        struct Object *result = NULL, *car = internal_car(obj);
        car = internal_eval(s, car);

        if (!is_atom(car)) {
            object_unref(s->cb, car);
            SCLISP_REPORT_ERR(s, SCLISP_BADARG,
                    "non-atomic operator is not executable");
            return NULL;
        }

        switch (car->o.atom.tag) {
            case FUNCTION:
                result = eval_function(s, car->o.atom.a.function.args,
                        internal_cdr(obj), car->o.atom.a.function.body);
                break;
            case BUILTIN:
                result = car->o.atom.a.builtin.func(internal_cdr(obj),
                        car->o.atom.a.builtin.user);
                break;
            default:
                SCLISP_REPORT_ERR(s, SCLISP_BADARG,
                        "atomic operator is not executable");
                break;
        }

        object_unref(s->cb, car);

        if (result)
            return result;
    } else {
        int res;

        if (obj->o.atom.tag != SYMBOL)
            return object_ref(obj);

        if ((res = scope_query(s->scope, obj->o.atom.a.symbol, &obj))) {
            SCLISP_REPORT_ERR(s, res, "scope query failed");
            return NULL;
        }

        return obj;
    }

    return NULL;
}

/***************************************************
 * Parser
 **************************************************/

enum TokenTag {
    TOK_INTEGER,
    TOK_REAL,
    TOK_STRING,
    TOK_SYMBOL,
    TOK_NIL,
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_QUOTE,

    TOK_UNKOWN = -1
};

struct Token {
    enum TokenTag tag;
    union {
        long integer;
        double real;
        char *str;
    } data;
    struct Token *next;
};

static void tokstream_free(struct sclisp_cb *cb, struct Token *tok)
{
    while (tok) {
        struct Token *next = tok->next;

        if (tok->tag == TOK_STRING || tok->tag == TOK_SYMBOL)
            cb->free_func(cb, tok->data.str);
        cb->free_func(cb, tok);

        tok = next;
    }
}

static struct Token* lex_expr(struct sclisp *s, const char *expr)
{
    char buf[128];
    long integer;
    double real;
    struct Token head;
    struct Token *cur;
    struct Token *tail = &head;
    int off = 0;
    int quote = 0;

    head.next = NULL;

    buf[0] = '\0';
    while (*expr) {
        enum TokenTag tag = TOK_UNKOWN;

        if (off == sizeof(buf)/sizeof(buf[0]) - 1) {
            SCLISP_REPORT_ERR(s, SCLISP_OVERFLOW,
                    "token length exceeds buffer size");
            tokstream_free(s->cb, head.next);
            return NULL;
        }

        if (*expr == '"') {
            if (!quote) {
                quote = 1;
                ++expr;
                continue;
            } else {
                quote = 0;
                tag = TOK_STRING;
                goto append_tok;
            }
        }

        /* TODO: Handle other whitespace, if necessary. */
        if (!quote && off && (*expr == ')' || isspace(*expr))) {
            --expr;
            goto append_tok;
        }

        if (!quote && isspace(*expr)) {
            ++expr;
            continue;
        }

        buf[off++] = *expr;
        buf[off] = '\0';

        if (!quote) {
            if (off == 1) {
                switch (buf[0]) {
                    case '(':
                        tag = TOK_LPAREN;
                        goto append_tok;
                    case ')':
                        tag = TOK_RPAREN;
                        goto append_tok;
                    case '\'':
                        tag = TOK_QUOTE;
                        goto append_tok;
                    default:
                        break;
                }
            }
        }

        ++expr;

        /* TODO: Handle broken quote/broken paren somewhere?? */
        if (*expr)
            continue;

append_tok:
        if (tag == TOK_UNKOWN) {
            if (sc_scan_integer(buf, &integer))
                tag = TOK_INTEGER;
            else if (sc_scan_real(buf, &real))
                tag = TOK_REAL;
            else if (!strcmp(buf, "nil"))
                tag = TOK_NIL;
            else
                tag = TOK_SYMBOL;
        }

        cur = s->cb->zalloc_func(s->cb, sizeof(*cur));
        if (!cur) {
            tokstream_free(s->cb, head.next);
            SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
            return NULL;
        }

        cur->tag = tag;
        switch (tag) {
            case TOK_INTEGER:
                cur->data.integer = integer;
                break;
            case TOK_REAL:
                cur->data.real = real;
                break;
            case TOK_STRING:
            case TOK_SYMBOL:
                cur->data.str = sc_strdup(s->cb, buf);
                if (!cur->data.str) {
                    tokstream_free(s->cb, head.next);
                    SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
                    return NULL;
                }
                break;
            default:
                break;
        }

        tail->next = cur;
        tail = cur;

        ++expr;
        buf[0] = '\0';
        off = 0;
    }

    return head.next;
}

static struct Object* parse_expr_helper(struct sclisp *s, struct Token **tok)
{
    int pcount = 0, quoting = 0;
    struct Object dummy;
    struct Object *tail = &dummy;

    dummy.o.cell.cdr = NULL;

    while (*tok) {
        struct Object *next = NULL;

        switch ((*tok)->tag) {
            case TOK_INTEGER:
                next = some_integer(s, (*tok)->data.integer);
                break;
            case TOK_REAL:
                next = some_real(s, (*tok)->data.real);
                break;
            case TOK_STRING:
                next = some_string(s, (*tok)->data.str);
                break;
            case TOK_SYMBOL:
                next = some_symbol(s, (*tok)->data.str);
                break;
            case TOK_NIL:
                break;
            case TOK_LPAREN:
                if (pcount)
                    next = parse_expr_helper(s, tok);
                else {
                    ++pcount;
                    *tok = (*tok)->next;
                    continue;
                }
                break;
            case TOK_RPAREN:
                if (pcount)
                    return dummy.o.cell.cdr;
                else {
                    /* TODO: We're off sides. Handle this syntax error. */
                }
                break;
            case TOK_QUOTE:
                /* This might not be the most elegant way to do this,
                   but it gets the job done. */
                ++quoting;
                *tok = (*tok)->next;
                if ((*tok)->tag == TOK_LPAREN) {
                    next = parse_expr_helper(s, tok);
                } else
                    continue;
                break;
            default:
                SCLISP_REPORT_BUG(s, "BUG - invalid token type");
                goto parse_error;
        }

        ON_ERR_UNREF1_THEN(s, next, goto parse_error);

        while (quoting) {
            struct Object *t0, *t1;

            t0 = next;
            next = internal_cons(s, next, NULL);
            object_unref(s->cb, t0);

            ON_ERR_UNREF1_THEN(s, next, goto parse_error);

            t1 = some_symbol(s, "quote");
            ON_ERR_UNREF2_THEN(s, next, t1, goto parse_error);

            t0 = next;
            next = internal_cons(s, t1, next);
            object_unref(s->cb, t0);
            object_unref(s->cb, t1);

            ON_ERR_UNREF1_THEN(s, next, goto parse_error);

            --quoting;
        }

        if (!pcount)
            return next;

        tail->o.cell.cdr = internal_cons(s, next, NULL);
        tail = tail->o.cell.cdr;

        object_unref(s->cb, next);

        if (SCLISP_ERR_REPORTED(s))
            goto parse_error;

        *tok = (*tok)->next;
    }

    return dummy.o.cell.cdr;

parse_error:

    object_unref(s->cb, dummy.o.cell.cdr);
    return NULL;
}

static struct Object* parse_expr(struct sclisp *s, const char *expr)
{
    struct Object *result;
    struct Token *tok = lex_expr(s, expr);

    if (SCLISP_ERR_REPORTED(s)) {
        tokstream_free(s->cb, tok);
        return NULL;
    }

    /* TODO: Check that tok has been set to NULL, indicating all tokens
       consumed. If this is not the case, parens are off balance. */
    result = parse_expr_helper(s, &tok);
    tokstream_free(s->cb, tok);

    return result;
}

/***************************************************
 * Object printer
 **************************************************/

int internal_repr_helper(const struct Object *obj, char *buf, int offset,
        int max)
{
    /* TODO: Indicate that truncation is happening in some way. */
    if (max - 1 <= offset)
        return offset;

    if (!obj || obj->tag == ATOM) {
        int len;

        if (!obj) {
            len = 3;
            strncpy(&buf[offset], "nil", max - offset - 1);
        } else switch (obj->o.atom.tag) {
            case INTEGER:
                len = snprintf(&buf[offset], max - offset - 1, "%ld",
                        obj->o.atom.a.integer);
                break;
            case REAL:
                /* HACK: Do not display excess trailing zeros after the
                   decimal point. */
                len = snprintf(&buf[offset], max - offset - 1, "%.6f",
                        obj->o.atom.a.real);
                while (buf[offset + len - 1] == '0' &&
                        buf[offset + len - 2] != '.')
                    --len;
                break;
            case STRING:
                len = snprintf(&buf[offset], max - offset - 1, "\"%s\"",
                        obj->o.atom.a.string);
                break;
            case SYMBOL:
                len = strlen(obj->o.atom.a.symbol);
                strncpy(&buf[offset], obj->o.atom.a.symbol, max - offset - 1);
                break;
            case FUNCTION:
                len = 6;
                strncpy(&buf[offset], "<func>", max - offset - 1);
                break;
            case BUILTIN:
                len = 9;
                strncpy(&buf[offset], "<builtin>", max - offset - 1);
                break;
            default:
                /* TODO: This should probably be treated as a bug in
                   some way. */
                return offset;
        }

        offset = MIN_(max - 1, offset + len);
        buf[offset] = '\0';
    } else {
        buf[offset++] = '(';
        buf[offset] = '\0';

        /* Treat this as a special case: the empty list. */
        while ((offset < max - 1) && obj) {
            offset = internal_repr_helper(obj->o.cell.car, buf, offset, max);
            obj = obj->o.cell.cdr;
            if ((offset < max - 1) && obj) {
                if (obj->tag != CELL) {
                    strncpy(&buf[offset], " . ", max - offset - 1);
                    offset = MIN_(max - 1, offset + 3);
                    buf[offset] = '\0';
                    offset = internal_repr_helper(obj, buf, offset, max);
                    obj = NULL;
                } else {
                    buf[offset++] = ' ';
                    buf[offset] = '\0';
                }
            }
        }

        if (offset < max - 1) {
            buf[offset++] = ')';
            buf[offset] = '\0';
        }
    }

    return offset;
}

static char* internal_repr(struct sclisp *s, const struct Object *obj)
{
    char *buf = s->cb->zalloc_func(s->cb, 1024);
    if (!buf) {
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
        return NULL;
    }

    /* TODO: Somehow indicate truncation or if bugs have occurred. */
    internal_repr_helper(obj, buf, 0, 1024);

    return buf;
}

/***************************************************
 * Builtin functions
 **************************************************/

#define BUILTIN_FUNC(n) \
    static struct Object* builtin_##n(struct Object *args, void *user)

static const char * const NEEDS_ONE_ARG = "needs exactly one argument";
static const char * const NEEDS_LTE_TWO_ARGS =
    "accepts no more than two arguments";
static const char * const NEEDS_TWO_ARG = "needs exactly two arguments";

/* XXX: Calling a function with no arguments is indistinguishable from
   calling it with a single argument of nil. This is fine. */
#define BUILTIN_FUNC_ONE_ARG(_a1)   \
    do {                                                        \
        if (internal_cdr(args)) {                               \
            SCLISP_REPORT_ERR(s, SCLISP_BADARG, NEEDS_ONE_ARG); \
            return NULL;                                        \
        }                                                       \
        _a1 = internal_eval(s, internal_car(args));             \
        ON_ERR_UNREF1_THEN(s, _a1, return NULL);                \
    } while (0)

#define BUILTIN_FUNC_LTE_TWO_ARGS(_a1, _a2) \
    do {                                                                \
        if (internal_cdr(internal_cdr(args))) {                         \
            SCLISP_REPORT_ERR(s, SCLISP_BADARG, NEEDS_LTE_TWO_ARGS);    \
            return NULL;                                                \
        }                                                               \
        _a1 = internal_eval(s, internal_car(args));                     \
        ON_ERR_UNREF1_THEN(s, _a1, return NULL);                        \
        _a2 = internal_eval(s, internal_car(internal_cdr(args)));       \
        ON_ERR_UNREF2_THEN(s, _a2, _a1, return NULL);                   \
    } while (0)

#define BUILTIN_FUNC_TWO_ARG(_a1, _a2) \
    do {                                                            \
        if (!args || !internal_cdr(args) ||                         \
                internal_cdr(internal_cdr(args))) {                 \
            SCLISP_REPORT_ERR(s, SCLISP_BADARG, NEEDS_TWO_ARG);     \
            return NULL;                                            \
        }                                                           \
        _a1 = internal_eval(s, internal_car(args));                 \
        ON_ERR_UNREF1_THEN(s, _a1, return NULL);                    \
        _a2 = internal_eval(s, internal_car(internal_cdr(args)));   \
        ON_ERR_UNREF2_THEN(s, _a2, _a1, return NULL);               \
    } while (0)

enum MathOp {
    ATOM_MUL,
    ATOM_DIV,
    ATOM_ADD,
    ATOM_SUB,
    ATOM_MOD
};

static int math_op(struct Atom *l, const struct Object *r, enum MathOp op)
{
    struct Atom mut_r, *ar = &mut_r;

    if (is_nil(r)) {
        /* In my world, nil == 0. */
        mut_r.tag = INTEGER;
        mut_r.a.integer = 0;
    } else if (!is_numeric_atom(r))
        return SCLISP_BADARG;
    else
        mut_r = r->o.atom;

    #define _promote_left(left, right)  \
        do {                                                    \
            if (right->tag == REAL && left->tag == INTEGER) {   \
                left->a.real = left->a.integer;                 \
                left->tag = REAL;                               \
            }                                                   \
        } while (0)

    /* Promote to real if necessary. */
    _promote_left(l, ar);
    _promote_left(ar, l);

    #undef _promote_left

    #define _checked_math(left, right, _op) \
        do {                                                \
            if (left->tag == INTEGER)                       \
                left->a.integer _op##= right->a.integer;    \
            else                                            \
                left->a.real _op##= right->a.real;          \
        } while (0)

    #define _atom_zero(atom)    \
        ((atom)->tag == INTEGER && !(atom)->a.integer) ||       \
                ((atom)->tag == REAL && (atom)->a.real == 0.0)

    switch (op) {
        case ATOM_MUL:
            _checked_math(l, ar, *);
            break;
        case ATOM_DIV:
            if (_atom_zero(ar))
                return SCLISP_BADARG;
            _checked_math(l, ar, /);
            break;
        case ATOM_ADD:
            _checked_math(l, ar, +);
            break;
        case ATOM_SUB:
            _checked_math(l, ar, -);
            break;
        case ATOM_MOD:
            if (_atom_zero(ar))
                return SCLISP_BADARG;
            if (l->tag == INTEGER)
                l->a.integer = l->a.integer % ar->a.integer;
            else
                #if SCLISP_FMOD_SUPPORT
                    l->a.real = fmod(l->a.real, ar->a.real);
                #else
                    return SCLISP_UNSUPPORTED;
                #endif
            break;
        default:
            return SCLISP_BUG;
    }

    #undef _atom_zero
    #undef _checked_math

    return SCLISP_OK;
}

#define some_number(s, atom)   \
    (((atom)->tag == INTEGER ) ?                \
        some_integer(s, (atom)->a.integer) :    \
        some_real(s, (atom)->a.real))

#define MATH_FUNC(_name, _op, _init) \
    BUILTIN_FUNC(_name)                                                 \
    {                                                                   \
        struct sclisp *s = (struct sclisp *)user;                       \
        struct Object *car;                                             \
        struct Atom acc;                                                \
                                                                        \
        acc.tag = INTEGER;                                              \
        acc.a.integer = (_init * _init) & 1;                            \
                                                                        \
        if ((_init < 0) && internal_cdr(args)) {                        \
            struct Object *ecar = internal_eval(s, internal_car(args)); \
            ON_ERR_UNREF1_THEN(s, ecar, return NULL);                   \
            if (!ecar)                                                  \
                acc.a.integer = 0;                                      \
            else if (is_numeric_atom(ecar))                             \
                acc = ecar->o.atom;                                     \
            else {                                                      \
                SCLISP_REPORT_ERR(s, SCLISP_BADARG, NULL);              \
                object_unref(s->cb, ecar);                              \
                return NULL;                                            \
            }                                                           \
            args = internal_cdr(args);                                  \
            object_unref(s->cb, ecar);                                  \
        }                                                               \
                                                                        \
        for (; (car = internal_car(args)) || args;                      \
                args = internal_cdr(args)) {                            \
            struct Object *ecar;                                        \
            int res;                                                    \
            ecar = internal_eval(s, car);                               \
            ON_ERR_UNREF1_THEN(s, ecar, return NULL);                   \
            res = math_op(&acc, ecar, _op);                             \
            object_unref(s->cb, ecar);                                  \
            if (res) {                                                  \
                SCLISP_REPORT_ERR(s, res, "math op failed");            \
                return NULL;                                            \
            }                                                           \
        }                                                               \
                                                                        \
        return some_number(s, &acc);                                    \
    }

MATH_FUNC(plus, ATOM_ADD, 0)
MATH_FUNC(minus, ATOM_SUB, -1)
MATH_FUNC(multiply, ATOM_MUL, 1)
MATH_FUNC(divide, ATOM_DIV, -1)
MATH_FUNC(mod, ATOM_MOD, -1)

#undef MATH_FUNC
#undef some_number

BUILTIN_FUNC(set)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *left, *right, *rest, *eright = NULL;

    left = internal_car(args);
    right = internal_car((rest = internal_cdr(args)));
    rest = internal_cdr(rest);

    if (!is_symbol(left) || rest) {
        struct Object *lcar = internal_car(left);

        if (is_cell(left) && is_symbol(lcar)) {
            /* This function assignment sugar. */
            eright = some_function(s, internal_cdr(left),
                    internal_cdr(args));
            ON_ERR_UNREF1_THEN(s, eright, return NULL);
            left = lcar;
        } else {
            SCLISP_REPORT_ERR(s, SCLISP_BADARG, "set - bad first operand");
            return NULL;
        }
    }

    if (!eright)
        eright = internal_eval(s, right);
    ON_ERR_UNREF1_THEN(s, eright, return NULL);

    scope_set(s, s->scope, left->o.atom.a.symbol, eright);
    ON_ERR_UNREF1_THEN(s, eright, return NULL);

    /* eright is returned, so no need to unref */

    return eright;
}

#define CARCDR_FUNC(_op)    \
    BUILTIN_FUNC(_op)                               \
    {                                               \
        struct sclisp *s = (struct sclisp *)user;   \
        struct Object *arg1, *result;               \
                                                    \
        BUILTIN_FUNC_ONE_ARG(arg1);                 \
        result = internal_##_op(arg1);              \
                                                    \
        object_ref(result);                         \
        object_unref(s->cb, arg1);                  \
                                                    \
        return result;                              \
    }

CARCDR_FUNC(car)
CARCDR_FUNC(cdr)

#undef CARCDR_FUNC

BUILTIN_FUNC(cons)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *arg1, *arg2, *result;

    /* We need no more than two arguments. */
    BUILTIN_FUNC_LTE_TWO_ARGS(arg1, arg2);

    result = internal_cons(s, arg1, arg2);
    object_unref(s->cb, arg1);
    object_unref(s->cb, arg2);

    return result;
}

BUILTIN_FUNC(eval)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *arg1, *result;

    BUILTIN_FUNC_ONE_ARG(arg1);

    result = internal_eval(s, arg1);
    object_unref(s->cb, arg1);

    return result;
}

BUILTIN_FUNC(reverse)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *arg1, *result;

    BUILTIN_FUNC_ONE_ARG(arg1);

    result = internal_reverse(s, arg1);

    object_unref(s->cb, arg1);

    return result;
}

BUILTIN_FUNC(list)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *ecar, *ecdr, *result;

    if (is_nil(args))
        return NULL;

    ecar = internal_eval(s, internal_car(args));
    ON_ERR_UNREF1_THEN(s, ecar, return NULL);

    ecdr = builtin_list(internal_cdr(args), s);
    ON_ERR_UNREF2_THEN(s, ecar, ecdr, return NULL);

    result = internal_cons(s, ecar, ecdr);
    object_unref(s->cb, ecar);
    object_unref(s->cb, ecdr);

    return result;
}

BUILTIN_FUNC(quote)
{
    struct sclisp *s = (struct sclisp *)user;

    if (internal_cdr(args)) {
        SCLISP_REPORT_ERR(s, SCLISP_BADARG, NEEDS_ONE_ARG);
        return NULL;
    }

    return object_ref(internal_car(args));
}

BUILTIN_FUNC(lambda)
{
    struct sclisp *s = (struct sclisp *)user;

    return some_function(s, internal_car(args), internal_cdr(args));
}

BUILTIN_FUNC(cond)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *car;

    for (; (car = internal_car(args)) || args; args = internal_cdr(args)) {
        struct Object *ecar;
        int res;

        if (!is_cell(car) || internal_cdr(internal_cdr(car))) {
            SCLISP_REPORT_ERR(s, SCLISP_BADARG,
                    "cond branch needs two arguments");
            return NULL;
        }

        ecar = internal_eval(s, internal_car(car));
        res = is_true(ecar);
        object_unref(s->cb, ecar);

        if (SCLISP_ERR_REPORTED(s))
            return NULL;

        if (res)
            return internal_eval(s, internal_car(internal_cdr(car)));
    }

    return NULL;
}

#define UNARY_BOOL_FUNC(n)  \
    BUILTIN_FUNC(n##q)                                  \
    {                                                   \
        struct sclisp *s = (struct sclisp *)user;       \
        struct Object *ecar;                            \
        int res;                                        \
                                                        \
        BUILTIN_FUNC_ONE_ARG(ecar);                     \
                                                        \
        res = is_##n(ecar);                             \
        object_unref(s->cb, ecar);                      \
                                                        \
        return res ? SC_STATIC_TRUE : SC_STATIC_FALSE;  \
    }

UNARY_BOOL_FUNC(true)
UNARY_BOOL_FUNC(false)
UNARY_BOOL_FUNC(atom)
UNARY_BOOL_FUNC(cell)
UNARY_BOOL_FUNC(nil)

#undef UNARY_BOOL_FUNC

enum LogicOp {
    ATOM_LT,
    ATOM_LTE,
    ATOM_GT,
    ATOM_GTE,
    ATOM_EQ
};

static struct Object* logic_op(struct sclisp *s, struct Object *l,
        struct Object *r, enum LogicOp op)
{
    struct Atom mut_l, mut_r, *ml = &mut_l, *mr = &mut_r;
    char *pstr = NULL;
    int res = -1;

    /* Static instance equality hack, for no good reason. */
    if (op == ATOM_EQ && l && r && l->ref == SCLISP_STATIC_MAGIC &&
            r->ref == SCLISP_STATIC_MAGIC)
        return l == r ? SC_STATIC_TRUE : SC_STATIC_FALSE;

    #define _verify_arg(_a) \
        do {                                                                \
            if (is_nil(_a)) {                                               \
                mut_##_a.tag = INTEGER;                                     \
                mut_##_a.a.integer = 0;                                     \
            } else if (!is_numeric_atom(_a) && !is_string(_a)) {            \
                SCLISP_REPORT_ERR(s, SCLISP_BADARG,                         \
                        "logic op needs integer, real, or string operands");\
                return NULL;                                                \
            } else                                                          \
                mut_##_a = _a->o.atom;                                      \
        } while (0)

    _verify_arg(l);
    _verify_arg(r);

    #undef _verify_arg

    #define _promote_left(left, right) \
        do {                                                            \
            if (right->tag == REAL && left->tag == INTEGER) {           \
                left->a.real = left->a.integer;                         \
                left->tag = REAL;                                       \
            } else if (right->tag == STRING && left->tag != STRING) {   \
                struct Object wrapper;                                  \
                                                                        \
                if (pstr) {                                             \
                    SCLISP_REPORT_BUG(s, "BUG - pstr should be NULL");  \
                    return NULL;                                        \
                }                                                       \
                                                                        \
                wrapper.tag = ATOM;                                     \
                wrapper.o.atom = *left;                                 \
                wrapper.ref = SCLISP_TRANSIENT_MAGIC;                   \
                pstr = internal_repr(s, &wrapper);                      \
                                                                        \
                if (SCLISP_ERR_REPORTED(s)) {                           \
                    s->cb->free_func(s->cb, pstr);                      \
                    return NULL;                                        \
                }                                                       \
                                                                        \
                left->tag = STRING;                                     \
                left->a.string = pstr;                                  \
            }                                                           \
        } while (0)

    _promote_left(ml, mr);
    _promote_left(mr, ml);

    #undef _promote_left

    #define _checked_compare(left, right, _op) \
        do {                                                            \
            if (left->tag == INTEGER)                                   \
                res = left->a.integer _op right->a.integer;             \
            else if (left->tag == REAL)                                 \
                res = left->a.real _op right->a.real;                   \
            else                                                        \
                res = strcmp(left->a.string, right->a.string) _op 0;    \
        } while (0)

    switch (op) {
        case ATOM_LT:
            _checked_compare(ml, mr, <);
            break;
        case ATOM_LTE:
            _checked_compare(ml, mr, <=);
            break;
        case ATOM_GT:
            _checked_compare(ml, mr, >);
            break;
        case ATOM_GTE:
            _checked_compare(ml, mr, >=);
            break;
        case ATOM_EQ:
            _checked_compare(ml, mr, ==);
            break;
        default:
            SCLISP_REPORT_BUG(s, "BUG - invalid logic op");
            break;
    }

    #undef _checked_compare

    s->cb->free_func(s->cb, pstr);

    if (res >= 0)
        return res ? SC_STATIC_TRUE : SC_STATIC_FALSE;

    return NULL;
}

#define LOGIC_FUNC(_name, _op)  \
    BUILTIN_FUNC(_name)                             \
    {                                               \
        struct sclisp *s = (struct sclisp *)user;   \
        struct Object *arg1, *arg2, *result;        \
                                                    \
        BUILTIN_FUNC_TWO_ARG(arg1, arg2);           \
        result = logic_op(s, arg1, arg2, _op);      \
                                                    \
        object_unref(s->cb, arg1);                  \
        object_unref(s->cb, arg2);                  \
                                                    \
        return result;                              \
    }

LOGIC_FUNC(lt, ATOM_LT)
LOGIC_FUNC(lte, ATOM_LTE)
LOGIC_FUNC(gt, ATOM_GT)
LOGIC_FUNC(gte, ATOM_GTE)
LOGIC_FUNC(eq, ATOM_EQ)

#undef LOGIC_FUNC

BUILTIN_FUNC(and)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *car, *ecar = SC_STATIC_TRUE;

    for (; (car = internal_car(args)) || args; args = internal_cdr(args)) {
        object_unref(s->cb, ecar);
        ecar = internal_eval(s, car);

        ON_ERR_UNREF1_THEN(s, ecar, return NULL);

        if (!is_true(ecar)) {
            object_unref(s->cb, ecar);

            return NULL;
        }
    }

    return ecar;
}

BUILTIN_FUNC(or)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *car;

    for (; (car = internal_car(args)) || args; args = internal_cdr(args)) {
        struct Object *ecar = internal_eval(s, car);

        ON_ERR_UNREF1_THEN(s, ecar, return NULL);

        if (is_true(ecar))
            return ecar;

        object_unref(s->cb, ecar);
    }

    return NULL;
}

BUILTIN_FUNC(typeof)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *ecar, *result = NULL;

    BUILTIN_FUNC_ONE_ARG(ecar);

    if (!ecar)
        return SC_STATIC_NIL_STR;

    if (is_cell(ecar)) {
        result = SC_STATIC_CELL_STR;
    } else switch (ecar->o.atom.tag) {
        #define _case(_t)   case _t: result = SC_STATIC_##_t##_STR; break
        _case(INTEGER);
        _case(REAL);
        _case(STRING);
        _case(SYMBOL);
        _case(FUNCTION);
        _case(BUILTIN);
        default:
            SCLISP_REPORT_BUG(s, "BUG - object has unknown type");
            break;
        #undef _case
    }

    object_unref(s->cb, ecar);

    return result;
}

/* TODO: Experimental builtin. */
/* TODO: Should this actually just print every argument and ignore
   things that aren't printable? Should this call repr on non-string
   arguments? Figure out the experimental semantics of this function. */
BUILTIN_FUNC(println)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *ecar;

    BUILTIN_FUNC_ONE_ARG(ecar);

    if (is_string(ecar))
        /* TODO: Support some kind of escape sequences, especially \n. */
        sc_printf(s->cb, SCLISP_STDOUT, "%s\n", ecar->o.atom.a.string);
    else {
        SCLISP_REPORT_ERR(s, SCLISP_UNSUPPORTED,
                "cannot print non-string object");
    }
    object_unref(s->cb, ecar);

    return NULL;
}

BUILTIN_FUNC(prompt)
{
    struct sclisp *s = (struct sclisp *)user;
    struct Object *result, *arg1;
    char *line;

    BUILTIN_FUNC_ONE_ARG(arg1);

    if (is_string(arg1))
        s->cb->print_func(s->cb, SCLISP_STDOUT, arg1->o.atom.a.string);
    object_unref(s->cb, arg1);

    line = sc_getline(s);
    if (SCLISP_ERR_REPORTED(s)) {
        s->cb->free_func(s->cb, line);
        return NULL;
    }

    result = some_string(s, line);
    s->cb->free_func(s->cb, line);

    return result;
}

#undef BUILTIN_FUNC_TWO_ARG
#undef BUILTIN_FUNC_LTE_TWO_ARGS
#undef BUILTIN_FUNC_ONE_ARG

#undef BUILTIN_FUNC

void apply_builtins(struct sclisp *s)
{
    /* FIXME: Add some kind of error handling here. Anything
       that goes wrong in this function is definitely a bug,
       however it's not terribly clear what the best way to
       report that to the user would be at this point. */
    #define _apply_named_builtin(f, n)                  \
        do {                                            \
            struct Object *b;                           \
            b = some_builtin(s, builtin_##f, s, NULL);  \
            scope_set(s, s->scope, n, b);               \
            object_unref(s->cb, b);                     \
        } while (0)
    #define _apply_builtin(f)   _apply_named_builtin(f, PPSTR(f))

    _apply_builtin(set);
    _apply_named_builtin(plus, "+");
    _apply_named_builtin(minus, "-");
    _apply_named_builtin(multiply, "*");
    _apply_named_builtin(divide, "/");
    _apply_builtin(mod);
    _apply_builtin(car);
    _apply_builtin(cdr);
    _apply_builtin(cons);
    _apply_builtin(eval);
    _apply_builtin(reverse);
    _apply_builtin(list);
    _apply_builtin(quote);
    _apply_builtin(lambda);
    _apply_builtin(cond);
    _apply_named_builtin(trueq, "true?");
    _apply_named_builtin(falseq, "false?");
    _apply_named_builtin(atomq, "atom?");
    _apply_named_builtin(cellq, "cell?");
    _apply_named_builtin(nilq, "nil?");
    _apply_named_builtin(lt, "<");
    _apply_named_builtin(lte, "<=");
    _apply_named_builtin(gt, ">");
    _apply_named_builtin(gte, ">=");
    _apply_named_builtin(eq, "==");
    _apply_builtin(and);
    _apply_builtin(or);
    _apply_builtin(typeof);
    _apply_builtin(println);
    _apply_builtin(prompt);

    #undef _apply_builtin
    #undef _apply_named_builtin

    /* Add true/false constant syntax sugar. */
    scope_set(s, s->scope, "#t", SC_STATIC_TRUE);
    scope_set(s, s->scope, "#f", SC_STATIC_FALSE);
}

/***************************************************
 * User function API/wrappers
 **************************************************/

struct UserFuncState {
    struct sclisp *s;
    struct Object *args;
    struct Object *result;
};

#define OBJECT_as_integer(_s, _o, _out)     object_as_integer(_o, _out)
#define OBJECT_as_real(_s, _o, _out)        object_as_real(_o, _out)
#define OBJECT_as_string(_s, _o, _out)      object_as_string(_s, _o, _out)

#define WRAPPER_ARG_FUNC(_n, _t) \
    static int wrapper_arg_##_n(const struct sclisp_func_api *api,  \
            unsigned index, _t *out)                                \
    {                                                               \
        struct UserFuncState *st;                                   \
        struct Object *args, *ecar;                                 \
        unsigned i, res;                                            \
                                                                    \
        if (!api || !out)                                           \
            return SCLISP_BADARG;                                   \
        st = (struct UserFuncState*)api->inst;                      \
        args = st->args;                                            \
                                                                    \
        for (i = 0; i < index; ++i)                                 \
            args = internal_cdr(args);                              \
                                                                    \
        ecar = internal_eval(st->s, internal_car(args));            \
        ON_ERR_UNREF1_THEN(st->s, ecar, return st->s->le);          \
                                                                    \
        /* TODO: Determine if nil should be allowed in user         \
           functions. Currently it is not. */                       \
        if (!ecar)                                                  \
            return SCLISP_ERR;                                      \
                                                                    \
        res = OBJECT_as_##_n(st->s, ecar, out);                     \
        object_unref(st->s->cb, ecar);                              \
                                                                    \
        return res;                                                 \
    }

WRAPPER_ARG_FUNC(integer, long)
WRAPPER_ARG_FUNC(real, double)
WRAPPER_ARG_FUNC(string, char*)

#undef WRAPPER_ARG_FUNC

#define SAFE_integer(_v)    ( 1 )
#define SAFE_real(_v)       ( 1 )
#define SAFE_string(_v)     (!!_v)

#define WRAP_RETURN_FUNC(_n, _t)    \
    static int wrapper_return_##_n(const struct sclisp_func_api *api, _t ret) \
    {                                                                       \
        struct UserFuncState *st;                                           \
                                                                            \
        if (!api || !SAFE_##_n(ret))                                        \
            return SCLISP_BADARG;                                           \
        st = (struct UserFuncState*)api->inst;                              \
                                                                            \
        if (SCLISP_ERR_REPORTED(st->s))                                     \
            return st->s->le;                                               \
                                                                            \
        object_unref(st->s->cb, st->result);                                \
        st->result = some_##_n(st->s, ret);                                 \
                                                                            \
        return st->s->le;                                                   \
    }

WRAP_RETURN_FUNC(integer, long)
WRAP_RETURN_FUNC(real, double)
WRAP_RETURN_FUNC(string, char*)

#undef WRAP_RETURN_FUNC

static struct Object* user_builtin_wrapper(struct Object *args, void *user)
{
    struct UserFunc *f = (struct UserFunc*)user;
    struct UserFuncState state;
    struct sclisp_func_api api;
    int res;

    state.s = f->s;
    state.args = args;
    state.result = NULL;

    api.arg_integer = wrapper_arg_integer;
    api.arg_real = wrapper_arg_real;
    api.arg_string = wrapper_arg_string;
    api.return_integer = wrapper_return_integer;
    api.return_real = wrapper_return_real;
    api.return_string = wrapper_return_string;
    api.cb = f->s->cb;
    api.inst = &state;

    res = f->func(&api, f->user);
    if (res) {
        /* TODO: Make it so this doesn't clear any existing error
           (or, in particular, error message). This may be
           difficult. */
        SCLISP_REPORT_ERR(f->s, res, NULL);
    }

    return state.result;
}

static void user_builtin_wrapper_dtor(void *user)
{
    struct UserFunc *f = (struct UserFunc*)user;

    if (f->dtor)
        f->dtor(f->user);

    f->s->cb->free_func(f->s->cb, f);
}

/***************************************************
 * User scope API
 **************************************************/

#define USER_SCOPE_GET_FUNC(_n, _t) \
    static int user_scope_get_##_n(const struct sclisp_scope_api *api,      \
            char* sym, _t *out)                                             \
    {                                                                       \
        struct sclisp *s;                                                   \
        struct Object *obj = NULL;                                          \
        int res = SCLISP_OK;                                                \
                                                                            \
        if (!api || !sym || !out)                                           \
            return SCLISP_BADARG;                                           \
                                                                            \
        s = (struct sclisp*)api->inst;                                      \
        if ((res = scope_query(s->scope, sym, &obj)))                       \
            return res;                                                     \
                                                                            \
        res = OBJECT_as_##_n(s, obj, out);                                  \
                                                                            \
        object_unref(s->cb, obj);                                           \
                                                                            \
        return res;                                                         \
    }

USER_SCOPE_GET_FUNC(integer, long)
USER_SCOPE_GET_FUNC(real, double)
USER_SCOPE_GET_FUNC(string, char*)

#undef USER_SCOPE_GET_FUNC

#undef OBJECT_as_string
#undef OBJECT_as_real
#undef OBJECT_as_integer

#define USER_SCOPE_SET_FUNC(_n, _t) \
    static int user_scope_set_##_n(const struct sclisp_scope_api *api,      \
            char *sym, _t val)                                              \
    {                                                                       \
        struct sclisp *s;                                                   \
        struct Object *obj;                                                 \
                                                                            \
        if (!api || !sym || !SAFE_##_n(val))                                \
            return SCLISP_BADARG;                                           \
                                                                            \
        s = (struct sclisp*)api->inst;                                      \
                                                                            \
        s->le = SCLISP_OK;                                                  \
        s->errmsg = NULL;                                                   \
                                                                            \
        obj = some_##_n(s, val);                                            \
        ON_ERR_UNREF1_THEN(s, obj, return s->le);                           \
                                                                            \
        scope_set(s, s->scope, sym, obj);                                   \
        object_ref(obj);                                                    \
                                                                            \
        return s->le;                                                       \
    }

USER_SCOPE_SET_FUNC(integer, long)
USER_SCOPE_SET_FUNC(real, double)
USER_SCOPE_SET_FUNC(string, char*)

#undef USER_SCOPE_SET_FUNC

#undef SAFE_string
#undef SAFE_real
#undef SAFE_integer

/***************************************************
 * Default and shim callbacks
 **************************************************/

static void* default_alloc_func(struct sclisp_cb *cb, unsigned long sz)
{
    (void)cb;
    return malloc(sz);
}

static void* default_zalloc_func(struct sclisp_cb *cb, unsigned long sz)
{
    (void)cb;
    return calloc(1, sz);
}

static void default_free_func(struct sclisp_cb *cb, void *mem)
{
    (void)cb;
    free(mem);
}

static void default_print_func(struct sclisp_cb *cb, int fd, const char *str)
{
    (void)cb;
    fputs(str, fd == SCLISP_STDOUT ? stdout : stderr);
}

static char default_getchar_func(struct sclisp_cb *cb)
{
    (void)cb;
    return getchar();
}


static void* shim_zalloc_func(struct sclisp_cb *cb, unsigned long sz)
{
    void *mem = cb->alloc_func(cb, sz);
    if (mem)
        memset(mem, 0, sz);
    return mem;
}

struct sclisp_cb DEFAULT_CB = {
    default_alloc_func,
    default_zalloc_func,
    default_free_func,
    default_print_func,
    default_getchar_func,
    NULL,
};

/***************************************************
 * Library constants
 **************************************************/

const char * const SCLISP_VERSION = SCLISP_LIB_VERSION;
const unsigned long SCLISP_VERSION_NUMBER = SCLISP_LIB_VERSION_NUMBER;

/***************************************************
 * Library API
 **************************************************/

int sclisp_init(struct sclisp **s, struct sclisp_cb *cb)
{
    struct sclisp *_s;

    sc_lazy_static();

    if (!s)
        return SCLISP_BADARG;

    if (cb) {
        /* TODO: Consider making print_func optional. */
        if (!cb->alloc_func || !cb->free_func || !cb->print_func)
            return SCLISP_BADARG;
        if (!cb->zalloc_func)
            cb->zalloc_func = shim_zalloc_func;
    }
    else
        cb = &DEFAULT_CB;

    _s = cb->zalloc_func(cb, sizeof(**s));
    if (!_s)
        return SCLISP_NOMEM;

    _s->cb = cb;
    _s->scope = cb->zalloc_func(cb, sizeof(*(_s->scope)));
    if (!_s->scope) {
        cb->free_func(cb, _s);
        return SCLISP_NOMEM;
    }

    _s->lr = NULL;
    _s->le = SCLISP_OK;
    _s->errmsg = NULL;

    _s->usapi.get_integer = user_scope_get_integer;
    _s->usapi.get_real = user_scope_get_real;
    _s->usapi.get_string = user_scope_get_string;
    _s->usapi.set_integer = user_scope_set_integer;
    _s->usapi.set_real = user_scope_set_real;
    _s->usapi.set_string = user_scope_set_string;
    _s->usapi.cb = _s->cb;
    _s->usapi.inst = _s;

    apply_builtins(_s);

    *s = _s;

    return SCLISP_OK;
}

void sclisp_destroy(struct sclisp *s)
{
    sc_lazy_static();

    object_unref(s->cb, s->lr);
    s->lr = NULL;

    while (s->scope) {
        struct Scope *tmp = s->scope->parent;
        scope_free(s->cb, s->scope);
        s->scope = tmp;
    }

    s->cb->free_func(s->cb, s);
}

int sclisp_eval(struct sclisp *s, const char *exp)
{
    struct Object *parsed_expr, *tmp;

    sc_lazy_static();

    if (!s || !exp)
        return SCLISP_BADARG;

    s->le = SCLISP_OK;
    s->errmsg = NULL;

    parsed_expr = parse_expr(s, exp);
    tmp = s->lr;
    s->lr = internal_eval(s, parsed_expr);

    object_unref(s->cb, parsed_expr);
    object_unref(s->cb, tmp);

    return s->le;
}

const char* sclisp_errstr(int errcode)
{
    switch (errcode) {
        #define _case(_s)   case _s: return #_s
        _case(SCLISP_OK);
        _case(SCLISP_ERR);
        _case(SCLISP_NOMEM);
        _case(SCLISP_BADARG);
        _case(SCLISP_UNSUPPORTED);
        _case(SCLISP_OVERFLOW);
        _case(SCLISP_BUG);
        default:
            return NULL;
        #undef _case
    }
}

const char* sclisp_errmsg(struct sclisp *s)
{
    if (!s)
        return NULL;

    return s->errmsg;
}

/* TODO: Everything below is experimental API. */

int sclisp_register_user_func(struct sclisp *s,
        int (*user_func)(const struct sclisp_func_api *api, void *user),
        const char *name, void *user, void (*dtor)(void *user))
{
    struct UserFunc *f;
    struct Object *user_builtin;

    if (!s || !name)
        return SCLISP_BADARG;

    s->le = SCLISP_OK;
    s->errmsg = NULL;

    if (!user_func) {
        /* TODO: Unset existing function if exists. */
        /* XXX: As a temporary hack, simply set the binding to NIL. */
        scope_set(s, s->scope, name, NULL);
        return s->le;
    }

    f = s->cb->alloc_func(s->cb, sizeof(*f));
    if (!f) {
        SCLISP_REPORT_ERR(s, SCLISP_NOMEM, NULL);
        dtor(user);
        return s->le;
    }

    f->func = user_func;
    f->user = user;
    f->dtor = dtor;
    f->s = s;

    user_builtin = some_builtin(s, user_builtin_wrapper, f,
            user_builtin_wrapper_dtor);
    if (SCLISP_ERR_REPORTED(s)) {
        s->cb->free_func(s->cb, f);
        return s->le;
    }

    scope_set(s, s->scope, name, user_builtin);
    object_unref(s->cb, user_builtin);

    return s->le;
}

const struct sclisp_scope_api* sclisp_get_scope_api(struct sclisp *s)
{
    if (s)
        return &s->usapi;

    return NULL;
}

/* This API currently calls repr on the most recent eval result.
 * This is not necessarily how this API will work long term. Instead,
 * it may be possible to get the most recent result as a struct Object*
 * (or some other opaque reference) and this function may take a
 * parameter that is a pointer to an object of that type. */
int sclisp_repr(struct sclisp *s)
{
    char *r;

    sc_lazy_static();

    s->le = SCLISP_OK;
    s->errmsg = NULL;

    r = internal_repr(s, s->lr);

    /* TODO: Maybe differentiate this nil from allocation error? */
    sc_printf(s->cb, SCLISP_STDOUT, "%s\n", r ? r : "nil");
    s->cb->free_func(s->cb, r);

    return s->le;
}
