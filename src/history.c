/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: history.c,v 33000.4 1994/04/18 21:52:11 hawkeye Exp $ */


/****************************************************************
 * Fugue history and logging                                    *
 *                                                              *
 * Maintains the circular lists for input and output histories. *
 * Handles text queuing and file I/O for logs.                  *
 ****************************************************************/

#ifndef NO_HISTORY

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "history.h"
#include "world.h"
#include "socket.h"
#include "expand.h"	/* process_macro() */
#include "output.h"
#include "macro.h"
#include "keyboard.h"   /* handle_input_string() */
#include "commands.h"
#include "search.h"     /* List in recall_history() */

#define mod(x, r)   (((x) >= 0) ? ((x)%(r)) : ((r) - (-(x))%(r)))
#define empty(hist) (!(hist)->alines || !(hist)->size)

static void     FDECL(alloc_history,(History *hist, int maxsize));
static History *FDECL(parse_hist_opts,(char **argp));
static void     FDECL(recordline,(History *hist, Aline *aline));
static void     FDECL(save_to_hist,(History *hist, Aline *aline));
static void     FDECL(save_to_log,(TFILE *logfile, char *str));
static void     FDECL(hold_input,(char *str));
static int      FDECL(check_watchname,(History *hist));
static int      FDECL(check_watchdog,(History *hist, char *str));
static void     FDECL(listlog,(World *world));

static History input[1], global[1], local[1];
static Aline *blankline;
static int norecord = 0;         /* supress history (but not log) recording */
static int nolog = 0;            /* supress log (but not history) recording */

int log_count = 0;

extern int restrict;
extern Stringp keybuf;

void init_history(hist, maxsize)
    History *hist;
    int maxsize;
{
    hist->logfile = NULL;
    hist->pos = hist->index = -1;
    hist->size = hist->num = 0;
    alloc_history(hist, maxsize);
}

static void alloc_history(hist, maxsize)
    History *hist;
    int maxsize;
{
    hist->alines = maxsize ? (Aline**)MALLOC(maxsize * sizeof(Aline *)) : NULL;
    hist->maxsize = maxsize;
}

void init_histories()
{
    init_history(input, SAVEINPUT);
    init_history(global, SAVEGLOBAL);
    init_history(local, SAVELOCAL);
    (blankline = new_aline("", 0))->links = 1;
    save_to_hist(input, blankline);
    input->index = input->pos;
}

#ifdef DMALLOC
void free_histories()
{
    free_history(input);
    free_history(global);
    free_history(local);
    free_aline(blankline);
}
#endif

void free_history(hist)
    History *hist;
{
    int i;

    if (hist->alines) {
        for (i = 0; i < hist->size; i++) free_aline(hist->alines[i]);
        FREE(hist->alines);
        if (hist->logfile) {
            tfclose(hist->logfile);
            if (!--log_count) status_bar(STAT_LOGGING);
        }
    }
}

static void save_to_hist(hist, aline)
    History *hist;
    Aline *aline;
{
    if (!hist->alines) alloc_history(hist, SAVEWORLD);
    hist->pos = mod(hist->pos + 1, hist->maxsize);
    if (hist->size < hist->maxsize) hist->size++;
    else free_aline(hist->alines[hist->pos]);
    (hist->alines[hist->pos] = aline)->links++;
    hist->num++;
}

static void save_to_log(logfile, str)
    TFILE *logfile;
    char *str;
{
    if (wraplog) {
        /* ugly, but some people want it */
        char savech, *next = str;
        int i, first = TRUE;
        unsigned int len;
        do {
            if (!first && wrapflag)
                for (i = wrapspace; i; --i) tfputc(' ', logfile);
            str = wrap(&next, &len, &first);
            savech = str[len];
            str[len] = '\0';
            tfputs(str, logfile);
            tfflush(logfile);
            str[len] = savech;
        } while (*next);
    } else {
        tfputs(str, logfile);
        tfflush(logfile);
    }
}

static void recordline(hist, aline)
    History *hist;
    Aline *aline;
{
    if (!(aline->attrs & F_NOHISTORY) && !norecord) save_to_hist(hist, aline);
    if (hist->logfile && !nolog) save_to_log(hist->logfile, aline->str);
}

