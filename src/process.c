/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: process.c,v 35004.30 1999/01/31 00:27:50 hawkeye Exp $ */

/************************
 * Fugue processes.     *
 ************************/

#ifndef NO_PROCESS

#include "config.h"
#include <sys/types.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "history.h"
#include "world.h"
#include "process.h"
#include "socket.h"
#include "expand.h"
#include "commands.h"
#include "output.h"  /* oflush() */

#define P_REPEAT     'R'
#define P_QFILE      '\''
#define P_QSHELL     '!'
#define P_QRECALL    '#'
#define P_QLOCAL     '`'

#define PROC_DEAD     0
#define PROC_RUNNING  1

CONST char *enum_disp[] = { "echo", "send", "exec", NULL };
enum { DISP_ECHO, DISP_SEND, DISP_EXEC };

typedef struct Proc {
    int pid;
    char type;
    char state;
    int disp;           /* disposition: what to do with the generated text */
    int count;
    int FDECL((*func),(struct Proc *proc));
    TIME_T ptime;	/* delay.  -1 == global %ptime, -2 == synchrounous */
    TIME_T timer;	/* time of next execution */
    char *pre;		/* prefix string */
    char *suf;		/* suffix string */
    TFILE *input;	/* source of quote input */
    struct World *world;/* where to send output */
    char *cmd;		/* command or file name */
    Stringp buffer;     /* buffer for prefix+cmd+suffix */
    struct Proc *next, *prev;
} Proc;

static struct Value *FDECL(newproc,(int type, int FDECL((*func),(Proc *proc)),
                 int count, CONST char *pre, CONST char *suf, TFILE *input,
                 struct World *world, CONST char *cmd, TIME_T ptime, int disp));
static struct Value *FDECL(killproc,(Proc *proc, int needresult));

static void FDECL(nukeproc,(Proc *proc));
static int  FDECL(runproc,(Proc *proc));
static int  FDECL(do_repeat,(Proc *proc));
static int  FDECL(do_quote,(Proc *proc));
static void FDECL(strip_escapes,(char *src));
static int  FDECL(procopt,(CONST char *opts, char **argp, TIME_T *ptime,
    struct World **world, int *disp, int *subflag));

TIME_T proctime = 0;              /* when next process should be run */

static int runall_depth = 0;
static Proc *proclist = NULL;     /* procedures to execute */

struct Value *handle_ps_command(args)
    char *args;
{
    Proc *p;
    char obuf[18], nbuf[10];
    TIME_T now, next;
    int opt, shortflag = FALSE, repeatflag = FALSE, quoteflag = FALSE;
    struct World *world = NULL;

    startopt(args, "srqw:");
    while ((opt = nextopt(&args, NULL))) {
        switch(opt) {
        case 's': shortflag = TRUE; break;
        case 'r': repeatflag = TRUE; break;
        case 'q': quoteflag = TRUE; break;
        case 'w':
            if (!(world = (*args) ? find_world(args) : xworld())) {
                eprintf("No world %s", args);
                return newint(0);
            }
            break;
        default:  return newint(0);
        }
    }

    now = time(NULL);
    if (!repeatflag && !quoteflag)
        repeatflag = quoteflag = TRUE;
    if (!shortflag)
        oprintf("  PID     NEXT TYPE   DISP WORLD       PTIME COUNT COMMAND");

    for (p = proclist; p; p = p->next) {
        if (p->state == PROC_DEAD) continue;
        if (!repeatflag && p->type == P_REPEAT) continue;
        if (!quoteflag && p->type != P_REPEAT) continue;
        if (world && p->world != world) continue;

        if (shortflag) {
            oprintf("%d", p->pid);
            continue;
        }

        if (p->ptime == -2) {
            strcpy(nbuf, "     0");
        } else if (lpquote) {
            strcpy(nbuf, "     ?");
        } else {
            next = p->timer - now;
            if (next >= 0)
                sprintf(nbuf, "%2ld:%02ld:%02ld",
                    (long)next/3600, (long)(next/60) % 60, (long)next % 60);
            else
                sprintf(nbuf, "%8s", "pending");
        }
        sprintf(obuf, "%-8s ", p->world ? p->world->name : "");
        if (p->ptime >= 0) {
            sprintf(obuf+9, "%2ld:%02ld:%02ld", (long)p->ptime/3600,
                (long)(p->ptime/60) % 60, (long)p->ptime % 60);
        } else {
            strcpy(obuf+9, p->ptime == -2 ? "S       " : "        ");
        }
        if (p->type == P_REPEAT) {
            oprintf("%5d %s repeat      %s %5d %s",
                p->pid, nbuf, obuf, p->count, p->cmd);
        } else {
            oprintf("%5d %s quote  %s %s       %s%c\"%s\"%s",
                p->pid, nbuf, enum_disp[p->disp], obuf, p->pre, p->type,
                p->cmd, p->suf);
        }
    }
    return newint(1);
}

