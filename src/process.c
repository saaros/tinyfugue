/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: process.c,v 33000.2 1994/03/14 16:57:06 hawkeye Exp $ */

/*************************************************
 * Fugue processes.                              *
 *                                               *
 * Rewritten by Ken Keys. Originally written by  *
 * Leo Plotkin and modified by Greg Hudson.      *
 * Handles /repeat and /quote processes.         *
 *************************************************/

#ifndef NO_PROCESS

#include "config.h"
#include <ctype.h>
#include <sys/types.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "process.h"
#include "util.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "expand.h"
#include "macro.h"
#include "commands.h"

#define P_REPEAT     '\0'
#define P_QFILE      '\''
#define P_QCOMMAND   '!'
#define P_QRECALL    '#'
#define P_QLOCAL     '`'

typedef struct Proc {
    int pid;
    char type;
    int count;
    int FDECL((*func),(struct Proc *proc));
    int ptime;
    TIME_T timer;       /* time of next execution */
    char *pre;          /* what to prefix */
    char *suf;          /* what to suffix */
    TFILE *input;       /* source of quote input */
    struct World *world;/* where to send output */
    char *cmd;          /* command or file name */
    struct Proc *next, *prev;
} Proc;

static int  FDECL(newproc,(int type, int FDECL((*func),(Proc *proc)),
                           int count, char *pre, char *suf, TFILE *input,
                           struct World *world, char *cmd, int ptime));
static void FDECL(removeproc,(Proc *proc));
static void FDECL(freeproc,(Proc *proc));
static int  FDECL(runproc,(Proc *proc));
static int  FDECL(do_repeat,(Proc *proc));
static int  FDECL(do_quote,(Proc *proc));
static void FDECL(strip_escapes,(char *src));
static int  FDECL(procopt,(char **argp, int *ptime, struct World **world));

TIME_T proctime = 0;              /* when next process should be run */

extern int restrict;

static Proc *proclist = NULL;     /* procedures to execute */

int handle_ps_command(args)
    char *args;
{
    Proc *p;
    char buf[11];

    oprintf("  PID TYPE    WORLD      PTIME COUNT COMMAND");
    for (p = proclist; p; p = p->next) {
        if (p->world) sprintf(buf, "-w%-8s", p->world->name);
        else sprintf(buf, "%10s", "");
        if (p->type == P_REPEAT) {
            oprintf("%5d /repeat %s -%-4d %5d %s", p->pid, buf,
            (p->ptime < 0) ? process_time : p->ptime, p->count, p->cmd);
        } else {
            oprintf("%5d /quote  %s -%-4d       %s%c\"%s\"%s", p->pid, buf,
                (p->ptime < 0) ? process_time : p->ptime, p->pre, p->type,
                p->cmd, p->suf);
        }
    }
    return 1;
}

static int newproc(type, func, count, pre, suf, input, world, cmd, ptime)
    int type, count, ptime;
    int FDECL((*func),(Proc *proc));
    char *pre, *suf, *cmd;
    TFILE *input;
    struct World *world;
{
    Proc *proc;
    static int pid = 0;

    proc = (Proc *) MALLOC(sizeof(Proc));

    proc->count = count;
    proc->func = func;
    proc->ptime = ptime;
    proc->type = type;
    proc->timer = time(NULL) + ((ptime < 0) ? process_time : ptime);
    proc->pre = STRDUP(pre);
    proc->suf = STRDUP(suf);
    proc->cmd = STRDUP(cmd);
    proc->pid = ++pid;
    proc->input = input;
    proc->world = world;

    if (proclist) proclist->prev = proc;
    proc->next = proclist;
    proc->prev = NULL;
    proclist = proc;
    if (proctime == 0 || proc->timer < proctime) proctime = proc->timer;
    do_hook(H_PROCESS, NULL, "%d", proc->pid);
    return proc->pid;
}

static void freeproc(proc)
    Proc *proc;
{
    do_hook(H_KILL, NULL, "%d", proc->pid);
    if (proc->type == P_QCOMMAND) readers_clear(fileno(proc->input->u.fp));
    if (proc->input) tfclose(proc->input);
    FREE(proc->pre);
    FREE(proc->suf);
    FREE(proc->cmd);
    FREE(proc);
}

static void removeproc(proc)
    Proc *proc;
{
    if (proc->next) proc->next->prev = proc->prev;
    if (proc->prev) proc->prev->next = proc->next;
    else proclist = proc->next;
}

void kill_procs()
{
    Proc *next;

    for (; proclist; proclist = next) {
        next = proclist->next;
        freeproc(proclist);
    }
    proctime = 0;
}

void kill_procs_by_world(world)
    struct World *world;
{
    Proc *proc, *next;

    if (!proclist) return;
    for (proc = proclist; proc; proc = next) {
        next = proc->next;
        if (proc->world == world) {
            removeproc(proc);
            freeproc(proc);
        }
    }
}

int handle_kill_command(args)
    char *args;
{
    Proc *proc;
    int pid, error = 0;

    while (*args) {
        if ((pid = numarg(&args)) < 0) {
            return 0;
        } else {
            for (proc = proclist; proc && (proc->pid != pid); proc=proc->next);
            if (!proc) {
                tfputs("% no such process", tferr);
                error++;
            } else {
                removeproc(proc);
                freeproc(proc);
            }
        }
    }
    return !error;
}

