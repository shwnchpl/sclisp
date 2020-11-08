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

#ifndef SCLISP_H_
#define SCLISP_H_

#ifdef __cplusplus
extern "C" {
#endif

#define SCLISP_OK           0
#define SCLISP_ERR          1
#define SCLISP_NOMEM        2
#define SCLISP_BADARG       3
#define SCLISP_UNSUPPORTED  4
#define SCLISP_OVERFLOW     5
#define SCLISP_BUG          0xbadb01

#define SCLISP_STDOUT   1
#define SCLISP_STDERR   2

struct sclisp;

struct sclisp_cb {
    void* (*alloc_func)(struct sclisp_cb*, unsigned long);
    void* (*zalloc_func)(struct sclisp_cb*, unsigned long);
    void (*free_func)(struct sclisp_cb*, void *);
    void (*print_func)(struct sclisp_cb*, int, const char *);
    char (*getchar_func)(struct sclisp_cb*);
    void* user;
};

extern const char * const SCLISP_VERSION;
extern const unsigned long SCLISP_VERSION_NUMBER;

int sclisp_init(struct sclisp **s, struct sclisp_cb *cb);
void sclisp_destroy(struct sclisp *s);
int sclisp_eval(struct sclisp *s, const char *exp);

const char* sclisp_errstr(int errcode);
const char* sclisp_errmsg(struct sclisp *s);

/* TODO: Experimental API. Stabilize. */

struct sclisp_func_api {
    int (*arg_integer)(const struct sclisp_func_api *api, unsigned index,
            long *out);
    int (*arg_real)(const struct sclisp_func_api *api, unsigned index,
            double *out);
    int (*arg_string)(const struct sclisp_func_api *api, unsigned index,
            char **out);
    int (*return_integer)(const struct sclisp_func_api *api, long ret);
    int (*return_real)(const struct sclisp_func_api *api, double ret);
    int (*return_string)(const struct sclisp_func_api *api, char *ret);
    struct sclisp_cb *cb;
    void *inst;
};

struct sclisp_scope_api {
    int (*get_integer)(const struct sclisp_scope_api *api, char *sym,
            long *out);
    int (*get_real)(const struct sclisp_scope_api *api, char *sym,
            double *out);
    int (*get_string)(const struct sclisp_scope_api *api, char *sym,
            char **out);
    int (*set_integer)(const struct sclisp_scope_api *api, char *sym,
            long val);
    int (*set_real)(const struct sclisp_scope_api *api, char *sym, double val);
    int (*set_string)(const struct sclisp_scope_api *api, char *sym,
            char *val);
    struct sclisp_cb *cb;
    void *inst;
};

int sclisp_register_user_func(struct sclisp *s,
        int (*user_func)(const struct sclisp_func_api *api, void *user),
        const char *name, void *user, void (*dtor)(void *user));

const struct sclisp_scope_api* sclisp_get_scope_api(struct sclisp *s);

int sclisp_repr(struct sclisp *s);

#ifdef __cplusplus
}
#endif

#endif /* SCLISP_H_ */
