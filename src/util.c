/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: util.c,v 35004.23 1997/03/27 01:04:52 hawkeye Exp $ */


/*
 * Fugue utilities.
 *
 * String handling routines
 * Mail checker
 * Cleanup routine
 */

#include "config.h"
#ifdef LOCALE_H
# include LOCALE_H
#endif
#include <errno.h>
extern int errno;
#include <sys/types.h>
#include <sys/stat.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "output.h"	/* fix_screen() */
#include "tty.h"	/* reset_tty() */
#include "signals.h"	/* core() */
#include "variable.h"

typedef struct RegInfo {
    regexp *re;
    CONST char *str;
    short temp;
} RegInfo;

extern TIME_T mail_update;

static TIME_T mail_mtime;	/* mail file modification time */
static long mail_size;		/* mail file size */
static RegInfo top_reginfo = { NULL, NULL, FALSE };
static RegInfo *reginfo = &top_reginfo;

int mail_flag = 0;
char tf_ctype[0x100];

static char *FDECL(cmatch,(CONST char *pat, int ch));

#ifndef CASE_OK
int lcase(x) char x; { return isupper(x) ? tolower(x) : x; }
int ucase(x) char x; { return islower(x) ? toupper(x) : x; }
#endif

void init_util1()
{
    int i;

    for (i = 0; i < 0x100; i++) {
        tf_ctype[i] = 0;
    }

    tf_ctype['+']  |= IS_UNARY | IS_ADDITIVE;
    tf_ctype['-']  |= IS_UNARY | IS_ADDITIVE;
    tf_ctype['!']  |= IS_UNARY;
    tf_ctype['*']  |= IS_MULT;
    tf_ctype['/']  |= IS_MULT;

    tf_ctype['"']  |= IS_QUOTE;
    tf_ctype['`']  |= IS_QUOTE;
    tf_ctype['\''] |= IS_QUOTE;
    tf_ctype['/']  |= IS_STATMETA;
    tf_ctype['%']  |= IS_STATMETA;
    tf_ctype['$']  |= IS_STATMETA;
    tf_ctype[')']  |= IS_STATMETA;
    tf_ctype['\n'] |= IS_STATMETA;
    tf_ctype['\\'] |= IS_STATMETA | IS_STATEND;
    tf_ctype[';']  |= IS_STATEND;

    tf_ctype['b']  |= IS_KEYSTART;  /* break */
    tf_ctype['d']  |= IS_KEYSTART;  /* do, done */
    tf_ctype['e']  |= IS_KEYSTART;  /* else, elseif, endif */
    tf_ctype['i']  |= IS_KEYSTART;  /* if */
    tf_ctype['t']  |= IS_KEYSTART;  /* then */
    tf_ctype['w']  |= IS_KEYSTART;  /* while */

    tf_ctype['B']  |= IS_KEYSTART;  /* BREAK */
    tf_ctype['D']  |= IS_KEYSTART;  /* DO, DONE */
    tf_ctype['E']  |= IS_KEYSTART;  /* ELSE, ELSEIF, ENDIF */
    tf_ctype['I']  |= IS_KEYSTART;  /* IF */
    tf_ctype['T']  |= IS_KEYSTART;  /* THEN */
    tf_ctype['W']  |= IS_KEYSTART;  /* WHILE */
}

/* Convert ascii string to printable string with "^X" forms. */
/* Returns pointer to static area; copy if needed. */
char *ascii_to_print(str)
    CONST char *str;
{
    STATIC_BUFFER(buffer);
    char c;

    for (Stringterm(buffer, 0); *str; str++) {
        c = unmapchar(*str);
        if (c == '^' || c == '\\') {
            Stringadd(Stringadd(buffer, '\\'), c);
        } else if (isprint(c)) {
            Stringadd(buffer, c);
        } else if (iscntrl(c)) {
            Stringadd(Stringadd(buffer, '^'), CTRL(c));
        } else {
            Sprintf(buffer, 0, "\\0x%2x", c);
        }
    }
    return buffer->s;
}

