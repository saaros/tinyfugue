/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: util.c,v 33000.8 1994/04/26 08:55:13 hawkeye Exp $ */


/*
 * Fugue utilities.
 *
 * Uppercase/lowercase table
 * String handling routines
 * Mail checker
 * Cleanup routine
 */

#include "config.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "output.h"	/* fix_screen() */
#include "macro.h"	/* do_hook()... */
#include "tty.h"	/* reset_tty() */

static int mail_mtime = 0;      /* mail modification time */

static int  FDECL(cmatch,(char **pat, int c1));
static int  FDECL(wmatch,(char *wlist, char **str));

int mail_flag = 0;
char lowercase_values[128], uppercase_values[128];    /* for lcase(), ucase() */

struct {
    regexp *re;
    char *str;
    short temp, ok;
} reginfo;

void init_util()
{
    int i;

    /* Some non-standard compilers don't handle tolower() or toupper()
     * on a character that wouldn't ordinarily be converted.  So we
     * create our own conversion table and use macros in util.h.
     */
    for (i = 0; i < 128; i++) {
        lowercase_values[i] = isupper(i) ? tolower(i) : i;
        uppercase_values[i] = islower(i) ? toupper(i) : i;
    }
}

#undef CTRL
/* convert to or from ctrl character */
#define CTRL(c)  ((ucase(c) + '@') & 0x7F)

/* Convert ascii string to printable string with "^X" forms. */
/* Returns pointer to static area; copy if needed. */
char *ascii_to_print(key)
    char *key;
{
    STATIC_BUFFER(buffer);

    for (Stringterm(buffer, 0); *key; key++) {
        if (*key == '^' || *key == '\\') {
            Stringadd(Stringadd(buffer, '\\'), *key);
        } else if (!isprint(*key) || iscntrl(*key)) {
            Stringadd(Stringadd(buffer, '^'), CTRL(*key));
        } else Stringadd(buffer, *key);
    }
    return buffer->s;
}

/* Convert a printable string containing '^X' and '\nnn' to real ascii. */
/* Returns pointer to static area; copy if needed. */
char *print_to_ascii(src)
    char *src;
{
    STATIC_BUFFER(dest);

    Stringterm(dest, 0);
    while (*src) {
        if (*src == '^') {
            Stringadd(dest, *++src ? CTRL(*src++) : '^');
        } else if (*src == '\\' && isdigit(*++src)) {
            Stringadd(dest, strtochr(&src));
        } else Stringadd(dest, *src++);
    }
    return dest->s;
}


#ifndef HAVE_STRTOL
char strtochr(sp)
    char **sp;
{
    int c;

    if (**sp != '0') {
        c = atoi(*sp);
        while (isdigit(*++(*sp)));
    } else if (lcase(*++(*sp)) == 'x') {
        for ((*sp)++, c = 0; isxdigit(**sp); (*sp)++)
            c = c * 0x10 + lcase(**sp) - (isdigit(**sp) ? '0' : ('a' - 10));
    } else {
        for (c = 0; isdigit(**sp); (*sp)++)
            c = c * 010 + **sp - '0';
    }
    return (char)(c % 128);
}

int strtoi(**sp)
    char **sp;
{
    int i;
    while (isspace(**sp)) ++*sp;
    i = atoi(*sp);
    while (isdigit(**sp)) ++*sp;
    return i;
}
#endif


/* String handlers
 * Some of these are already present in most C libraries, but go by
 * different names or are not always there.  Since they're small, TF
 * simply uses its own routines with non-standard but consistant naming.
 * These are heavily used functions, so speed is favored over simplicity.
 */

/* case-insensitive strchr() */
char *cstrchr(s, c)
    register CONST char *s;
    register int c;
{
    for (c = lcase(c); *s; s++) if (lcase(*s) == c) return (char *)s;
    return (c) ? NULL : (char *)s;
}

/* c may be escaped by preceeding it with e */
char *estrchr(s, c, e)
    register CONST char *s;
    register int c, e;
{
    while (*s) {
        if (*s == c) return (char *)s;
        if (*s == e) {
            if (*++s) s++;
        } else s++;
    }
    return NULL;
}

#ifndef HAVE_INDEX
/* On some broken Solaris 2.x systems, libtermcap calls index() and rindex().
 * This takes care of them.  (I really hate catering to broken systems.)
 */
char *index(s, c)
    char *s, c;
{
    return strchr(s, c);
}

char *rindex(s, c)
    char *s, c;
{
    return strrchr(s, c);
}
#endif

