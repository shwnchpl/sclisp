/**********************************************************************
* Copyright (C) 2020 Shawn M. Chapla
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License
* as published by the Free Software Foundation; either version 2
* of the License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA
* 02110-1301, USA.
***********************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include <readline/history.h>
#include <readline/readline.h>

#include "sclisp.h"

static int paren_balence_cr(int count, int key)
{
    char *buf = rl_line_buffer;
    int pcount = 0;

    (void)count;
    (void)key;

    while (*buf) {
        if (*buf == '(') ++pcount;
        else if  (*buf == ')') --pcount;
        ++buf;
    }

    if (!pcount) {
        rl_done = 1;
        printf("\n");
    } else
        rl_insert(count, '\n');

    return 0;
}

static int startup_hook(void)
{
    rl_variable_bind("blink-matching-paren", "on");
    rl_set_paren_blink_timeout(200000);  /* microseconds */

    rl_bind_key('\t', rl_complete);
    rl_bind_key('\n', paren_balence_cr);
    rl_bind_key('\r', paren_balence_cr);

    return 0;
}

int main(void)
{
    struct sclisp *s;

    printf("SCLisp repl. Copyright 2020 Shawn M. Chapla.\n"
            "Linked against SCLisp version %s (%lu)\n\n", SCLISP_VERSION,
            SCLISP_VERSION_NUMBER);

    rl_startup_hook = startup_hook;
    sclisp_init(&s, NULL);

    while (1) {
        char* input = readline("sclisp> ");
        int res;

        if (!input)
            break;

        if (strlen(input) > 0)
            add_history(input);

        res = sclisp_eval(s, input);
        if (res) {
            const char* errstr = sclisp_errstr(res);
            const char* errmsg = sclisp_errmsg(s);
            printf("ERROR (%s): %s\n", errstr ? errstr : "",
                    errmsg ? errmsg : "");
        }
        sclisp_repr(s);

        free(input);
    }

    printf("\n");

    sclisp_destroy(s);

    return 0;
}