/* Convert a printable string containing "^X" and "\nnn" to real ascii. */
/* "^@" and "\0" are mapped to '\200'. */
/* Returns pointer to static area; copy if needed. */
char *print_to_ascii(src)
    CONST char *src;
{
    STATIC_BUFFER(dest);

    Stringterm(dest, 0);
    while (*src) {
        if (*src == '^') {
            Stringadd(dest, *++src ? mapchar(CTRL(*src)) : '^');
            if (*src) src++;
        } else if (*src == '\\' && isdigit(*++src)) {
            char c;
            c = strtochr((char**)&src);
            Stringadd(dest, mapchar(c));
        } else Stringadd(dest, *src++);
    }
    return dest->s;
}

/* String handlers
 * Some of these are already present in most C libraries, but go by
 * different names or are not always there.  If tfconfig couldn't
 * find them, we use our own.
 * These are heavily used functions, so speed is favored over simplicity.
 */

#ifndef HAVE_strtol
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
    return (char)(c & 0xFF);
}

int strtoint(sp)
    char **sp;
{
    int i;
    while (isspace(**sp)) ++*sp;
    i = atoi(*sp);
    while (isdigit(**sp)) ++*sp;
    return i;
}

long strtolong(sp)
    char **sp;
{
    long i;
    while (isspace(**sp)) ++*sp;
    i = atol(*sp);
    while (isdigit(**sp)) ++*sp;
    return i;
}
#endif

int enum2int(str, vec, msg)
    CONST char *str, **vec, *msg;
{
    int i, val;
    STATIC_BUFFER(buf);

    for (i = 0; vec[i]; ++i) {
        if (cstrcmp(str, vec[i]) == 0) return i;
    }
    if (isdigit(*str)) {
        if ((val = strtoint(&str)) < i && !*str) return val;
    }
    Stringcpy(buf, vec[0]);
    for (i = 1; vec[i]; ++i) Sprintf(buf, SP_APPEND, ", %s", vec[i]);
    eprintf("valid values for %s are: %S", msg, buf);
    return -1;
}

#if 0 /* not used */
/* case-insensitive strchr() */
char *cstrchr(s, c)
    register CONST char *s;
    register int c;
{
    for (c = lcase(c); *s; s++) if (lcase(*s) == c) return (char *)s;
    return (c) ? NULL : (char *)s;
}
#endif

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

#ifdef sun
# ifndef HAVE_index
/* Workaround for some buggy Solaris 2.x systems, where libtermcap calls index()
 * and rindex(), but they're not defined in libc.
 */
#undef index
char *index(s, c)
    CONST char *s;
    char c;
{
    return strchr(s, c);
}

#undef rindex
char *rindex(s, c)
    CONST char *s;
    char c;
{
    return strrchr(s, c);
}
# endif /* HAVE_index */
#endif /* sun */

#ifndef HAVE_strstr
char *strstr(s1, s2) 
    register CONST char *s1, *s2;
{
    int len;

    if ((len = strlen(s2) - 1) < 0) return (char*)s1;
    while ((s1 = strchr(s1, *s2))) {
        if (strncmp(s1 + 1, s2 + 1, len) == 0) return (char*)s1;
    }
    return NULL;
}
#endif

#ifndef cstrcmp
/* case-insensitive strcmp() */
int cstrcmp(s, t)
    register CONST char *s, *t;
{
    register int diff;
    while ((*s || *t) && !(diff = lcase(*s) - lcase(*t))) s++, t++;
    return diff;
}
#endif

#if 0  /* not used */
/* case-insensitive strncmp() */
int cstrncmp(s, t, n)
    register CONST char *s, *t;
    int n;
{
    register int diff;
    while (n && *s && !(diff = lcase(*s) - lcase(*t))) s++, t++, n--;
    return n ? diff : 0;
}
#endif

/* numarg
 * Converts argument to a nonnegative integer.  Returns -1 for failure.
 * The *str pointer will be advanced to beginning of next word.
 */
int numarg(str)
    char **str;
{
    int result;
    if (isdigit(**str)) {
        result = strtoint(str);
    } else {
        eprintf("invalid or missing numeric argument");
        result = -1;
        while (**str && !isspace(**str)) ++*str;
    }
    while (isspace(**str)) ++*str;
    return result;
}

