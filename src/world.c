/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: world.c,v 35004.27 1997/11/16 22:05:01 hawkeye Exp $ */


/********************************************************
 * Fugue world routines.                                *
 ********************************************************/

#include "config.h"
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "tfio.h"
#include "history.h"
#include "world.h"
#include "process.h"
#include "macro.h"	/* remove_world_macros() */
#include "commands.h"
#include "socket.h"

#define LW_HIDE		001
#define LW_UNNAMED	002
#define LW_SHORT	004

static int  FDECL(list_worlds,(CONST Pattern *name, CONST Pattern *type,
    struct TFILE *file, int flags));
static void FDECL(free_fields,(World *w));
static void FDECL(free_world,(World *w));

static World *hworld = NULL, *defaultworld = NULL;

static void free_fields(w)
    World *w;
{
    FREE(w->name);
    FREE(w->character);
    FREE(w->pass);
    FREE(w->host);
    FREE(w->port);
    FREE(w->mfile);
    FREE(w->type);
}

static void free_world(w)
    World *w;
{
    free_fields(w);
#ifndef NO_HISTORY
    if (w->history) {
        free_history(w->history);
        FREE(w->history);
    }
#endif
    FREE(w);
}

#ifdef DMALLOC
void free_worlds()
{
    World *next;

    if (defaultworld)
        free_world(defaultworld);
    for ( ; hworld; hworld = next) {
        next = hworld->next;
        free_world(hworld);
    }
}
#endif

/* A NULL name means unnamed; world will be given a temp name. */
World *new_world(name, character, pass, host, port, mfile, type, flags)
    CONST char *name, *character, *pass, *host, *port, *mfile, *type;
    int flags;
{
    World *result;
    static int unnamed = 1;
    smallstr buffer;
    int is_redef = FALSE;
 
    if (name && cstrcmp(name, "default") == 0) {
        if (defaultworld) {
            free_fields(defaultworld);
            is_redef = TRUE;
        } else defaultworld = (World *)XMALLOC(sizeof(World));
        result = defaultworld;
#ifndef NO_HISTORY
        result->history = NULL;
#endif
    } else if (name && (result = find_world(name))) {
        free_fields(result);
        is_redef = TRUE;
    } else {
        World **pp;
        for (pp = &hworld; *pp; pp = &(*pp)->next);
        *pp = result = (World *) XMALLOC(sizeof(World));
        result->next = NULL;
#ifndef NO_HISTORY
        /* Don't allocate the history's queue until we actually need it. */
        result->history = init_history(NULL, 0);
#endif
    }

    if (name) {
        result->name      = STRDUP(name);
    } else {
        sprintf(buffer, "(unnamed%d)", unnamed++);
        result->name      = STRDUP(buffer);
    }
    result->character = STRDUP(character);
    result->pass      = STRDUP(pass);
    result->host      = STRDUP(host);
    result->port      = STRDUP(port);
    result->mfile     = STRDUP(mfile);
    result->type      = STRDUP(type);
    if (!is_redef) {
        result->flags = 0;
        result->sock = NULL;
    }
    result->flags     |= flags;

#ifdef PLATFORM_UNIX
# ifndef __CYGWIN32__
    if (*pass && loadfile && (loadfile->mode & (S_IROTH | S_IRGRP)) &&
        !loadfile->warned)
    {
        eprintf("Warning: file contains passwords and is readable by others.");
        loadfile->warned++;
    }
# endif /* __CYGWIN32__ */
#endif /* PLATFORM_UNIX */

    if (is_redef)
        do_hook(H_REDEF, "%% Redefined %s %s", "%s %s", "world", result->name);
    return result;
}

struct Value *handle_addworld_command(args)
    char *args;
{
    int count;
    World *w;
    char opt, *type = NULL;
    char *fields[6];
    int error = 0;
    int flags = 0;

    if (restrict >= RESTRICT_WORLD) {
        eprintf("restricted");
        return newint(0);
    }

    startopt(args, "pT:");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
        case 'T':
            if (type) FREE(type);
            type = STRDUP(args);
            break;
        case 'p':
            flags |= WORLD_NOPROXY;
            break;
        default:
            return newint(0);
        }
    }
    if (!type) type = STRNDUP("", 0);

    for (count = 0; *args && count < 6; count++) {
        fields[count] = stringarg(&args, NULL);
    }
    if (count == 1) {
        if (!(w = find_world(fields[0]))) {
            eprintf("%s: no such world", fields[0]);
            error++;
        } else {
            free(w->type);
            w->type = STRDUP(type);
            w->flags |= flags;
        }
    } else if (count < 3 || count > 6) {
        eprintf("wrong number of arguments");
        error++;
    } else if (*fields[0] == '(') {
        /* we can't allow user to assign names like "(unnamed1)" */
        eprintf("illegal world name: %s", fields[0]);
        error++;
    } else if (count >= 5) {
        error = !new_world(fields[0], fields[1], fields[2], fields[3],
            fields[4], count == 6 ? fields[5] : "", type, flags);
    } else if (cstrcmp(fields[0], "default") == 0) {
        error = !new_world(fields[0], fields[1], fields[2], "", "",
            count == 4 ? fields[3] : "", type, flags);
    } else {
        error = !new_world(fields[0], "", "", fields[1], fields[2],
            count == 4 ? fields[3] : "", type, flags);
    }
    FREE(type);
    return newint(!error);
}