#ifndef HAVE_STRSTR
char *STRSTR(s1, s2) 
    register CONST char *s1, *s2;
{
    int len;

    if ((len = strlen(s2) - 1) < 0) return (char*)s1;
    while (s1 = strchr(s1, *s2)) {
        if (strncmp(s1 + 1, s2 + 1, len) == 0) return (char*)s1;
    }
    return NULL;
}
#endif

/* case-insensitive strcmp() */
int cstrcmp(s, t)
    register CONST char *s, *t;
{
    while (*s && lcase(*s) == lcase(*t)) s++, t++;
    return lcase(*s) - lcase(*t);
}

/* case-insensitive strncmp() */
int cstrncmp(s, t, n)
    register CONST char *s, *t;
    int n;
{
    while (n && *s && lcase(*s) == lcase(*t)) s++, t++, n--;
    return n ? lcase(*s) - lcase(*t) : 0;
}

/* numarg
 * Converts argument to a nonnegative integer.  Returns -1 for failure.
 * The *str pointer will be advanced to beginning of next word.
 */
int numarg(str)
    char **str;
{
    int result;
    if (!isdigit(**str)) {
        cmderror("invalid or missing numeric argument");
        *str = NULL;
        return -1;
    }
    result = strtoi(str);
    while (isspace(**str)) ++*str;
    return result;
}

/* stringarg
 * Returns first word in *argp.  *argp will be advanced to next word.  If
 * <spaces> is non-NULL, the number of spaces after arg will be stored there.
 * NB: original string will be written into, and results will point into
 * original string.  Use a duplicate if you need to keep the original.
 */
char *stringarg(argp, spaces)
    char **argp;
    int *spaces;
{
    char *start;

    if (spaces) *spaces = 0;
    while (isspace(**argp)) ++*argp;
    for (start = *argp; (**argp && !isspace(**argp)); ++*argp) ;
    if (**argp) {
        if (spaces) ++*spaces;
        for (*((*argp)++) = '\0'; isspace(**argp); ++*argp)
            if (spaces) ++*spaces;
    }

    return start;
}

int regexec_and_hold(re, str, temp)
    regexp *re;
    char *str;
    int temp;
{
    reghold(re, str, temp);
    return (int)(reginfo.ok = regexec(reginfo.re, reginfo.str));
}

/* reghold
 * Hold onto a regexp and string for future reference.  If temp is true,
 * we are responsible for [de]allocation; otherwise, caller will handle it.
 */
void reghold(re, str, temp)
    regexp *re;
    char *str;
    int temp;
{
    regrelease();
    reginfo.re = re;
    reginfo.str = temp ? STRDUP(str) : str;
    reginfo.temp = temp;
    reginfo.ok = 1;
}

/* regrelease
 * Deallocate previously held regexp and string.
 */
void regrelease()
{
    if (reginfo.temp) {
        if (reginfo.re)  free(reginfo.re);
        if (reginfo.str) FREE(reginfo.str);
    }
    reginfo.re = NULL;
    reginfo.str = NULL;
    reginfo.ok = 0;
}

/* returns length of substituted string, or -1 if no substitution */
int regsubstr(dest, n)
    String *dest;
    int n;
{
    CONST regexp *re = reginfo.re;
    if (!(reginfo.ok && n < NSUBEXP && re && re->startp[n])) return -1;
    Stringncat(dest, re->startp[n], re->endp[n] - re->startp[n]);
    return re->endp[n] - re->startp[n];
}

void regerror(msg)
    char *msg;
{
    tfprintf(tferr, "%S: regexp error: %s", error_prefix(), msg);
}

/* call with (pat, NULL, 0) to zero-out pat.
 * call with (pat, str, 0) to init pat with some outside string (ie, strdup).
 * call with (pat, str, mflag) to init pat with some outside string.
 * call with (pat, pat->str, mflag) in reinit pat with already duped string.
 */
int init_pattern(pat, str, mflag)
    Pattern *pat;
    char *str;
    int mflag;
{
    pat->re = NULL;
    pat->str = NULL;
    if (!str) return 1;
    if (mflag == 2) {
        if (!(pat->re = regcomp(str))) return 0;
    } else if (mflag == 1) {
        if (!smatch_check(str)) return 0;
    }
    if (pat->str != str) pat->str = STRDUP(str);
    return 1;
}

void free_pattern(pat)
    Pattern *pat;
{
    if (pat->str) FREE(pat->str);
    if (pat->re) free(pat->re);    /* was allocated by malloc() in regcomp() */
    pat->str = NULL;
    pat->re  = NULL;
}

