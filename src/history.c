/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: history.c,v 35004.36 1997/12/14 21:24:42 hawkeye Exp $ */


/****************************************************************
 * Fugue history and logging                                    *
 *                                                              *
 * Maintains the circular lists for input and output histories. *
 * Handles text queuing and file I/O for logs.                  *
 ****************************************************************/

#ifndef NO_HISTORY

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "history.h"
#include "socket.h"		/* xworld() */
#include "world.h"
#include "output.h"		/* update_status_field(), etc */
#include "macro.h"		/* add_macro(), new_macro() */
#include "commands.h"
#include "search.h"		/* List in recall_history() */
#include "keyboard.h"		/* keybuf */
#include "variable.h"		/* setvar() */

#define GLOBALSIZE    1000	/* global history size */
#define LOCALSIZE      100	/* local history size */
#define INPUTSIZE      100	/* command history buffer size */

typedef struct History {	/* circular list of Alines, and logfile */
    struct Aline **alines;
    int size;			/* actual number of lines currently saved */
    int maxsize;		/* maximum number of lines that can be saved */
    int first;			/* position of first line in circular array */
    int last;			/* position of last line in circular array */
    int index;			/* current recall position */
    int total;			/* total number of lines ever saved */
    TFILE *logfile;
    CONST char *logname;
} History;

#define empty(hist) (!(hist)->alines || !(hist)->size)

static void     FDECL(alloc_history,(History *hist, int maxsize));
static int      FDECL(next_hist_opt,(char **argp, History **histp, long *nump));
static void     FDECL(save_to_hist,(History *hist, Aline *aline));
static void     FDECL(save_to_log,(History *hist, CONST char *str));
static void     FDECL(hold_input,(CONST char *str));
static void     FDECL(listlog,(World *world));
static void     FDECL(stoplog,(World *world));
static int      FDECL(do_watch,(char *args, CONST char *name, int *wlines,
                      int *wmatch, int flag));


static Aline blankline[1] = { BLANK_ALINE };
static int norecord = 0;         /* supress history (but not log) recording */
static int nolog = 0;            /* supress log (but not history) recording */
static struct History input[1];
static int wnmatch = 4, wnlines = 5, wdmatch = 2, wdlines = 5;

struct History globalhist[1], localhist[1];
int log_count = 0;

struct History *init_history(hist, maxsize)
    History *hist;
    int maxsize;
{
    if (!hist) hist = (History*)XMALLOC(sizeof(History));
    hist->logfile = NULL;
    hist->last = hist->index = -1;
    hist->first = hist->size = hist->total = 0;
    alloc_history(hist, maxsize);
    return hist;
}

static void alloc_history(hist, maxsize)
    History *hist;
    int maxsize;
{
    hist->maxsize = maxsize;
    if (maxsize) {
        hist->alines =
            (Aline**)dmalloc(maxsize * sizeof(Aline *), __FILE__, __LINE__);
        if (!hist->alines) {
            eprintf("not enough memory for %d lines of history.", maxsize);
            hist->maxsize = 1;
            hist->alines = (Aline**)XMALLOC(1 * sizeof(Aline *));
        }
    } else {
        hist->alines = NULL;
    }
}

void init_histories()
{
    init_history(input, INPUTSIZE);
    init_history(globalhist, GLOBALSIZE);
    init_history(localhist, LOCALSIZE);
    save_to_hist(input, blankline);
    input->index = input->last;
}

#ifdef DMALLOC
void free_histories()
{
    free_history(input);
    free_history(globalhist);
    free_history(localhist);
}
#endif

void free_history(hist)
    History *hist;
{
    if (hist->alines) {
        for ( ; hist->size; hist->size--) {
            free_aline(hist->alines[hist->first]);
            hist->first = nmod(hist->first + 1, hist->maxsize);
        }
        hist->first = 0;
        hist->last = -1;
        FREE(hist->alines);
        if (hist->logfile) {
            tfclose(hist->logfile);
            --log_count;
            update_status_field(NULL, STAT_LOGGING);
        }
    }
}