static struct Value *newproc(type, func, count, pre, suf, input, world, cmd, ptime, disp)
    int type, count, disp;
    TIME_T ptime;
    int FDECL((*func),(Proc *proc));
    CONST char *pre, *suf, *cmd;
    TFILE *input;
    struct World *world;
{
    Proc *proc;
    static int hipid = 0;

    if (!(proc = (Proc *) MALLOC(sizeof(Proc)))) {
        eprintf("newproc: not enough memory");
        return newint(0);
    }

    proc->disp = disp;
    proc->count = count;
    proc->func = func;
    proc->ptime = ptime;
    proc->type = type;
    proc->timer = time(NULL) + ((ptime < 0) ? process_time : ptime);
    proc->pre = STRDUP(pre);
    proc->suf = suf ? STRDUP(suf) : NULL;
    proc->cmd = STRDUP(cmd);
    proc->pid = ++hipid;
    proc->input = input;
    proc->world = world;
    Stringzero(proc->buffer);

    if (proclist) proclist->prev = proc;
    proc->next = proclist;
    proc->prev = NULL;
    proclist = proc;
    proc->state = PROC_RUNNING;
    do_hook(H_PROCESS, NULL, "%d", proc->pid);
    if (ptime == -2) {  /* synch */
        oflush();  /* flush now, process might take a while */
        while (runproc(proc));
        return killproc(proc, 1);  /* no nuke! */
    }
    if (lpquote) {
        proctime = time(NULL);
    } else if (proctime == 0 || proc->timer < proctime) {
        proctime = proc->timer;
    }
    return newint(proc->pid);
}

static struct Value *killproc(proc, needresult)
    Proc *proc;
    int needresult;
{
    int result = 1;

    proc->state = PROC_DEAD;
    do_hook(H_KILL, NULL, "%d", proc->pid);

    if (proc->type == P_QSHELL) readers_clear(fileno(proc->input->u.fp));
    if (proc->input) result = tfclose(proc->input);

    if (proc->type == P_QFILE) result = result + 1;

    if (!needresult) return NULL;
    if (proc->type == P_QLOCAL || proc->type == P_REPEAT) return_user_result();
    else return newint(result);
}

static void nukeproc(proc)
    Proc *proc;
{
    if (proc->next) proc->next->prev = proc->prev;
    if (proc->prev) proc->prev->next = proc->next;
    else proclist = proc->next;

    FREE(proc->pre);
    FREE(proc->cmd);
    if (proc->suf) FREE(proc->suf);
    Stringfree(proc->buffer);
    FREE(proc);
}

void nuke_dead_procs()
{
    Proc *proc, *next;

    for (proc = proclist; proc; proc = next) {
        next = proc->next;
        if (proc->state == PROC_DEAD)
            nukeproc(proc);
    }
}

void kill_procs()
{
    while (proclist) {
        if (proclist->state != PROC_DEAD) {
            if (proclist->type == P_QSHELL)
                readers_clear(fileno(proclist->input->u.fp));
            if (proclist->input)
                tfclose(proclist->input);
        }
        nukeproc(proclist);
    }

    proctime = 0;
}