int patmatch(pat, str, mflag, temp)
    Pattern *pat;
    char *str;
    int mflag, temp;
{
    if (!pat->str || !*pat->str) return 1;
    else if (mflag >= 2) return regexec_and_hold(pat->re, str, temp);
    else if (mflag == 1) return !smatch(pat->str, str);
    else return !strcmp(pat->str, str);
}

static int cmatch(pat, c1)
    char **pat;
    int c1;
{
    int result;

    c1 = lcase(c1);
    if ((result = (**pat == '^'))) ++*pat;
    while (1) {
        if (**pat == ']') {
            ++*pat;
            return !result;
        }
        if (**pat == '\\') ++*pat;
        if (*(*pat + 1) == '-') {
            char lo = **pat;
            *pat += 2;
            if (**pat == '\\') ++*pat;
            if (c1 >= lcase(lo) && c1 <= lcase(**pat)) { ++*pat; break; }
        } else if (lcase(**pat) == c1) { ++*pat; break; }
        ++*pat;
    }
    *pat = estrchr(*pat, ']', '\\') + 1;
    return result;
}

static int wmatch(wlist, str)
    char *wlist;       /* word list                      */
    char **str;        /* buffer to match from           */
{
    char *matchstr,    /* which word to find             */
         *strend,      /* end of current word from wlist */
         *matchbuf,    /* where to find from             */
         *bufend;      /* end of match buffer            */
    int  result = 1;   /* intermediate result            */

    if (!wlist || !*str) return 1;
    matchbuf = *str;
    matchstr = wlist;
    if ((bufend = strchr(matchbuf, ' ')) == NULL)
        *str += strlen(*str);
    else
        *(*str = bufend) = '\0';
    do {
        if ((strend = estrchr(matchstr, '|', '\\')) != NULL)
            *strend = '\0';
        result = smatch(matchstr, matchbuf);
        if (strend != NULL) *strend++ = '|';
    } while (result && (matchstr = strend) != NULL);
    if (bufend != NULL) *bufend = ' ';
    return result;
}

/* smatch_check() should be used on pat to check pattern syntax before
 * calling smatch().
 */
/* Based on code by Leo Plotkin. */

int smatch(pat, str)
    char *pat, *str;
{
    char ch;
    char *start = str;

    while (*pat) {
        switch (*pat) {
        case '\\':
            pat++;
            if (lcase(*pat++) != lcase(*str++)) return 1;
            break;
        case '?':
            if (!*str++) return 1;
            pat++;
            break;
        case '*':
            while (*pat == '*' || (*pat == '?' && *str++)) pat++;
            if (*pat == '?') return 1;
            if (*pat == '{') {
                if (str == start || *(str - 1) == ' ')
                    if (!smatch(pat, str)) return 0;
                while ((str = strchr(str, ' ')) != NULL)
                    if (!smatch(pat, ++str)) return 0;
                return 1;
            } else if (*pat == '[') {
                while (*str) if (!smatch(pat, str++)) return 0;
                return 1;
            }
            ch = (*pat == '\\' && *(pat + 1)) ? *(pat + 1) : *pat;
            while ((str = cstrchr(str, ch)) != NULL) {
                if (!smatch(pat, str++)) return 0;
            }
            return 1;
        case '[':
            ++pat;
            if (cmatch(&pat, *str++)) return 1;
            break;
        case '{':
            if (str != start && !isspace(*(str - 1))) return 1;
            {
                char *end;
                if (!(end = estrchr(pat, '}', '\\'))) {     /* can't happen if*/
                    tfputs("% smatch: unmatched '{'", tferr); /* smatch_check */
                    return 1;                                /* is used first */
                }
                *end = '\0';
                if (wmatch(pat + 1, &str)) {
                    *end = '}';
                    return 1;
                }
                *end = '}';
                pat = end + 1;
            }
            break;
        default:
            if(lcase(*pat++) != lcase(*str++)) return 1;
            break;
        }
    }
    return lcase(*pat) - lcase(*str);
}

/* verify syntax of smatch pattern */
int smatch_check(pat)
    CONST char *pat;
{
    int inword = FALSE;

    while (*pat) {
        switch (*pat) {
        case '\\':
            if (*++pat) pat++;
            break;
        case '[':
            if (!(pat = estrchr(pat, ']', '\\'))) {
                cmderror("glob error: unmatched '['");
                return 0;
            }
            pat++;
            break;
        case '{':
            if (inword) {
                cmderror("glob error: nested '{'");
                return 0;
            }
            inword = TRUE;
            pat++;
            break;
        case '}':
            inword = FALSE;
            pat++;
            break;
        case '?':
        case '*':
        default:
            pat++;
            break;
        }
    }
    if (inword) cmderror("glob error: unmatched '{'");
    return !inword;
}