void record_global(aline)
    Aline *aline;
{
    recordline(global, aline);
}

void record_local(aline)
    Aline *aline;
{
    recordline(local, aline);
}

void record_hist(hist, aline)
    History *hist;
    Aline *aline;
{
    if (hist) recordline(hist, aline);
}

static void hold_input(str)
    char *str;
{
    free_aline(input->alines[input->pos]);
    (input->alines[input->pos] = new_aline(str, 0))->links++;
}

void record_input(str)
    char *str;
{
    if (*str) {
        hold_input(str);
        save_to_hist(input, blankline);
        if (input->logfile && !nolog) save_to_log(input->logfile, str);
    }
    input->index = input->pos;
}

int recall_input(dir, searchflag)
    int dir;
    int searchflag;
{
    int i;
    int len, stop;
    char *pattern;

    if (input->size == 1) return 0;
    if (input->index == input->pos) hold_input(keybuf->s);

    if (searchflag) {
        pattern = input->alines[input->pos]->str;
        len = strlen(input->alines[input->pos]->str);
    }
    
    i = input->index;
    stop = (dir < 0) ? input->pos : mod(input->pos + 1, input->size);
    while ((i = mod(i + dir, input->size)) != stop) {
        if (!searchflag || strncmp(input->alines[i]->str, pattern, len) == 0) {
            input->index = i;
            dokey_dline();
            handle_input_string(input->alines[i]->str, input->alines[i]->len);
            return 1;
        }
    }
    if (beep) bell(1);
    return 0;
}

