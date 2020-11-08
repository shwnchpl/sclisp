#ifndef SCLISP_H_
#define SCLISP_H_

struct sclisp;

struct sclisp_cb {
    void (*print_func)(const char *);
    void *(*alloc_func)(unsigned long sz);
    void (*free_func)(void *);
};

int sclisp_init(struct sclisp **s, struct sclisp_cb *cb);
void sclisp_destroy(struct sclisp *s);
int sclisp_eval(struct sclisp *s, const char *exp);

#endif /* SCLISP_H_ */