void kill_procs_by_world(world)
    struct World *world;
{
    Proc *proc;

    for (proc = proclist; proc; proc = proc->next) {
        if (proc->world == world) killproc(proc, 0);
    }
}

struct Value *handle_kill_command(args)
    char *args;
{
    Proc *proc;
    int pid, error = 0;

    while (*args) {
        if ((pid = numarg(&args)) < 0) return 0;
        for (proc = proclist; proc && (proc->pid != pid); proc=proc->next);
        if (!proc || proc->state == PROC_DEAD) {
            eprintf("no process %d", pid);
            error++;
        } else {
            killproc(proc, 0);
        }
    }
    return newint(!error);
}

/* Run all processes that should be run, and set proctime to the time
 * of the next earliest process.
 */
int runall()
{
    Proc *proc;
    TIME_T now = time(NULL);
    int resched;	/* consider this process in proctime calculation? */

    runall_depth++;
    proctime = 0;
    for (proc = proclist; proc; proc = proc->next) {
        if (proc->state == PROC_DEAD) continue;
        if (proc->type == P_QSHELL) {
            if (is_active(fileno(proc->input->u.fp))) {
                if (!(resched = runproc(proc)))
                    killproc(proc, 0);  /* no nuke! */
            } else if (lpquote || (proc->timer <= now)) {
                resched = FALSE;
                readers_set(fileno(proc->input->u.fp));
            } else resched = TRUE;
        } else if (lpquote || (proc->timer <= now)) {
            if (!(resched = runproc(proc)))
                killproc(proc, 0);  /* no nuke! */
        } else resched = TRUE;

        if (resched && !lpquote && (!proctime || (proc->timer < proctime))) {
            proctime = proc->timer;
        }
    }
    runall_depth--;
    return 1;  /* for setvar() call */
}

static int runproc(p)
    Proc *p;
{
    int done;
    struct Sock *oldsock;

    oldsock = xsock;
    if (p->world) xsock = p->world->sock;
    done = !(*p->func)(p);
    xsock = oldsock;
    if (!done && p->ptime != -2) {   /* not synch */
        p->timer = time(NULL) + ((p->ptime < 0) ? process_time : p->ptime);
    }
    return !done;
}

/* do_repeat
 * Returns 0 if proc is done, nonzero otherwise.
 */
static int do_repeat(proc)
    Proc *proc;
{
    process_macro(proc->cmd, NULL, SUB_MACRO, "\bREPEAT");
    return --proc->count;
}

/* do_quote
 * Returns 0 if proc is done, nonzero otherwise.
 */