/* stringarg
 * Returns pointer to first space-delimited word in *str.  *str is advanced
 * to next word.  If end != NULL, *end will get pointer to end of first word;
 * otherwise, word will be nul terminated.
 */
char *stringarg(str, end)
    char **str;
    CONST char **end;
{
    char *start;
    while (isspace(**str)) ++*str;
    for (start = *str; (**str && !isspace(**str)); ++*str) ;
    if (end) *end = *str;
    else if (**str) *((*str)++) = '\0';
    if (**str)
        while (isspace(**str)) ++*str;
    return start;
}

int regexec_in_scope(re, str)
    regexp *re;
    CONST char *str;
{
    int result;

    if (reginfo->temp) {
        if (reginfo->re) free(reginfo->re);
        if (reginfo->str) FREE(reginfo->str);
    }

    reginfo->re = re;
    reginfo->str = STRDUP(str);
    reginfo->temp = TRUE;
    result = regexec(re, (char *)reginfo->str);
    if (!result) {
        FREE(reginfo->str);
        reginfo->str = NULL;
    }
    return result;
}

void *new_reg_scope(re, str)
    regexp *re;
    CONST char *str;
{
    RegInfo *old;

    old = reginfo;
    reginfo = (RegInfo *)XMALLOC(sizeof(RegInfo));

    if (re) {			/* use new re */
        reginfo->re = re;
        reginfo->str = str;
    } else {			/* inherit old re */
        reginfo->re = old->re;
        reginfo->str = old->str;
    }
    reginfo->temp = FALSE;

    return old;
}

void restore_reg_scope(old)
    void *old;
{
    if (reginfo->temp) {
        if (reginfo->re)  free(reginfo->re);
        if (reginfo->str) FREE(reginfo->str);
    }
    FREE(reginfo);
    reginfo = (RegInfo *)old;
}

/* returns length of substituted string, or -1 if no substitution */
/* n>=0:  nth substring */
/* n=-1:  left substring */
/* n=-2:  right substring */
int regsubstr(dest, n)
    String *dest;
    int n;
{
    CONST regexp *re = reginfo->re;
    if (!(reginfo->str && n < NSUBEXP && re && re->startp[n >= 0 ? n : 0]))
        return -1;
    if (n < -2 || !(re->endp[n >= 0 ? n : 0])) {
        internal_error(__FILE__, __LINE__);
        return -1;
    }
    if (n == -1) {
        Stringncat(dest, reginfo->str, re->startp[0] - reginfo->str);
        return re->startp[0] - reginfo->str;
    } else if (n == -2) {
        Stringcat(dest, re->endp[0]);
        return strlen(re->endp[0]);
    } else {
        Stringncat(dest, re->startp[n], re->endp[n] - re->startp[n]);
        return re->endp[n] - re->startp[n];
    }
}

void regerror(msg)
    char *msg;
{
    eprintf("regexp error: %s", msg);
}

/* call with (pat, NULL, 0) to zero-out pat.
 * call with (pat, str, 0) to init pat with some outside string (ie, strdup).
 * call with (pat, str, mflag) to init pat with some outside string.
 */