int recall_history(args, file)
    char *args;
    TFILE *file;
{
    int n0, n1, n_or_t = 0, colon, i, mflag = matching, want;
    TIME_T t0, t1;
    TIME_T now = time(NULL);
    short numbers = FALSE, timestamps = FALSE, attrs = 0;
    char opt, *arg;
    Pattern pat;
    World *world = xworld();
    History *hist = NULL;
    Aline *aline;
    extern TFILE *tfscreen;
    extern char *enum_match[];
    static Aline *startmsg = NULL, *endmsg = NULL;
    static List stack[1];
    STATIC_BUFFER(buffer);

    init_pattern(&pat, NULL, 0);
    if (!startmsg) {
        startmsg = new_aline("---- Recall start ----", 0);
        endmsg = new_aline("----- Recall end -----", 0);
        startmsg->links = endmsg->links = 1;
        init_list(stack);
    }
    startopt(args, "a:f:w:lgitm:");
    while ((opt = nextopt(&arg, &i))) {
        switch (opt) {
        case 'w':
            if (!*arg || (world = find_world(arg)) == NULL) {
                tfprintf(tferr, "%% No world %s", arg);
                return 0;
            }
            hist = world->history;
            break;
        case 'l':
            hist = local;
            break;
        case 'g':
            hist = global;
            break;
        case 'i':
            hist = input;
            break;
        case 'a': case 'f':
            if ((i = parse_attrs(&arg)) < 0) return 0;
            attrs |= i;
            break;
        case 't':
            timestamps = TRUE;
            break;
        case 'm':
            if ((mflag = enum2int(arg, enum_match, "-m")) < 0) return 0;
            break;
        default: return 0;
        }
    }
    if (!hist) hist = world ? world->history : global;
    if (empty(hist)) return 0;
    if (arg && *arg == '#') {
        numbers = TRUE;
        arg++;
    }

    t0 = 0;
    t1 = now;
    n0 = 0;
    n1 = hist->num - 1;
    want = hist->size;

    if (!arg || !*arg) {
        n_or_t = -1;  /* flag syntax error */
    } else if (*arg == '-') {                                 /*  -y */
        ++arg;
        n_or_t = parsetime(&arg, &colon);
        if (colon) t1 = abstime(n_or_t);
        else n0 = n1 = hist->num - n_or_t;
    } else if (*arg == '/') {                                 /*  /x */
        want = atoi(++arg);
        while (isdigit(*arg)) arg++;
    } else if (isdigit(*arg)) {
        n_or_t = parsetime(&arg, &colon);
        if (n_or_t < 0) {
            /* error */
        } else if (*arg != '-') {                             /* x   */
            if (colon) t0 = t1 - n_or_t;
            else n0 = hist->num - n_or_t;
        } else if (isdigit(*++arg)) {                         /* x-y */
            if (colon) t0 = abstime(n_or_t);
            else n0 = n_or_t - 1;
            n_or_t = parsetime(&arg, &colon);
            if (colon) t1 = abstime(n_or_t);
            else n1 = n_or_t - 1;
        } else {                                              /* x-  */
            if (colon) t0 = abstime(n_or_t);
            else n0 = n_or_t - 1;
        }
    }
    if (n_or_t < 0 || (arg && *arg && *arg != ' ')) {
        tfputs("% Bad recall syntax.", tferr);
        return 0;
    }
    if (arg && (arg = strchr(arg, ' ')) != NULL) {
        *arg++ = '\0';
        if (!init_pattern(&pat, arg, mflag)) return 0;
    }

    if (!file) file = tfout;
    if (file == tfscreen) {
        norecord++;                     /* don't save this output in history */
        tfputa(startmsg, file);
    }

    if (n0 < hist->num - hist->size) n0 = hist->num - hist->size;
    if (n1 >= hist->num) n1 = hist->num - 1;
    if (n0 <= n1 && t0 <= t1) {
        n0 = mod(n0, hist->size);
        n1 = mod(n1, hist->size);
        attrs = ~attrs | F_NORM;

        if (hist == input) hold_input(keybuf->s);
        for (i = n1; want > 0; i = mod(i - 1, hist->size)) {
            if (i == n0) want = 0;
            aline = hist->alines[i];
            if (aline->time > t1) continue;
            if (aline->time < t0) break;
            if (gag && (aline->attrs & F_GAG & attrs)) continue;
            if (!patmatch(&pat, aline->str, mflag, FALSE)) continue;
            want--;
            Stringterm(buffer, 0);
            if (numbers)
                Sprintf(buffer, SP_APPEND, "%d: ",
                    hist->num - mod(hist->pos - i, hist->size));
            if (timestamps) {
                Sprintf(buffer, SP_APPEND, "[%s] ", tftime("", aline->time));
            }
            /* share aline if possible: copy only if different */
            /* BUG: partials don't get copied.  To do so we would have
             * to malloc; copy, shifted right by buffer->len; and fill
             * in the first buffer->len attrs with 0.
             */
            if (timestamps || numbers) {
                Stringcat(buffer, aline->str);
                aline = new_aline(buffer->s, aline->attrs & attrs);
            } else if (aline->attrs & ~attrs & F_ATTR) {
                aline = new_aline(aline->str, aline->attrs & attrs);
            }

            inlist((GENERIC*)aline, stack, NULL);
        }
    }

    while (stack->head)
        tfputa((Aline *)unlist(stack->head, stack), file);

    if (mflag == 2) regrelease();
    free_pattern(&pat);

    if (file == tfscreen) {
        tfputa(endmsg, file);
        norecord--;
    }
    return 1;
}

static int check_watchname(hist)
    History *hist;
{
    extern int wnmatch, wnlines;
    int nmatches = 1, i, slines;
    char *line, *name, *end, c;
    STATIC_BUFFER(buffer);

    slines = (wnlines > hist->size) ? hist->size : wnlines;
    name = hist->alines[mod(hist->pos, hist->size)]->str;
    if (*name == ' ') return 0;
    for (end = name; *end && !isspace(*end); ++end);
    for (i = 1; i < slines; i++) {
        line = hist->alines[mod(hist->pos - i, hist->size)]->str;
        if (!strncmp(line, name, end - name) && (++nmatches == wnmatch)) break;
    }
    if (nmatches < wnmatch) return 0;
    c = *end;
    *end = '\0';
    Sprintf(buffer, 0, "{%s}*", name);
    *end = c;
    oprintf("%% Watchname: gagging \"%S\"", buffer);
    return add_macro(new_macro("",buffer->s,"",0,NULL,"",gpri,100,F_GAG,0));
}