static int do_quote(proc)
    Proc *proc;
{
    STATIC_BUFFER(line);

    if (!tfgetS(line, proc->input)) return 0;
    if (proc->type == P_QSHELL) readers_clear(fileno(proc->input->u.fp));
    Sprintf(proc->buffer, 0, "%s%S%s", proc->pre, line, proc->suf);
    if (qecho) tfprintf(tferr, "%s%S", qprefix ? qprefix : "", proc->buffer);
    switch (proc->disp) {
    case DISP_ECHO:
        oputs(proc->buffer->s);
        break;
    case DISP_SEND:
        process_macro(proc->buffer->s, NULL, SUB_LITERAL, "\bQUOTE");
        break;
    case DISP_EXEC:
        process_macro(proc->buffer->s, NULL, SUB_KEYWORD, "\bQUOTE");
        break;
    }
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

static int procopt(opts, argp, ptime, world, disp, subflag)
    CONST char *opts;
    char **argp;
    TIME_T *ptime;
    struct World **world;
    int *disp, *subflag;
{
    char opt, *ptr;
    long num;

    *world = NULL;
    *ptime = -1;
    startopt(*argp, opts);
    while ((opt = nextopt(&ptr, &num))) {
        switch(opt) {
        case 'w':
            if (!(*world = (*ptr) ? find_world(ptr) : xworld())) {
                eprintf("No world %s", ptr);
                return FALSE;
            }
            break;
        case 's':
            if ((*subflag = enum2int(ptr, enum_sub, "-s")) < 0)
                return FALSE;
            break;
        case 'S':
            *ptime = -2; /* synch */
            break;
        case 'd':
            if ((*disp = enum2int(ptr, enum_disp, "-d")) < 0)
                return FALSE;
            break;
        case '@':
            *ptime = num;
            break;
        default:  return FALSE;
        }
    }
    *argp = ptr;
    return TRUE;
}

struct Value *handle_quote_command(args)
    char *args;
{
    char *pre, *cmd, *suf = NULL;
    STATIC_BUFFER(newcmd);
    TFILE *input, *oldout, *olderr;
    int type, result;
    TIME_T ptime;
    int disp = -1, subflag = SUB_MACRO;
    struct World *world;

    if (!*args || !procopt("@Sw:d:s:", &args, &ptime, &world, &disp, &subflag))
        return newint(0);

    pre = args;
    while (*args != '\'' && *args != '!' && *args != '#' && *args != '`') {
        if (*args == '\\') args++;
        if (!*args) {
            eprintf("missing command character");
            return newint(0);
        }
        args++;
    }
    type = *args;
    *args = '\0';
    strip_escapes(pre);
    if (*++args == '"') {
        cmd = ++args;
        if ((args = estrchr(args, '"', '\\'))) {
            *args = '\0';
            suf = args + 1;
            strip_escapes(suf);
        }
        strip_escapes(cmd);
    } else {
        cmd = args;
    }

    switch (type) {
    case P_QFILE:
        if (restriction >= RESTRICT_FILE) {
            eprintf("files restricted");
            return newint(0);
        }
        cmd = expand_filename(cmd);
        if ((input = tfopen(cmd, "r")) == NULL) {
            operror(cmd);
            return newint(0);
        }
        break;
    case P_QSHELL:
        if (restriction >= RESTRICT_SHELL) {
            eprintf("shell restricted");
            return newint(0);
        }
        /* null input, and capture stderr */
#ifdef PLATFORM_UNIX
        Sprintf(newcmd, 0, "{ %s; } </dev/null 2>&1", cmd);
#endif
#ifdef PLATFORM_OS2
        Sprintf(newcmd, 0, "( %s ) <nul 2>&1", cmd);
#endif
        if ((input = tfopen(newcmd->s, "p")) == NULL) {
            operror(cmd);
            return newint(0);
        }
        break;
#ifndef NO_HISTORY
    case P_QRECALL:
        oldout = tfout;
        olderr = tferr;
        tfout = input = tfopen(NULL, "q");
        /* tferr = input; */
        result = do_recall(cmd);
        tferr = olderr;
        tfout = oldout;
        if (!result) {
            tfclose(input);
            return newint(0);
        }
        break;
#endif
    case P_QLOCAL:
        oldout = tfout;
        olderr = tferr;
        tfout = input = tfopen(NULL, "q");
        /* tferr = input; */
        process_macro(cmd, NULL, subflag, "\bQUOTE");
        tferr = olderr;
        tfout = oldout;
        break;
    default:    /* impossible */
        return newint(0);
    }
    return newproc(type, do_quote, -1, pre, suf, input, world, cmd,
        ptime, (disp >= 0) ? disp : (*pre ? DISP_EXEC : DISP_SEND));
}

struct Value *handle_repeat_command(args)
    char *args;
{
    int count;
    TIME_T ptime;
    struct World *world;

    if (!*args || !procopt("@Sw:", &args, &ptime, &world, NULL, NULL)) return 0;
    if ((count = numarg(&args)) <= 0) return 0;
    return newproc(P_REPEAT, do_repeat, count, "", "", NULL, world, args,
        ptime, DISP_ECHO);
}

#endif /* NO_PROCESS */