static void save_to_hist(hist, aline)
    History *hist;
    Aline *aline;
{
    if (aline->time < 0) aline->time = time(NULL);
    if (!hist->alines)
        alloc_history(hist, hist->maxsize ? hist->maxsize : histsize);
    if (hist->size == hist->maxsize) {
        free_aline(hist->alines[hist->first]);
        hist->first = nmod(hist->first + 1, hist->maxsize);
    } else {
        hist->size++;
    }
    hist->last = nmod(hist->last + 1, hist->maxsize);
    (hist->alines[hist->last] = aline)->links++;
    hist->total++;
}

static void save_to_log(hist, str)
    History *hist;
    CONST char *str;
{
    if (wraplog) {
        /* ugly, but some people want it */
        STATIC_BUFFER(buf);
        int i = 0, first = TRUE, len, remaining;
        for (remaining = strlen(str); remaining; str += len, remaining -= len) {
            if (!first && wrapflag)
                for (i = wrapspace; i; i--) tfputc(' ', hist->logfile);
            len = wraplen(str, remaining, !first);
            if (str[len]) {
                /* stupid copy, just to give right length string to tfputs().
                 * (We can't just write a '\0' into str: it could be const).
                 */
                Stringncpy(buf, str, len);
                tfputs(buf->s, hist->logfile);
            } else {
                tfputs(str, hist->logfile);
            }
            first = FALSE;
        }
    } else {
        tfputs(str, hist->logfile);
    }
    tfflush(hist->logfile);
}

void recordline(hist, aline)
    History *hist;
    Aline *aline;
{
    if (!(aline->attrs & F_NOHISTORY) && !norecord) save_to_hist(hist, aline);
    if (hist->logfile && !nolog) save_to_log(hist, aline->str);
}

static void hold_input(str)
    CONST char *str;
{
    free_aline(input->alines[input->last]);
    input->alines[input->last] = new_aline(str, sockecho ? 0 : F_GAG);
    input->alines[input->last]->links++;
    input->alines[input->last]->time = time(NULL);
}

void record_input(str)
    CONST char *str;
{
    char *prev_line;

    input->index = input->last;

    if (!*str) return;
    if (input->size > 1) {
        prev_line = input->alines[nmod(input->last-1, input->maxsize)]->str;
        if (strcmp(str, prev_line) == 0) return;
    }

    hold_input(str);
    save_to_hist(input, blankline);
    input->index = input->last;

    if (input->logfile && !nolog) save_to_log(input, str);
}

/* recall_input() parameter combinations:
 *
 *    dir    searchflag meaning
 *  -------- ---------- -------
 *  -1 or 1     0       single step backward or forward
 *  -1 or 1     1       search backward or forward
 *  -2 or 2   ignored   go to beginning or end
 */

Aline *recall_input(dir, searchflag)
    int dir;
    int searchflag;
{
    int i;
    Aline *pat = NULL;

    if (input->index == input->last) hold_input(keybuf->s);

    if (dir < -1 || dir > 1) {
        i = (dir < 0) ? input->first : input->last;
        if (input->index == i) return NULL;
        dir = (dir < 0) ? 1 : -1;
    } else {
        pat = searchflag ? input->alines[input->last] : NULL;
        i = input->index;
        if (i == ((dir < 0) ? input->first : input->last)) return NULL;
        i = nmod(i + dir, input->maxsize);
    }

    /* Keep looking while lines are gagged.
     * If pat, also keep looking while lines are too short or don't match.
     */
    while ((input->alines[i]->attrs & F_GAG) || (pat &&
        (input->alines[i]->len <= pat->len ||
        strncmp(input->alines[i]->str, pat->str, pat->len) != 0)))
    {
        if (i == ((dir < 0) ? input->first : input->last)) return NULL;
        i = nmod(i + dir, input->maxsize);
    }

    input->index = i;
    return input->alines[i];
}