/* remove leading and trailing spaces */
char *stripstr(s)
    char *s;
{
    char *end;

    while (isspace(*s)) s++;
    if (*s) {
        for (end = s + strlen(s) - 1; isspace(*end); end--);
        *++end = '\0';
    }
    return s;
}


/* General command option parser

   startopt should be called before nextopt.  args is the argument list
   to be parsed, opts is a string containing valid options.  Options which
   take string arguments should be followed by a ':'; options which take
   numeric arguments should be followed by a '#'.  String arguments may be
   omitted.  The special character '0' expects an integer option of the
   form "-nn".  The special charcter '@' expects a time option of the
   form "-hh:mm:ss", "-hh:mm", or "-ss".

   nextopt returns the next option character.  If option takes a string
   argument, a pointer to it is returned in *arg; an integer argument
   is returned in *num.  If end of options is reached, nextopt returns
   '\0', and *arg points to remainder of argument list.  End of options
   is marked by "\0", "=", "--", or a word not beggining with
   '-'.  If an invalid option is encountered, an error message is
   printed and '?' is returned.

   Option Syntax Rules:
      All options must be preceded by '-'.
      Options may be grouped after a single '-'.
      There must be no space between an option and its argument.
      String option-arguments may be quoted.  Quotes in the arg must be escaped.
      All options must precede operands.
      A '--' or '-' with no option may be used to mark the end of the options.
*/

static char *argp, *options;
static int inword;

void startopt(args, opts)
    char *args, *opts;
{
    argp = args;
    options = opts;
    inword = 0;
}

char nextopt(arg, num)
    char **arg;
    int *num;
{
    char *q, opt, quote;
    STATIC_BUFFER(buffer);

    if (!inword) {
        while (isspace(*argp)) argp++;
        if (strcmp(argp, "--") == 0 || strncmp(argp, "-- ", 3) == 0) {
            for (*arg = argp + 2; isspace(**arg); ++*arg);
            return '\0';
        } else if (*argp != '-' || !*++argp || isspace(*argp)) {
            for (*arg = argp; isspace(**arg); ++*arg);
            return '\0';
        }
    } else if (*argp == '=') {        /* '=' marks end, & is part of parms */
        *arg = argp;                  /*... for stuff like  /def -t"foo"=bar */
        return '\0';
    }

    opt = *argp;

    /* time option */
    if ((isdigit(opt) || opt == ':') && strchr(options, '@')) {
        *num = parsetime(&argp, NULL);
        return '@';

    /* numeric option */
    } else if (isdigit(opt) && strchr(options, '0')) {
        *num = strtoi(&argp);
        return '0';
    }

    /* other options */
    if (opt == '@' || opt == ':' || opt == '#' || !(q = strchr(options, opt))) {
        tfprintf(tferr, "%S: invalid option: %c", error_prefix(), opt);
        return '?';
    }

    /* option takes a string argument */
    if (*++q == ':') {
        Stringterm(buffer, 0);
        ++argp;
        if (*argp == '"' || *argp == '\'' || *argp == '`') {
            quote = *argp;
            for (argp++; *argp && *argp != quote; Stringadd(buffer, *argp++))
                if (*argp == '\\' && (argp[1] == quote || argp[1] == '\\'))
                    argp++;
            if (!*argp) {
                tfprintf(tferr, "%S: unmatched %c in %c option",
                    error_prefix(), quote, opt);
                return '?';
            } else argp++;
        } else while (*argp && !isspace(*argp)) Stringadd(buffer, *argp++);
        *arg = buffer->s;

    /* option takes a numeric argument */
    } else if (*q == '#') {
        argp++;
        if (!isdigit(*argp)) {
            tfprintf(tferr, "%S: %c option requires numeric argument",
                error_prefix(), opt);
            return '?';
        }
        *num = strtoi(&argp);

    /* option takes no argument */
    } else {
        argp++;
    }

    inword = (*argp && !isspace(*argp));
    return opt;
}

#ifndef DMALLOC
Aline *new_aline(str, attrs)
    char *str;
    int attrs;
#else
Aline *dnew_aline(str, attrs, file, line)
    char *str;
    char *file;
    int attrs, line;
#endif
{
    Aline *aline;

#ifndef DMALLOC
    aline = (Aline *)MALLOC(sizeof(Aline));
#else
    aline = (Aline *)dmalloc(sizeof(Aline), file, line);
#endif
    aline->len = strlen(str);
    aline->str = strcpy((char*)MALLOC(aline->len + 1), str);
    aline->attrs = attrs;
    aline->partials = NULL;
    aline->links = 0;
    aline->time = time(NULL);
    return aline;
}

