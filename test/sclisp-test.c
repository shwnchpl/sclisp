#include "sclisp.h"

#include <stdio.h>

int main(void)
{
    struct sclisp* s;

    sclisp_init(&s, NULL);
    sclisp_eval(s, "(1 2 3)");
    sclisp_destroy(s);

    printf("Ok\n");

    return 0;
}