void runall(now)
    TIME_T now;
{
    Proc *proc, *next;
    TIME_T earliest = 0;

    proctime = 0;
    for (proc = proclist; proc; proc = next) {
        next = proc->next;
        if (proc->type == P_QCOMMAND) {
            if (is_active(fileno(proc->input->u.fp))) {
                if (!runproc(proc)) proc = NULL;
            } else if (proc->timer <= now) {
                proc->timer = 0;
                readers_set(fileno(proc->input->u.fp));
            }
        } else if (lpquote || (proc->timer <= now)) {
            if (!runproc(proc)) proc = NULL;
        }

        if (proc && proc->timer && (!earliest || (proc->timer < earliest)))
            earliest = proc->timer;
    }
    /* calculate next proc (proctime may have been set by a nested process) */
    proctime = (proctime && proctime < earliest) ? proctime : earliest;
}

/* runproc
 * Returns 0 if proc is done, nonzero otherwise.
 */
static int runproc(p)
    Proc *p;
{
    int notdone;
    extern struct Sock *fsock, *xsock;

    if (p->world) xsock = p->world->sock;
    notdone = (*p->func)(p);
    xsock = fsock;
    if (notdone) {
        return p->timer =
            time(NULL) + ((p->ptime < 0) ? process_time : p->ptime);
    }
    removeproc(p);
    freeproc(p);
    return 0;
}

/* do_repeat
 * Returns 0 if proc is done, nonzero otherwise.
 */
static int do_repeat(proc)
    Proc *proc;
{
    if (proc->count--) {
        process_macro(proc->cmd, NULL, SUB_MACRO);
    }
    return proc->count;
}

/* do_quote
 * Returns 0 if proc is done, nonzero otherwise.
 */
static int do_quote(proc)
    Proc *proc;
{
    STATIC_BUFFER(line);
    STATIC_BUFFER(buffer);

    if (!tfgetS(line, proc->input)) return 0;
    if (proc->type == P_QCOMMAND) readers_clear(fileno(proc->input->u.fp));
    Sprintf(buffer, 0, "%s%S%s", proc->pre, line, proc->suf);
    if (qecho) tfprintf(tferr, "%s%S", qprefix, buffer);
    process_macro(buffer->s, NULL, SUB_NONE);
    return TRUE;

}

static void strip_escapes(src)
    char *src;
{
    char *dest;

    if (!*src) return;
    for (dest = src; *src; *dest++ = *src++) {
        if (*src == '\\') src++;
    }
    *dest = '\0';
}

static int procopt(argp, ptime, world)
    char **argp;
    int *ptime;
    struct World **world;
{
    char opt, *ptr;

    *world = NULL;
    *ptime = -1;
    startopt(*argp, "@w:");
    while ((opt = nextopt(&ptr, ptime))) {
        switch(opt) {
        case 'w':
            if (!*ptr) *world = xworld();
            else if ((*world = find_world(ptr)) == NULL) {
                tfprintf(tferr, "%% No world %s", ptr);
                return FALSE;
            }
            break;
        case '@': break;
        default:  return FALSE;
        }
    }
    *argp = ptr;
    return TRUE;
}

int handle_quote_command(args)
    char *args;
{
    char *pre, *cmd, *suf;
    STATIC_BUFFER(newcmd);
    extern TFILE *tfout, *tferr;
    TFILE *input, *oldout, *olderr;
    int type, ptime, pid;
    struct World *world;

    if (!*args || !procopt(&args, &ptime, &world)) return 0;

    pre = args;
    while (*args != '\'' && *args != '!' && *args != '#' && *args != '`') {
        if (*args == '\\') args++;
        if (!*args) {
            tfputs("% Bad /quote syntax", tferr);
            return 0;
        }
        args++;
    }
    type = *args;
    *args = '\0';
    if (*++args == '"') {
        cmd = ++args;
        if ((args = estrchr(args, '"', '\\')) == NULL) suf = "";
        else {
            *args = '\0';
            suf = args + 1;
        }
    } else {
        cmd = args;
        suf = "";
    }
    strip_escapes(pre);
    strip_escapes(suf);
    strip_escapes(cmd);
    switch (type) {
    case P_QFILE:
        if (restrict >= RESTRICT_FILE) {
            tfputs("% \"/quote '\" restricted", tferr);
            return 0;
        }
        cmd = expand_filename(cmd);
        if ((input = tfopen(cmd, "r")) == NULL) {
            operror(cmd);
            return 0;
        }
        break;
    case P_QCOMMAND:
        if (restrict >= RESTRICT_SHELL) {
            tfputs("% \"/quote !\" restricted", tferr);
            return 0;
        }
        /* null input, and capture stderr */
        Sprintf(newcmd, 0, "( %s ) </dev/null 2>&1", cmd);
        if ((input = tfopen(newcmd->s, "p")) == NULL) {
            operror(cmd);
            return 0;
        }
        break;
    case P_QRECALL:
        input = tfopen(NULL, "q");
        if (!recall_history(cmd, input)) {
            tfclose(input);
            return 0;
        }
        break;
    case P_QLOCAL:
        input = tfopen(NULL, "q");
        oldout = tfout;
        olderr = tferr;
        tfout = input;
        /* tferr = input; */
        process_macro(cmd, NULL, SUB_MACRO);
        tferr = olderr;
        tfout = oldout;
        break;
    default:    /* impossible */
        return 0;
    }
    pid = newproc(type, do_quote, -1, pre, suf, input, world, cmd, ptime);
    if (lpquote) runall(time(NULL));
    return pid;
}

int handle_repeat_command(args)
    char *args;
{
    int ptime, count, pid;
    struct World *world;

    if (!*args || !procopt(&args, &ptime, &world)) return 0;
    if ((count = numarg(&args)) <= 0) return 0;
    pid = newproc(P_REPEAT, do_repeat, count, "", "", NULL, world, args, ptime);
    if (lpquote) runall(time(NULL));
    return pid;
}

#endif /* NO_PROCESS */