struct Value *handle_recall_command(args)
    char *args;
{
    return newint(do_recall(args));
}

int do_recall(args)
    char *args;
{
    int n0, n1, istime, i, ii, mflag = matching, want, count, truth = !0;
    long n_or_t = 0;
    TIME_T t0, t1, now = time(NULL);
    int numbers, timestamps = FALSE;
    attr_t attrs = 0;
    short *partials = NULL;
    char opt;
    Pattern pat;
    World *world = xworld();
    History *hist = NULL;
    Aline *aline;
    static List stack[1] = {{ NULL, NULL }};
    STATIC_BUFFER(buffer);
    static Aline *startmsg = NULL, *endmsg = NULL;

    if (!startmsg) {
        (startmsg = new_aline("---- Recall start ----", 0))->links = 1;
        (endmsg = new_aline("---- Recall end ----", 0))->links = 1;
    }

    init_pattern_str(&pat, NULL);
    startopt(args, "ligw:a:f:tm:v");
    while ((opt = next_hist_opt(&args, &hist, NULL))) {
        switch (opt) {
        case 'a': case 'f':
            if ((i = parse_attrs(&args)) < 0) return 0;
            attrs |= i;
            break;
        case 't':
            timestamps = TRUE;
            break;
        case 'm':
            if ((mflag = enum2int(args, enum_match, "-m")) < 0) return 0;
            break;
        case 'v':
            truth = 0;
            break;
        default: return 0;
        }
    }
    if (!hist) hist = world ? world->history : globalhist;
    if ((numbers = (args && *args == '#'))) args++;
    while(isspace(*args)) args++;

    t0 = 0;
    t1 = now;
    n0 = 0;
    n1 = hist->total - 1;
    want = hist->size;

    if (!args || !*args) {
        n_or_t = -1;  /* flag syntax error */
        eprintf("missing arguments");
        return 0;
    } else if (*args == '-') {                                 /*  -y */
        ++args;
        if ((n_or_t = parsetime(&args, &istime)) < 0) {
            eprintf("syntax error in recall range");
            return 0;
        }
        if (istime) t1 = abstime(n_or_t);
        else n0 = n1 = hist->total - n_or_t;
    } else if (*args == '/') {                                 /*  /x */
        ++args;
        want = strtoint(&args);
    } else if (isdigit(*args)) {
        if ((n_or_t = parsetime(&args, &istime)) < 0) {
            eprintf("syntax error in recall range");
            return 0;
        } else if (*args != '-') {                             /* x   */
            if (istime) t0 = t1 - n_or_t;
            else n0 = hist->total - n_or_t;
        } else if (isdigit(*++args)) {                         /* x-y */
            if (istime) t0 = abstime(n_or_t);
            else n0 = n_or_t - 1;
            if ((n_or_t = parsetime(&args, &istime)) < 0) {
                eprintf("syntax error in recall range");
                return 0;
            }
            if (istime) t1 = abstime(n_or_t);
            else n1 = n_or_t - 1;
        } else {                                               /* x-  */
            if (istime) t0 = abstime(n_or_t);
            else n0 = n_or_t - 1;
        }
    }
    if (*args && !isspace(*args)) {
        eprintf("extra characters after recall range: %s", args);
        return 0;
    }
    while (isspace(*args)) ++args;
    if (*args && !init_pattern(&pat, args, mflag))
        return 0;

    if (empty(hist)) return 0;          /* (after parsing, before searching) */

    if (tfout == tfscreen) {
        norecord++;                     /* don't save this output in history */
        oputa(startmsg);
    }

    if (n0 < hist->total - hist->size) n0 = hist->total - hist->size;
    if (n1 >= hist->total) n1 = hist->total - 1;
    if (n0 <= n1 && t0 <= t1) {
        n0 = nmod(n0, hist->maxsize);
        n1 = nmod(n1, hist->maxsize);
        attrs = ~attrs | F_NORM;

        if (hist == input) hold_input(keybuf->s);
        for (i = n1; want > 0; i = nmod(i - 1, hist->maxsize)) {
            if (i == n0) want = 0;
            aline = hist->alines[i];
            if (aline->time > t1 || aline->time < t0) continue;
            if (gag && (aline->attrs & F_GAG & attrs)) continue;
            if (!patmatch(&pat, aline->str) == truth) continue;
            want--;
            Stringterm(buffer, 0);
            if (numbers)
                Sprintf(buffer, SP_APPEND, "%d: ",
                    hist->total - nmod(hist->last - i, hist->maxsize));
            if (timestamps) {
                Sprintf(buffer, SP_APPEND, "[%s] ",
                    tftime(time_format, aline->time));
            }

            /* share aline if possible: copy only if different */
            partials = NULL;
            if (aline->partials &&
                (buffer->len || (aline->attrs & ~attrs & F_ATTR)))
            {
                partials = XMALLOC(sizeof(short) * (buffer->len+aline->len));
                for (ii = 0; ii < buffer->len; ii++)
                    partials[ii] = 0;
                memcpy(partials + buffer->len, aline->partials,
                    sizeof(short) * aline->len);
            }
            if (buffer->len) {
                Stringfncat(buffer, aline->str, aline->len);
                aline = new_aline(buffer->s, aline->attrs & attrs);
                if (partials) aline->partials = partials;
            } else if (aline->attrs & ~attrs & F_ATTR) {
                aline = new_aline(aline->str, aline->attrs & attrs);
                if (partials) aline->partials = partials;
            }

            inlist((GENERIC*)aline, stack, NULL);
        }
    }

    for (count = 0; stack->head; count++)
        oputa((Aline *)unlist(stack->head, stack));

    free_pattern(&pat);

    if (tfout == tfscreen) {
        oputa(endmsg);
        norecord--;
    }
    return count;
}