/* should not be called for defaultworld */
int nuke_world(w)
    World *w;
{
    World **pp;

    if (w->sock) {
        eprintf("%s is in use.", w->name);
        return 0;
    }

    for (pp = &hworld; *pp != w; pp = &(*pp)->next);
    *pp = w->next;
    remove_world_macros(w);
    kill_procs_by_world(w);
    free_world(w);
    return 1;
}

struct Value *handle_unworld_command(args)
    char *args;
{
    World *w;
    int result = 0;
    CONST char *name;

    while (*(name = stringarg(&args, NULL))) {
        if (defaultworld && cstrcmp(name, "default") == 0) {
            free_world(defaultworld);
            defaultworld = NULL;
        } else if ((w = find_world(name))) {
            result += nuke_world(w);
        } else {
            eprintf("No world %s", name);
        }
    }
    return newint(result);
}


struct Value *handle_listworlds_command(args)
    char *args;
{
    int flags = LW_HIDE, mflag = matching, error = 0, result;
    char c;
    Pattern type, name;

    init_pattern_str(&type, NULL);
    init_pattern_str(&name, NULL);
    startopt(args, "T:uscm:");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
            case 'T':  free_pattern(&type);
                       error += !init_pattern_str(&type, args);
                       break;
            case 'u':  flags |= LW_UNNAMED; break;
            case 's':  flags |= LW_SHORT; break;
            case 'c':  flags &= ~LW_HIDE; break;
            case 'm':  error = ((mflag = enum2int(args, enum_match, "-m")) < 0);
                       break;
            default:   return newint(0);
        }
    }
    if (error) return newint(0);
    init_pattern_mflag(&type, mflag);
    if (*args) error += !init_pattern(&name, args, mflag);
    if (error) return newint(0);
    result = list_worlds(*args?&name:NULL, type.str?&type:NULL, NULL, flags);
    free_pattern(&name);
    free_pattern(&type);
    return newint(result);
}

static int list_worlds(name, type, file, flags)
    CONST Pattern *name, *type;
    TFILE *file;
    int flags;
{
    World *p;
    int first = 1, count = 0;
    STATIC_BUFFER(buf);

    Stringterm(buf, 0);
    for (p = defaultworld; p || first; p = first ? hworld : p->next, first=0)
    {
        if (!p || (!(flags & LW_UNNAMED) && p->flags & WORLD_TEMP)) continue;
        if (name && !patmatch(name, p->name)) continue;
        if (type && !patmatch(type, p->type)) continue;
        count++;
        if (flags & LW_SHORT) {
            tfputs(p->name, file ? file : tfout);
        } else {
            Stringcpy(buf, "/addworld ");
            if (*p->type) Sprintf(buf, SP_APPEND, "-T'%q' ", '\'', p->type);
            if (p->flags & WORLD_NOPROXY) Stringcat(buf, "-p ");
            Sprintf(buf, SP_APPEND, "%s ", p->name);
            if (*p->character) {
                Sprintf(buf, SP_APPEND, "%s ", p->character);
                if (!(flags & LW_HIDE)) Sprintf(buf, SP_APPEND, "%s ", p->pass);
            }
            if (*p->host) Sprintf(buf, SP_APPEND, "%s %s ", p->host, p->port);
            if (*p->mfile) Stringcat(buf, p->mfile);
            tfputs(buf->s, file ? file : tfout);
        }
    }
    return count;
}

struct Value *handle_saveworld_command(args)
    char *args;
{
    TFILE *file;
    char opt;
    CONST char *mode = "w";
    char *name;
    int result;

    if (restrict >= RESTRICT_FILE) {
        eprintf("restricted");
        return newint(0);
    }

    startopt(args, "a");
    while ((opt = nextopt(&args, NULL))) {
        if (opt != 'a') return newint(0);
        mode = "a";
    }
    if ((name = tfname(args, "WORLDFILE")) == NULL)
        return newint(0);
    if ((file = tfopen(name, mode)) == NULL) {
        operror(args);
        return newint(0);
    }
    oprintf("%% %sing world definitions to %s", *mode == 'a' ? "Append" :
        "Writ", file->name);
    result = list_worlds(NULL, NULL, file, 0);
    tfclose(file);
    return newint(result);
}

World *get_default_world()
{
    return defaultworld;
}

World *find_world(name)
    CONST char *name;
{
    World *p;

    if (!name || !*name) return hworld;
    for (p=hworld; p && (!p->name || cstrcmp(name, p->name) != 0); p = p->next);
    return p;
}

/* Perform (*func)(world) on every world */
void mapworld(func)
    void FDECL((*func),(World *world));
{
    World *w;

    for (w = hworld; w; w = w->next)
        (*func)(w);
}