void free_aline(aline)
    Aline *aline;
{
    if (--(aline->links) <= 0) {
        FREE(aline->str);
        if (aline->partials) FREE(aline->partials);
        FREE(aline);
    }
}

void ch_maildelay()
{
    extern TIME_T mail_update;
    mail_update = 0;
}

void ch_mailfile()
{
    if (MAIL) {
        mail_mtime = 0;
        ch_maildelay();
    }
}

void init_mail()
{
    Stringp path;
    char *name;

    if (MAIL) {  /* was imported from environment */
        ch_mailfile();
    } else if ((name = getvar("LOGNAME")) || (name = getvar("USER"))) {
        Sprintf(Stringinit(path), 0, "%s/%s", MAILDIR, name);
        setvar("MAIL", path->s, FALSE);
        Stringfree(path);
    } else {
        tfputs("% Warning:  Can't figure out name of mail file.", tferr);
    }
}

void check_mail()
{
    struct stat buf;

    if (!MAIL || !*MAIL || maildelay <= 0) return;

    if (stat(expand_filename(MAIL), &buf) < 0) {
        if (mail_flag) { mail_flag = 0; status_bar(STAT_MAIL); }
        mail_mtime = 0;
    } else {
        if (buf.st_size == 0 || buf.st_mtime <= buf.st_atime) {
            if (mail_flag) { mail_flag = 0; status_bar(STAT_MAIL); }
        } else {
            if (!mail_flag) { mail_flag = 1; status_bar(STAT_MAIL); }
            if (buf.st_mtime > mail_mtime)
                do_hook(H_MAIL, "%% You have new mail in %s", "%s", MAIL);
        }
        mail_mtime = buf.st_mtime;
    }
}

/* Converts a string of the form "hh:mm:ss", "hh:mm", or "ss" to seconds.
 * Return value in <colon> is true if a colon was used; so, if <colon>
 * is false, the string could be interpreted as a plain integer.  <strp>
 * is advanced to the first character past the time string.  Return
 * value is -1 for an invalid string, a nonnegative integer otherwise.
 */
int parsetime(strp, colon)
    char **strp;
    int *colon;     /* return true if ':' found (i.e., can't be an int) */
{
    static int t;

    if (!isdigit(**strp) && **strp != ':') {
        cmderror("invalid or missing integer or time value");
        return -1;
    }
    t = strtoi(strp);
    if (**strp == ':') {
        if (colon) *colon = TRUE;
        t *= 3600;
        ++*strp;
        if (isdigit(**strp)) {
            t += strtoi(strp) * 60;
            if (**strp == ':') {
                ++*strp;
                if (isdigit(**strp))
                    t += strtoi(strp);
            }
        }
    } else {
        if (colon) *colon = FALSE;
    }
    return t;
}

/* Converts an hms value (from parsetime()) to an absolute clock time
 * within the last 24h.
 * Ideally, we'd use mktime() to find midnight, but not all systems
 * have it.  So we use ctime() to get <now> in a string, convert the
 * HH:MM:SS fields to an integer, and subtract from <now> to get the
 * time_t for midnight.  This function isn't heavily used anyway.
 * BUG: doesn't handle switches to/from daylight savings time.
 */
TIME_T abstime(hms)
    int hms;
{
    TIME_T result, now;
    char *ptr;

    now = time(NULL);
    ptr = ctime(&now) + 11;                  /* find HH:MM:SS part of string */
    result = now - parsetime(&ptr,NULL);     /* convert, subtract -> midnight */
    if ((result += hms) > now) result -= 24 * 60 * 60;
    return result;
}

/* returns a pointer to a formatted time string */
char *tftime(fmt, t)
    char *fmt;
    TIME_T t;
{
    static smallstr buf;

    if (!*fmt) fmt = (time_format && *time_format) ? time_format : "%c";
    if (strcmp(fmt, "@") == 0) {
        sprintf(buf, "%ld", t);
        return buf;
    } else {
#ifdef HAVE_STRFTIME
        return strftime(buf, sizeof(buf) - 1, fmt, localtime(&t)) ? buf : NULL;
#else
        char *str = ctime(&t);
        str[strlen(str) - 1] = '\0';    /* remove ctime()'s '\n' */
        return str;
#endif
    }
}

/* Cleanup and error routines. */
void cleanup()
{
    if (visual) fix_screen();
    reset_tty();
}

void die(why)
    CONST char *why;
{
    cleanup();
    puts(why);
    exit(1);
}