static int do_watch(args, name, wlines, wmatch, flag)
    char *args;
    CONST char *name;
    int *wlines, *wmatch, flag;
{
    int out_of, match;

    if (!*args) {
        oprintf("%% %s %sabled.", name, flag ? "en" : "dis");
        return 1;
    } else if (cstrcmp(args, "off") == 0) {
        setvar(name, "0", FALSE);
        oprintf("%% %s disabled.", name);
        return 1;
    } else if (cstrcmp(args, "on") == 0) {
        /* do nothing */
    } else {
        if ((match = numarg(&args)) < 0) return 0;
        if ((out_of = numarg(&args)) < 0) return 0;
        *wmatch = match;
        *wlines = out_of;
    }
    setvar(name, "1", FALSE);
    oprintf("%% %s enabled, searching for %d out of %d lines",
        name, *wmatch, *wlines);
    return 1;
}

struct Value *handle_watchdog_command(args)
    char *args;
{
    return newint(do_watch(args, "watchdog", &wdlines, &wdmatch, watchdog));
}

struct Value *handle_watchname_command(args)
    char *args;
{
    return newint(do_watch(args, "watchname", &wnlines, &wnmatch, watchname));
}

int is_watchname(hist, aline)
    History *hist;
    Aline *aline;
{
    int nmatches = 1, i;
    CONST char *line, *end;
    STATIC_BUFFER(buf);

    if (!watchname || !gag || aline->attrs & F_GAG) return 0;
    if (isspace(*aline->str)) return 0;
    for (end = aline->str; *end && !isspace(*end); ++end);
    for (i = ((wnlines >= hist->size) ? hist->size - 1 : wnlines); i > 0; i--) {
        line = hist->alines[nmod(hist->last - i, hist->maxsize)]->str;
        if (strncmp(line, aline->str, end - aline->str) != 0) continue;
        if (++nmatches == wnmatch) break;
    }
    if (nmatches < wnmatch) return 0;
    Stringcpy(buf, "{");
    Stringfncat(buf, aline->str, end - aline->str);
    Stringcat(buf, "}*");
    oprintf("%% Watchname: gagging \"%S\"", buf);
    return add_macro(new_macro(buf->s, "", 0, NULL, "", gpri, 100, F_GAG, 0,
        MATCH_GLOB));
}

