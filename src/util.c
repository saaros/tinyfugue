/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: util.c,v 35004.60 1999/01/31 00:27:56 hawkeye Exp $ */


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

static RegInfo top_reginfo = { NULL, NULL, FALSE };
static RegInfo *reginfo = &top_reginfo;

typedef struct mail_info_s {	/* mail file information */
    char *name;			/* file name */
    int flag;			/* new mail? */
    int error;			/* error */
    TIME_T mtime;		/* file modification time */
    long size;			/* file size */
    struct mail_info_s *next;
} mail_info_t;

static mail_info_t *maillist = NULL;

TIME_T mail_update = 0;		/* next mail update (0==immediately) */
int mail_count = 0;
char tf_ctype[0x100];

static char *FDECL(cmatch,(CONST char *pat, int ch));
static void  NDECL(free_maillist);

#ifndef CASE_OK
int lcase(x) char x; { return is_upper(x) ? tolower(x) : x; }
int ucase(x) char x; { return is_lower(x) ? toupper(x) : x; }
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
    tf_ctype['|']  |= IS_STATEND;

    tf_ctype['b']  |= IS_KEYSTART;  /* break */
    tf_ctype['d']  |= IS_KEYSTART;  /* do, done */
    tf_ctype['e']  |= IS_KEYSTART;  /* else, elseif, endif, exit */
    tf_ctype['i']  |= IS_KEYSTART;  /* if */
    tf_ctype['r']  |= IS_KEYSTART;  /* return */
    tf_ctype['t']  |= IS_KEYSTART;  /* then */
    tf_ctype['w']  |= IS_KEYSTART;  /* while */

    tf_ctype['B']  |= IS_KEYSTART;  /* BREAK */
    tf_ctype['D']  |= IS_KEYSTART;  /* DO, DONE */
    tf_ctype['E']  |= IS_KEYSTART;  /* ELSE, ELSEIF, ENDIF, EXIT */
    tf_ctype['I']  |= IS_KEYSTART;  /* IF */
    tf_ctype['R']  |= IS_KEYSTART;  /* RETURN */
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
        } else if (is_print(c)) {
            Stringadd(buffer, c);
        } else if (is_cntrl(c)) {
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
        } else if (*src == '\\' && is_digit(*++src)) {
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
        while (is_digit(*++(*sp)));
    } else if (lcase(*++(*sp)) == 'x') {
        for ((*sp)++, c = 0; is_xdigit(**sp); (*sp)++)
            c = c * 0x10 + lcase(**sp) - (is_digit(**sp) ? '0' : ('a' - 10));
    } else {
        for (c = 0; is_digit(**sp); (*sp)++)
            c = c * 010 + **sp - '0';
    }
    return (char)(c & 0xFF);
}

int strtoint(sp)
    char **sp;
{
    int i;
    while (is_space(**sp)) ++*sp;
    i = atoi(*sp);
    while (is_digit(**sp)) ++*sp;
    return i;
}