static int check_watchdog(hist, str)
    History *hist;
    char *str;
{
    extern int wdmatch, wdlines;
    int nmatches = 0, i, slines;
    char *line;

    if (wdlines > hist->size) slines = hist->size;
    else slines = wdlines;
    for (i = 1; i < slines; i++) {
        line = hist->alines[mod(hist->pos - i, hist->size)]->str;
        if (!cstrcmp(line, str) && (nmatches++ == wdmatch)) return 1;
    }
    return 0;
}

int is_suppressed(str)
    char *str;
{
    extern Sock *xsock;

    if (empty(xsock->world->history)) return 0;
    return ((watchname && check_watchname(xsock->world->history)) ||
            (watchdog && check_watchdog(xsock->world->history, str)));
}

int history_sub(pattern)
    char *pattern;
{
    int size = input->size, pos = input->pos, i;
    Aline **L = input->alines;
    char *replace, *loc = NULL;
    STATIC_BUFFER(buffer);

    if (empty(input) || !*pattern) return 0;
    if ((replace = strchr(pattern, '^')) == NULL) return 0;
    *replace = '\0';
    for (i = 0; i < size; i++)
        if ((loc = STRSTR(L[mod(pos - i, size)]->str, pattern)) != NULL)
            break;
    *replace++ = '^';
    if (i == size) return 0;
    i = mod(pos - i, size);
    Stringterm(buffer, 0);
    Stringncat(buffer, L[i]->str, loc - L[i]->str);
    Stringcat(buffer, replace);
    Stringcat(buffer, loc + ((replace - 1) - pattern));
    record_input(buffer->s);
    return process_macro(buffer->s, NULL, sub);
}

static void listlog(world)
    World *world;
{
    if (world->history->logfile)
        oprintf("%% Logging world %s output to %s",
          world->name, world->history->logfile->name);
}

static History *parse_hist_opts(argp)
    char **argp;
{
    History *history = global;
    World *world;
    char c;

    startopt(*argp, "lgiw:");
    while ((c = nextopt(argp, NULL))) {
        switch (c) {
        case 'l':
            history = local;
            break;
        case 'i':
            history = input;
            break;
        case 'g':
            history = global;
            break;
        case 'w':
            if (!**argp) world = xworld();
            else world = find_world(*argp);
            if (!world) {
                tfprintf(tferr, "%% No world %s", *argp);
                history = NULL;
            } else history = world->history;
            break;
        default:
            return NULL;
        }
    }
    return history;
}

int handle_recordline_command(args)
    char *args;
{
    History *history;

    nolog++;
    if ((history = parse_hist_opts(&args))) {
        if (history == input) record_input(args);
        else recordline(history, new_aline(args, 0));
    }
    nolog--;
    return history ? 1 : 0;
}

int handle_log_command(args)
    char *args;
{
    History *history;
    TFILE *logfile;

    if (restrict >= RESTRICT_FILE) {
        tfputs("% /log: restricted", tferr);
        return 0;
    }

    if (!(history = parse_hist_opts(&args))) return 0;
    if (!*args) {
        if (log_count) {
            if (input->logfile)
                oprintf("%% Logging input to %s", input->logfile->name);
            if (local->logfile)
                oprintf("%% Logging local output to %s", local->logfile->name);
            if (global->logfile)
                oprintf("%% Logging global output to %s",global->logfile->name);
            mapsock(listlog);
        } else {
            oputs("% Logging disabled.");
        }
        return 1;
    } else if (cstrcmp(args, "OFF") == 0) {
        if (history->logfile) {
            tfclose(history->logfile);
            history->logfile = NULL;
            if (!--log_count) status_bar(STAT_LOGGING);
        }
        return 1;
    } else if (cstrcmp(args, "ON") == 0) {
        logfile = tfopen(tfname(NULL, "LOGFILE"), "a");
    } else {
        logfile = tfopen(expand_filename(args), "a");
    }
    if (!logfile) {
        operror(args);
        return 0;
    }
    if (history->logfile) {
        tfclose(history->logfile);
        history->logfile = NULL;
        log_count--;
    }
    do_hook(H_LOG, "%% Logging to file %s", "%s", logfile->name);
    history->logfile = logfile;
    if (!log_count++) status_bar(STAT_LOGGING);
    return 1;
}

#endif /* NO_HISTORY */