int is_watchdog(hist, aline)
    History *hist;
    Aline *aline;
{
    int nmatches = 0, i;
    CONST char *line;

    if (!watchdog || !gag || aline->attrs & F_GAG) return 0;
    for (i = ((wdlines >= hist->size) ? hist->size - 1 : wdlines); i > 0; i--) {
        line = hist->alines[nmod(hist->last - i, hist->maxsize)]->str;
        if (cstrcmp(line, aline->str) == 0 && (++nmatches == wdmatch)) return 1;
    }
    return 0;
}

String *history_sub(pattern)
    CONST char *pattern;
{
    int i;
    Aline **L = input->alines;
    char *replace, *loc = NULL;
    STATIC_BUFFER(buffer);

    if (empty(input) || !*pattern) return NULL;
    if ((replace = strchr(pattern, '^')) == NULL) return NULL;
    *replace = '\0';
    for (i = 1; i < input->size; i++) {
        loc = strstr(L[nmod(input->last - i, input->maxsize)]->str, pattern);
        if (loc) break;
    }
    *replace++ = '^';
    if (!loc) return NULL;
    i = nmod(input->last - i, input->maxsize);
    Stringterm(buffer, 0);
    Stringfncat(buffer, L[i]->str, loc - L[i]->str);
    Stringcat(buffer, replace);
    Stringcat(buffer, loc + ((replace - 1) - pattern));
    return buffer;
}

static void stoplog(world)
    World *world;
{
    if (world->history->logfile) tfclose(world->history->logfile);
    world->history->logfile = NULL;
}

static void listlog(world)
    World *world;
{
    if (world->history->logfile)
        oprintf("%% Logging world %s output to %s",
          world->name, world->history->logfile->name);
}

/* Parse "ligw:" history options.  If another option is found, it is returned,
 * so the caller can parse it.  If end of options is reached, 0 is returned.
 * '?' is returned for error.  *histp will contain a pointer to the history
 * selected by the "ligw:" options.  *histp will be unchanged if no relavant
 * options are given; the caller should assign a default before calling.
 */
static int next_hist_opt(argp, histp, nump)
    char **argp;
    History **histp;
    long *nump;
{
    World *world;
    char c;

    while ((c = nextopt(argp, nump))) {
        switch (c) {
        case 'l':
            *histp = localhist;
            break;
        case 'i':
            *histp = input;
            break;
        case 'g':
            *histp = globalhist;
            break;
        case 'w':
            if (!(world = (**argp) ? find_world(*argp) : xworld())) {
                eprintf("No world %s", *argp);
                return '?';
            } else *histp = world->history;
            break;
        default:
            return c;
        }
    }
    return c;
}

struct Value *handle_recordline_command(args)
    char *args;
{
    History *history = globalhist;
    char opt;
    long timestamp = -1;
    Aline *aline;

    startopt(args, "lgiw:t#");
    while ((opt = next_hist_opt(&args, &history, &timestamp))) {
        if (opt != 't') return newint(0);
    }

    nolog++;
    if (history == input) {
        record_input(args);
        if (timestamp >= 0)
            input->alines[nmod(input->last - 1, input->maxsize)]->time =
                (TIME_T)timestamp;
    } else {
        aline = new_aline(args, 0);
        if (timestamp >= 0)
            aline->time = (TIME_T)timestamp;
        recordline(history, aline);
    }
    nolog--;
    return newint(1);
}