long strtolong(sp)
    char **sp;
{
    long i;
    while (is_space(**sp)) ++*sp;
    i = atol(*sp);
    while (is_digit(**sp)) ++*sp;
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
    if (is_digit(*str)) {
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
    if (is_digit(**str)) {
        result = strtoint(str);
    } else {
        eprintf("invalid or missing numeric argument");
        result = -1;
        while (**str && !is_space(**str)) ++*str;
    }
    while (is_space(**str)) ++*str;
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
    while (is_space(**str)) ++*str;
    for (start = *str; (**str && !is_space(**str)); ++*str) ;
    if (end) *end = *str;
    else if (**str) *((*str)++) = '\0';
    if (**str)
        while (is_space(**str)) ++*str;
    return start;
}

int stringliteral(dest, str)
    String *dest;
    char **str;
{
    char quote;

    Stringterm(dest, 0);
    quote = **str;
    for (++*str; **str && **str != quote; ++*str) {
        if (**str == '\\') {
            if ((*str)[1] == quote || (*str)[1] == '\\')
                ++*str;
            else if ((*str)[1] && pedantic)
                eprintf("warning: the only legal escapes within this quoted string are \\\\ and \\%c.  \\\\%c is the correct way to write a literal \\%c inside a quoted string.", quote, (*str)[1], (*str)[1]);

        }
        Stringadd(dest, is_space(**str) ? ' ' : **str);
    }
    if (!**str) {
        Sprintf(dest, 0, "unmatched %c", quote);
        return 0;
    }
    ++*str;
    return 1;
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
        eprintf("invalid subexp %d", n);
        return -1;
    }
    if (n == -1) {
        Stringfncat(dest, reginfo->str, re->startp[0] - reginfo->str);
        return re->startp[0] - reginfo->str;
    } else if (n == -2) {
        Stringcat(dest, re->endp[0]);
        return strlen(re->endp[0]);
    } else {
        Stringfncat(dest, re->startp[n], re->endp[n] - re->startp[n]);
        return re->endp[n] - re->startp[n];
    }
}

void regerror(msg)
    char *msg;
{
    eprintf("regexp error: %s", msg);
}

/* call with (pat, NULL, -1) to zero-out pat.
 * call with (pat, str, -1) to init pat with some outside string (ie, strdup).
 * call with (pat, str, mflag) to init pat with some outside string.
 */
int init_pattern(pat, str, mflag)
    Pattern *pat;
    CONST char *str;
    int mflag;
{
    return init_pattern_str(pat, str) && init_pattern_mflag(pat, mflag);
}

int init_pattern_str(pat, str)
    Pattern *pat;
    CONST char *str;
{
    pat->re = NULL;
    pat->mflag = -1;
    pat->str = (!str) ? NULL : STRDUP(str);
    return 1;
}

int init_pattern_mflag(pat, mflag)
    Pattern *pat;
    int mflag;
{
    if (!pat->str || pat->mflag >= 0) return 1;
    pat->mflag = mflag;
    if (mflag == MATCH_GLOB) {
        if (smatch_check(pat->str)) return 1;
    } else if (mflag == MATCH_REGEXP) {
        char *s = pat->str;
        while (*s == '(' || *s == '^') s++;
        if (strncmp(s, ".*", 2) == 0)
            eprintf("Warning: leading \".*\" in a regexp is very inefficient, and never necessary.");
        if ((pat->re = regcomp((char*)pat->str))) return 1;
    } else if (mflag == MATCH_SIMPLE) {
        return 1;
    }
    FREE(pat->str);
    pat->str = NULL;
    return 0;
}

void free_pattern(pat)
    Pattern *pat;
{
    if (pat->str) FREE(pat->str);
    if (pat->re) free(pat->re);    /* was allocated by malloc() in regcomp() */
    pat->str = NULL;
    pat->re  = NULL;
}

int patmatch(pat, str)
    CONST Pattern *pat;
    CONST char *str;
{
    if (!pat->str) return 1;
    /* Even a blank regexp must be exec'd, so Pn will work. */
    if (pat->mflag == MATCH_REGEXP) return regexec(pat->re, (char *)str);
    if (!*pat->str) return 1;
    if (pat->mflag == MATCH_GLOB)   return !smatch(pat->str, str);
    if (pat->mflag == MATCH_SIMPLE) return !strcmp(pat->str, str);
    eprintf("internal error: pat->mflag == %d", pat->mflag);
    return 0;
}

/* class is a pointer to a string of the form "[...]..."
 * ch is compared against the character class described by class.
 * If ch matches, cmatch() returns a pointer to the char after ']' in class;
 * otherwise, cmatch() returns NULL.
 */