int init_pattern(pat, str, mflag)
    Pattern *pat;
    CONST char *str;
    int mflag;
{
    pat->re = NULL;
    pat->str = NULL;
    if (!str) return 1;
    if (mflag == MATCH_REGEXP) {
        if (!(pat->re = regcomp((char*)str))) return 0;
    } else if (mflag == MATCH_GLOB) {
        if (!smatch_check(str)) return 0;
    }
    pat->str = STRDUP(str);
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

int patmatch(pat, str, mflag)
    Pattern *pat;
    CONST char *str;
    int mflag;
{
    if (!pat->str || !*pat->str) return 1;
    if (mflag == MATCH_SIMPLE) return !strcmp(pat->str, str);
    if (mflag == MATCH_GLOB) return !smatch(pat->str, str);
    if (mflag == MATCH_REGEXP) return regexec(pat->re, (char *)str);
    return 0;
}

/* pat is a pointer to a string of the form "[...]..."
 * ch is compared against the character class described by pat.
 * If ch matches, cmatch() returns a pointer to the char after ']' in pat;
 * otherwise, cmatch() returns NULL.
 */
static char *cmatch(pat, ch)
    CONST char *pat;
    int ch;
{
    int not;

    ch = lcase(ch);
    if ((not = (*++pat == '^'))) ++pat;

    while (1) {
        if (*pat == ']') return (char*)(not ? pat + 1 : NULL);
        if (*pat == '\\') ++pat;
        if (pat[1] == '-') {
            char lo = *pat;
            pat += 2;
            if (*pat == '\\') ++pat;
            if (ch >= lcase(lo) && ch <= lcase(*pat)) break;
        } else if (lcase(*pat) == ch) break;
        ++pat;
    }
    return not ? 0 : (estrchr(++pat, ']', '\\') + 1);
}

/* smatch_check() should be used on pat to check pattern syntax before
 * calling smatch().
 */
/* Based on code by Leo Plotkin. */

int smatch(pat, str)
    CONST char *pat, *str;
{
    CONST char *start = str;
    static int inword = FALSE;

    while (*pat) {
        switch (*pat) {

        case '\\':
            pat++;
            if (lcase(*pat++) != lcase(*str++)) return 1;
            break;

        case '?':
            if (!*str || (inword && isspace(*str))) return 1;
            str++;
            pat++;
            break;

        case '*':
            while (*pat == '*' || *pat == '?') {
                if (*pat == '?') {
                    if (!*str || (inword && isspace(*str))) return 1;
                    str++;
                }
                pat++;
            }
            if (inword) {
                while (*str && !isspace(*str))
                    if (!smatch(pat, str++)) return 0;
                return smatch(pat, str);
            } else if (!*pat) {
                return 0;
            } else if (*pat == '{') {
             /* if (str == start || isspace(*(str - 1))) */
                if (str == start || isspace(str[-1]))
                    if (!smatch(pat, str)) return 0;
                while (*++str)
                  /*if (isspace(*(str - 1)) && !smatch(pat, str)) return 0; */
                    if (isspace(str[-1]) && !smatch(pat, str)) return 0;
                return 1;
            } else if (*pat == '[') {
                while (*str) if (!smatch(pat, str++)) return 0;
                return 1;
            } else {
                char c = (pat[0] == '\\' && pat[1]) ? pat[1] : pat[0];
                for (c = lcase(c); *str; str++)
                    if (lcase(*str) == c && !smatch(pat, str))
                        return 0;
                return 1;
            }

        case '[':
            if (inword && isspace(*str)) return 1;
            if (!(pat = cmatch(pat, *str++))) return 1;
            break;

        case '{':
            if (str != start && !isspace(*(str - 1))) return 1;
            {
                CONST char *end;
                int result = 1;

                /* This can't happen if smatch_check is used first. */
                if (!(end = estrchr(pat, '}', '\\'))) {
                    eprintf("smatch: unmatched '{'");
                    return 1;
                }

                inword = TRUE;
                for (pat++; pat <= end; pat++) {
                    if ((result = smatch(pat, str)) == 0) break;
                    if (!(pat = estrchr(pat, '|', '\\'))) break;
                }
                inword = FALSE;
                if (result) return result;
                pat = end + 1;
                while (*str && !isspace(*str)) str++;
            }
            break;

        case '}': case '|':
            if (inword) return (*str && !isspace(*str));
            /* else FALL THROUGH to default case */

        default:
            if (lcase(*pat++) != lcase(*str++)) return 1;
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
                eprintf("glob error: unmatched '['");
                return 0;
            }
            pat++;
            break;
        case '{':
            if (inword) {
                eprintf("glob error: nested '{'");
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
    if (inword) eprintf("glob error: unmatched '{'");
    return !inword;
}

/* remove leading and trailing spaces.  Modifies s and *s */
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

static char *argp;
static CONST char *options;
static int inword;

void startopt(args, opts)
    CONST char *args;
    CONST char *opts;
{
    argp = (char *)args;
    options = opts;
    inword = 0;
}

char nextopt(arg, num)
    char **arg;
    long *num;
{
    char *q, opt, quote;
    STATIC_BUFFER(buffer);

    if (!inword) {
        while (isspace(*argp)) argp++;
        if (*argp != '-') {
            *arg = argp;
            return '\0';
        } else {
            if (*++argp == '-') argp++;
            if (isspace(*argp) || !*argp) {
                for (*arg = argp; isspace(**arg); ++*arg);
                return '\0';
            }
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
        *num = strtoint(&argp);
        return '0';
    }

    /* other options */
    if (opt == '@' || opt == ':' || opt == '#' || !(q = strchr(options, opt))) {
        eprintf("invalid option: %c", opt);
        return '?';
    }

    q++;

    /* option takes a string argument */
    if (*q == ':') {
        Stringterm(buffer, 0);
        ++argp;
        if (is_quote(*argp)) {
            quote = *argp;
            for (argp++; *argp && *argp != quote; Stringadd(buffer, *argp++))
                if (*argp == '\\' && (argp[1] == quote || argp[1] == '\\'))
                    argp++;
            if (!*argp) {
                eprintf("unmatched %c in %c option", quote, opt);
                return '?';
            } else argp++;
        } else while (*argp && !isspace(*argp)) Stringadd(buffer, *argp++);
        *arg = buffer->s;

    /* option takes a numeric argument */
    } else if (*q == '#') {
        argp++;
        if (!isdigit(*argp)) {
            eprintf("%c option requires numeric argument", opt);
            return '?';
        }
        *num = strtoint(&argp);
        *arg = NULL;

    /* option takes no argument */
    } else {
        argp++;
        *arg = NULL;
    }

    inword = (*argp && !isspace(*argp));
    return opt;
}

void ch_locale()
{
#ifdef HAVE_setlocale
    CONST char *lang;

    lang = getvar("LANG");
    if (!lang) lang = "";
    if (!setlocale(LC_ALL, lang))
        eprintf("Invalid locale \"%s\".", lang);
#endif /* HAVE_setlocale */
}

void ch_maildelay()
{
    mail_update = 0;
}

void ch_mailfile()
{
    if (MAIL) {
        mail_mtime = -2;
        mail_size = -2;
        ch_maildelay();
    }
}

void init_util2()
{
    Stringp path;
    CONST char *name;

    ch_locale();

    if (MAIL) {  /* was imported from environment */
        ch_mailfile();
    } else if ((name = getvar("LOGNAME")) || (name = getvar("USER"))) {
        Sprintf(Stringinit(path), 0, "%s/%s", MAILDIR, name);
        setvar("MAIL", path->s, FALSE);
        Stringfree(path);
    } else {
        eputs("% Warning:  Can't figure out name of mail file.");
    }
}


/* check_mail()
 * Enables the "(Mail)" indicator iff there is unread mail.
 * Calls the MAIL hook iff there is new mail.
 *
 * Logic:
 * If the file exists and is not empty, we follow this chart:
 *
 *                 || first   | new     | mtime same   | mtime changed
 * Timestamps      || test    | file    | as last test | since last test
 * ================++=========+=========+==============+=================
 * mtime <  atime  || 0,mail  | 0       | 0            | 0      
 * mtime == atime  || 0,mail  | 1,hook% | same         | 0
 * mtime >  atime  || 0,mail  | 1,hook  | 1            | 1,hook
 *
 * "0" or "1" means turn the "(Mail)" indicator off or on; "same" means
 * leave it in the same state it was in before the check.
 * "mail" means print a message.  "hook" means call the MAIL hook.
 *
 * [%]Problem:
 * If the file is created between checks, and mtime==atime, there is
 * no way to tell if the mail has been read.  If shell() or suspend()
 * is used to read mail, we can avoid this situation by checking mail
 * before and after shell() and suspend().  There is no way to avoid
 * it if mail is read in an unrelated process (e.g, another window).
 *
 * Note that mail readers can write to the mail file, causing a change 
 * in size, a change in mtime, and mtime==atime.
 */
void check_mail()
{
    struct stat buf;
    static int depth = 0;

    if (depth) return;                         /* don't allow recursion */
    depth++;

    if (!MAIL || !*MAIL || maildelay <= 0 ||
        stat(expand_filename(MAIL), &buf) < 0)
    {
        /* Checking disabled, or there is no mail file. */
        if (mail_flag) { mail_flag = 0; status_bar(STAT_MAIL); }
        buf.st_mtime = -1;
        buf.st_size = -1;

    } else if (buf.st_size == 0) {
        /* There is no mail (the file exists, but is empty). */
        if (mail_flag) { mail_flag = 0; status_bar(STAT_MAIL); }
    } else if (mail_size == -2) {
        /* There is mail, but this is the first time we've checked.
         * There is no way to tell if it has been read yet; assume it has. */
        if (mail_flag) { mail_flag = 0; status_bar(STAT_MAIL); }
        oprintf("%% You have mail in %s", MAIL);  /* not "new", so not a hook */
    } else if (buf.st_mtime > buf.st_atime) {
        /* There is unread mail. */
        if (!mail_flag) { mail_flag = 1; status_bar(STAT_MAIL); }
        if (buf.st_mtime > mail_mtime)
            do_hook(H_MAIL, "%% You have new mail in %s", "%s", MAIL);
    } else if (mail_size < 0 && buf.st_mtime == buf.st_atime) {
        /* File did not exist last time; assume new mail is unread. */
        if (!mail_flag) { mail_flag = 1; status_bar(STAT_MAIL); }
        do_hook(H_MAIL, "%% You have new mail in %s", "%s", MAIL);
    } else if (buf.st_mtime > mail_mtime || buf.st_mtime < buf.st_atime) {
        /* Mail has been read. */
        if (mail_flag) { mail_flag = 0; status_bar(STAT_MAIL); }
    } /* else {
        There has been no change since the last check.  Do nothing.
    } */

    mail_mtime = buf.st_mtime;
    mail_size = buf.st_size;
    depth--;
}


/* Converts a string of the form "hh:mm:ss", "hh:mm", or "ss" to seconds.
 * Return value in <istime> is true string must be a time; otherwise,
 * the string could be interpreted as a plain integer.  <strp> is
 * advanced to the first character past the time string.  Return
 * value is -1 for an invalid string, a nonnegative integer otherwise.
 */
long parsetime(strp, istime)
    char **strp;
    int *istime;     /* return true if ':' found (i.e., can't be an int) */
{
    static long t;

    if (!isdigit(**strp) && **strp != ':') {
        eprintf("invalid or missing integer or time value");
        return -1;
    }
    t = strtolong(strp);
    if (**strp == ':') {
        if (istime) *istime = TRUE;
        t *= 3600;
        ++*strp;
        if (isdigit(**strp)) {
            t += strtolong(strp) * 60;
            if (**strp == ':') {
                ++*strp;
                if (isdigit(**strp))
                    t += strtolong(strp);
            }
        }
    } else {
        if (istime) *istime = FALSE;
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
    long hms;
{
    TIME_T result, now;
    char *ptr;

    now = time(NULL);
    ptr = ctime(&now) + 11;                  /* find HH:MM:SS part of string */
    result = now - parsetime(&ptr, NULL);    /* convert, subtract -> midnight */
    if ((result += hms) > now) result -= 24 * 60 * 60;
    return result;
}

/* returns a pointer to a formatted time string */
char *tftime(fmt, t)
    CONST char *fmt;
    TIME_T t;
{
    static smallstr buf;

    if (strcmp(fmt, "@") == 0) {
        sprintf(buf, "%ld", t);
        return buf;
    } else {
#ifdef HAVE_strftime
        if (!*fmt) fmt = "%c";
        return strftime(buf, sizeof(buf) - 1, fmt, localtime(&t)) ? buf : NULL;
#else
        char *str = ctime(&t);
        str[strlen(str) - 1] = '\0';    /* remove ctime()'s '\n' */
        return str;
#endif
    }
}

void internal_error(file, line)
    CONST char *file;
    int line;
{
    eprintf("Internal error at %s:%d.  %s", file, line,
        "Please report this to the author, and describe what you did.");
}

void die(why, err)
    CONST char *why;
    int err;
{
    fix_screen();
    reset_tty();
    if (err) perror(why);
    else {
        fputs(why, stderr);
        fputc('\n', stderr);
    }
    exit(1);
}