struct Value *handle_log_command(args)
    char *args;
{
    History *history;
    History dummy;
    TFILE *logfile = NULL;

    if (restriction >= RESTRICT_FILE) {
        eprintf("restricted");
        return newint(0);
    }

    history = &dummy;
    startopt(args, "lgiw:");
    if (next_hist_opt(&args, &history, NULL))
        return newint(0);

    if (history == &dummy && !*args) {
        if (log_count) {
            if (input->logfile)
                oprintf("%% Logging input to %s", input->logfile->name);
            if (localhist->logfile)
                oprintf("%% Logging local output to %s",
                    localhist->logfile->name);
            if (globalhist->logfile)
                oprintf("%% Logging global output to %s",
                    globalhist->logfile->name);
            mapworld(listlog);
        } else {
            oputs("% Logging disabled.");
        }
        return newint(1);
    } else if (cstrcmp(args, "OFF") == 0) {
        if (history == &dummy) {
            if (log_count) {
                if (input->logfile) tfclose(input->logfile);
                input->logfile = NULL;
                if (localhist->logfile) tfclose(localhist->logfile);
                localhist->logfile = NULL;
                if (globalhist->logfile) tfclose(globalhist->logfile);
                globalhist->logfile = NULL;
                mapworld(stoplog);
                log_count = 0;
                update_status_field(NULL, STAT_LOGGING);
            }
        } else if (history->logfile) {
            tfclose(history->logfile);
            history->logfile = NULL;
            --log_count;
            update_status_field(NULL, STAT_LOGGING);
        }
        return newint(1);
    } else if (cstrcmp(args, "ON") == 0 || !*args) {
        if (!(args = tfname(NULL, "LOGFILE")))
            return newint(0);
        logfile = tfopen(args, "a");
    } else {
        logfile = tfopen(expand_filename(args), "a");
    }
    if (!logfile) {
        operror(args);
        return newint(0);
    }
    if (history == &dummy) history = globalhist;
    if (history->logfile) {
        tfclose(history->logfile);
        history->logfile = NULL;
        log_count--;
    }
    do_hook(H_LOG, "%% Logging to file %s", "%s", logfile->name);
    history->logfile = logfile;
    log_count++;
    update_status_field(NULL, STAT_LOGGING);
    return newint(1);
}

#define histname(hist) \
        (hist == globalhist ? "global" : (hist == localhist ? "local" : \
        (hist == input ? "input" : "world")))

struct Value *handle_histsize_command(args)
    char *args;
{
    History *hist;
    int first, last, size, maxsize = 0;
    Aline **alines;

    hist = globalhist;
    startopt(args, "lgiw:");
    if (next_hist_opt(&args, &hist, NULL))
        return newint(0);
    if (*args) {
        if ((maxsize = numarg(&args)) <= 0) return newint(0);
        if (maxsize > 100000) {
            eprintf("%d lines?  Don't be ridiculous.", maxsize);
            return newint(0);
        }
        alines =
            (Aline**)dmalloc(maxsize * sizeof(Aline *), __FILE__, __LINE__);
        if (!alines) {
            eprintf("not enough memory for %d lines.", maxsize);
            return newint(0);
        }
        first = nmod(hist->total, maxsize);
        last = nmod(hist->total - 1, maxsize);
        for (size = 0; hist->size; hist->size--) {
            if (size < maxsize) {
                first = nmod(first - 1, maxsize);
                alines[first] = hist->alines[hist->last];
                size++;
            } else {
                free_aline(hist->alines[hist->last]);
            }
            hist->last = nmod(hist->last - 1, hist->maxsize);
        }
        if (hist->alines) FREE(hist->alines);
        hist->alines = alines;
        hist->first = first;
        hist->last = last;
        hist->size = size;
        hist->maxsize = maxsize;
    }
    oprintf("%% %s history capacity %s %ld lines.",
        histname(hist), maxsize ? "changed to" : "is",
        hist->maxsize ? hist->maxsize : histsize);
    hist->index = hist->last;
    return newint(hist->maxsize);
}

void sync_input_hist()
{
    input->index = input->last;
}

#endif /* NO_HISTORY */