static char *cmatch(class, ch)
    CONST char *class;
    int ch;
{
    int not;

    ch = lcase(ch);
    if ((not = (*++class == '^'))) ++class;

    while (1) {
        if (*class == ']') return (char*)(not ? class + 1 : NULL);
        if (*class == '\\') ++class;
        if (class[1] == '-' && class[2] != ']') {
            char lo = *class;
            class += 2;
            if (*class == '\\') ++class;
            if (ch >= lcase(lo) && ch <= lcase(*class)) break;
        } else if (lcase(*class) == ch) break;
        ++class;
    }
    return not ? NULL : (estrchr(++class, ']', '\\') + 1);
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
            if (!*str || (inword && is_space(*str))) return 1;
            str++;
            pat++;
            break;

        case '*':
            while (*pat == '*' || *pat == '?') {
                if (*pat == '?') {
                    if (!*str || (inword && is_space(*str))) return 1;
                    str++;
                }
                pat++;
            }
            if (inword) {
                while (*str && !is_space(*str))
                    if (!smatch(pat, str++)) return 0;
                return smatch(pat, str);
            } else if (!*pat) {
                return 0;
            } else if (*pat == '{') {
             /* if (str == start || is_space(*(str - 1))) */
                if (str == start || is_space(str[-1]))
                    if (!smatch(pat, str)) return 0;
                while (*++str)
                  /*if (is_space(*(str - 1)) && !smatch(pat, str)) return 0; */
                    if (is_space(str[-1]) && !smatch(pat, str)) return 0;
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
            if (inword && is_space(*str)) return 1;
            if (!(pat = cmatch(pat, *str++))) return 1;
            break;

        case '{':
            if (str != start && !is_space(*(str - 1))) return 1;
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
                while (*str && !is_space(*str)) str++;
            }
            break;

        case '}': case '|':
            if (inword) return (*str && !is_space(*str));
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

    while (is_space(*s)) s++;
    if (*s) {
        for (end = s + strlen(s) - 1; is_space(*end); end--);
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
    char *q, opt;
    STATIC_BUFFER(buffer);

    if (!inword) {
        while (is_space(*argp)) argp++;
        if (*argp != '-') {
            *arg = argp;
            return '\0';
        } else {
            if (*++argp == '-') argp++;
            if (is_space(*argp) || !*argp) {
                for (*arg = argp; is_space(**arg); ++*arg);
                return '\0';
            }
        }
    } else if (*argp == '=') {        /* '=' marks end, & is part of parms */
        *arg = argp;                  /*... for stuff like  /def -t"foo"=bar */
        return '\0';
    }

    opt = *argp;

    /* time option */
    if ((is_digit(opt) || opt == ':') && strchr(options, '@')) {
        *num = parsetime(&argp, NULL);
        return '@';
    }

    /* numeric option */
    if (is_digit(opt) && strchr(options, '0')) {
        *num = strtoint(&argp);
        return '0';
    }

    /* invalid options */
    if (opt == '@' || opt == ':' || opt == '#' || !(q = strchr(options, opt))) {
        int dash=1;
        CONST char *p;
        STATIC_BUFFER(helpbuf);
        Stringterm(helpbuf, 0);
        if (opt != '?') eprintf("invalid option: %c", opt);
        for (p = options; *p; p++) {
            switch (*p) {
            case '@': Stringcat(helpbuf, " -<time>"); dash=1; break;
            case '0': Stringcat(helpbuf, " -<number>"); dash=1; break;
            default:
                if (dash || p[1]==':' || p[1]=='#') Stringcat(helpbuf, " -");
                Stringadd(helpbuf, *p);
                dash=0;
                if (p[1] == ':') { Stringcat(helpbuf,"<string>"); p++; dash=1; }
                if (p[1] == '#') { Stringcat(helpbuf,"<number>"); p++; dash=1; }
            }
        }
        eprintf("options:%S", helpbuf);
        return '?';
    }

    q++;

    /* option takes a string argument */
    if (*q == ':') {
        Stringterm(buffer, 0);
        ++argp;
        if (is_quote(*argp)) {
            if (!stringliteral(buffer, &argp)) {
                eprintf("%S in %c option", buffer, opt);
                return '?';
            }
        } else {
            while (*argp && !is_space(*argp)) Stringadd(buffer, *argp++);
        }
        *arg = buffer->s;

    /* option takes a numeric argument */
    } else if (*q == '#') {
        argp++;
        if (!is_digit(*argp)) {
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

    inword = (*argp && !is_space(*argp));
    return opt;
}

#ifdef HAVE_tzset
int ch_timezone()
{
    tzset();
    return 1;
}
#endif

int ch_locale()
{
#ifdef HAVE_setlocale
    CONST char *lang;

#define tf_setlocale(cat, name, value) \
    do { \
        lang = setlocale(cat, value); \
        if (lang) { \
            eprintf("%s category set to \"%s\" locale.", name, lang); \
        } else { \
            eprintf("Invalid locale for %s.", name); \
        } \
    } while (0)

    tf_setlocale(LC_CTYPE, "LC_CTYPE", "");
    tf_setlocale(LC_TIME,  "LC_TIME",  "");

    return 1;
#else
    eprintf("Locale support is unavailable.");
    return 1;
#endif /* HAVE_setlocale */
}

int ch_maildelay()
{
    mail_update = 0;
    return 1;
}

int ch_mailfile()
{
    mail_info_t *info, **oldp, *newlist = NULL;
    char *path, *name;
    CONST char *end;

    path = (TFMAILPATH && *TFMAILPATH) ? TFMAILPATH : MAIL;
    while (*(name = stringarg(&path, &end))) {
        for (oldp = &maillist; *oldp; oldp = &(*oldp)->next) {
            if (strncmp(name, (*oldp)->name, end-name) == 0 &&
                !(*oldp)->name[end-name])
                    break;
        }
        if (*oldp) {
            info = *oldp;
            *oldp = (*oldp)->next;
        } else {
            info = XMALLOC(sizeof(mail_info_t));
            info->name = strncpy(XMALLOC(end-name+1), name, end-name);
	    info->name[end-name] = '\0';
            info->mtime = -2;
            info->size = -2;
            info->flag = 0;
            info->error = 0;
        }
        info->next = newlist;
        newlist = info;
    }
    free_maillist();
    maillist = newlist;
    ch_maildelay();
    return 1;
}

static void free_maillist()
{
    mail_info_t *info;
    while (maillist) {
        info = maillist;
        maillist = maillist->next;
        FREE(info->name);
        FREE(info);
    }
}

void init_util2()
{
    Stringp path;
    CONST char *name;

    ch_locale();

    if (MAIL || TFMAILPATH) {  /* was imported from environment */
        ch_mailfile();
#ifdef MAILDIR
    } else if ((name = getvar("LOGNAME")) || (name = getvar("USER"))) {
        Sprintf(Stringinit(path), 0, "%s/%s", MAILDIR, name);
        set_var_by_id(VAR_MAIL, 0, path->s);
        Stringfree(path);
#endif
    } else {
        eputs("% Warning:  Can't figure out name of mail file.");
    }
}

#ifdef DMALLOC
void free_util()
{
    free_maillist();
    if (reginfo->str) FREE(reginfo->str);
    if (reginfo->re) free(reginfo->re);      /* malloc()ed in regcomp() */
}
#endif


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
    int old_mail_count = mail_count;
    mail_info_t *info;

    if (depth) return;                         /* don't allow recursion */
    depth++;

    if (!maillist || maildelay <= 0) {
        if (mail_count) { mail_count = 0; update_status_field(NULL,STAT_MAIL); }
        depth--;
        return;
    }

    mail_count = 0;
    for (info = maillist; info; info = info->next) {
        if (stat(expand_filename(info->name), &buf) == 0) {
            errno = (buf.st_mode & S_IFDIR) ? EISDIR : 0;
        }
        if (errno) {
            /* Error, or file does not exist. */
            if (errno == ENOENT) {
                info->error = 0;
            } else if (info->error != errno) {
                eprintf("%s: %s", info->name, strerror(errno));
                info->error = errno;
            }
            info->mtime = -1;
            info->size = -1;
            info->flag = 0;
            continue;
        } else if (buf.st_size == 0) {
            /* There is no mail (the file exists, but is empty). */
            info->flag = 0;
        } else if (info->size == -2) {
            /* There is mail, but this is the first time we've checked.
             * There is no way to tell if it has been read; assume it has. */
            info->flag = 0;
            oprintf("%% You have mail in %s", info->name);  /* not "new" */
        } else if (buf.st_mtime > buf.st_atime) {
            /* There is unread mail. */
            info->flag = 1; mail_count++;
            if (buf.st_mtime > info->mtime)
                do_hook(H_MAIL, "%% You have new mail in %s", "%s", info->name);
        } else if (info->size < 0 && buf.st_mtime == buf.st_atime) {
            /* File did not exist last time; assume new mail is unread. */
            info->flag = 1; mail_count++;
            do_hook(H_MAIL, "%% You have new mail in %s", "%s", info->name);
        } else if (buf.st_mtime > info->mtime || buf.st_mtime < buf.st_atime) {
            /* Mail has been read. */
            info->flag = 0;
        } else if (info->flag >= 0) {
            /* There has been no change since the last check. */
            mail_count += info->flag;
        }

        info->error = 0;
        info->mtime = buf.st_mtime;
        info->size = buf.st_size;
    }

    if (mail_count != old_mail_count)
        update_status_field(NULL, STAT_MAIL);

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

    if (!is_digit(**strp) && **strp != ':') {
        eprintf("invalid or missing integer or time value");
        return -1;
    }
    t = strtolong(strp);
    if (**strp == ':') {
        if (istime) *istime = TRUE;
        t *= 3600;
        ++*strp;
        if (is_digit(**strp)) {
            t += strtolong(strp) * 60;
            if (**strp == ':') {
                ++*strp;
                if (is_digit(**strp))
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

/* appends a formatted time string to dest, returns length of new part */
int tftime(dest, fmt, sec, usec)
    String *dest;
    CONST char *fmt;
    long sec, usec;
{
    int oldlen = dest->len;

    if (!fmt || strcmp(fmt, "@") == 0) {
        Sprintf(dest, SP_APPEND, "%ld", (long)sec);
    } else {
#ifdef HAVE_strftime
        CONST char *s;
        static char fmtbuf[3] = "%?";  /* static to allow init in K&R C */
        struct tm *local = NULL;
        if (!*fmt) fmt = "%c";
        for (s = fmt; *s; s++) {
            if (*s != '%') {
                Stringadd(dest, *s);
            } else if (*++s == '@') {
                Sprintf(dest, SP_APPEND, "%ld", sec);
            } else if (*s == '.') {
                Sprintf(dest, SP_APPEND, "%02ld", (usec + 5000) / 10000);
            } else {
                if (!local) local = localtime(&sec);
                fmtbuf[1] = *s;
                Stringterm(dest, dest->len + 32);
                dest->len += strftime(dest->s + dest->len, 32, fmtbuf, local);
            }
        }
#else
        char *str = ctime(&t);
        Stringncat(dest, str, strlen(str) - 1);   /* remove ctime()'s '\n' */
#endif
    }
    return dest->len - oldlen;
}

void internal_error(file, line)
    CONST char *file;
    int line;
{
    eprintf("Internal error at %s:%d, %s.  %s", file, line, version,
        "Please report this to the author, and describe what you did.");
}

void die(why, err)
    CONST char *why;
    int err;
{
    fix_screen();
    reset_tty();
    if ((errno = err)) perror(why);
    else {
        fputs(why, stderr);
        fputc('\n', stderr);
    }
    exit(1);
}

