/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: world.c,v 35004.39 1999/01/31 00:27:58 hawkeye Exp $ */


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

#define LW_TABLE	001
#define LW_UNNAMED	002
#define LW_SHORT	004

static int  FDECL(list_worlds,(CONST Pattern *name, CONST Pattern *type,
    struct TFILE *file, int flags));
static void FDECL(free_world,(World *w));
static World *NDECL(alloc_world);

static World *hworld = NULL;

World *defaultworld = NULL;


static void free_world(w)
    World *w;
{
    if (w->name)      FREE(w->name);
    if (w->character) FREE(w->character);
    if (w->pass)      FREE(w->pass);
    if (w->host)      FREE(w->host);
    if (w->port)      FREE(w->port);
    if (w->mfile)     FREE(w->mfile);
    if (w->type)      FREE(w->type);
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

static World *alloc_world()
{
    World *result;
    result = (World *) XMALLOC(sizeof(World));
    result->type      = NULL;
    result->character = NULL;
    result->pass      = NULL;
    result->host      = NULL;
    result->port      = NULL;
    result->mfile     = NULL;
    result->flags     = 0;
    result->sock = NULL;
    result->next = NULL;
#ifndef NO_HISTORY
    result->history = NULL;
#endif
    return result;
}

/* A NULL name means unnamed; world will be given a temp name. */
World *new_world(name, character, pass, host, port, mfile, type, flags)
    CONST char *name, *character, *pass, *host, *port, *mfile, *type;
    int flags;
{
    World *result;
    static int unnamed = 1;
    smallstr buffer;
    int is_redef = FALSE;

    /* unnamed worlds can't be defined but can have other fields changed. */
    if (name && *name == '(' && (*host || *port)) {
        eprintf("illegal world name: %s", name);
        return NULL;
    }

    if (name && cstrcmp(name, "default") == 0) {
        if (defaultworld) {
            result = defaultworld;
            FREE(defaultworld->name);
            is_redef = TRUE;
        } else {
            result = defaultworld = alloc_world();
        }
    } else if (name && (result = find_world(name))) {
        FREE(result->name);
        is_redef = TRUE;

    } else {
        World **pp;
        if (!*host || !*port) {
            eprintf("new world requires host and port.");
            return NULL;
        }
        for (pp = &hworld; *pp; pp = &(*pp)->next);
        *pp = result = alloc_world();
#ifndef NO_HISTORY
        /* Don't allocate the history's queue until we actually need it. */
        result->history = init_history(NULL, 0);
#endif
    }

    if (name) {
        result->name = STRDUP(name);
    } else {
        sprintf(buffer, "(unnamed%d)", unnamed++);
        result->name = STRDUP(buffer);
    }

#define setfield(field) \
    do { \
        if (field && *field) { \
            if (result->field) FREE(result->field); \
            result->field = STRDUP(field); \
        } \
    } while (0);

    setfield(character);
    setfield(pass);
    setfield(host);
    setfield(port);
    setfield(mfile);
    setfield(type);
    result->flags |= flags;

#ifdef PLATFORM_UNIX
# ifndef __CYGWIN32__
    if (pass && *pass && loadfile && (loadfile->mode & (S_IROTH | S_IRGRP)) &&
        !loadfile->warned)
    {
        eprintf("Warning: file contains passwords and is readable by others.");
        loadfile->warned++;
    }
# endif /* __CYGWIN32__ */
#endif /* PLATFORM_UNIX */

    if (is_redef)
        do_hook(H_REDEF, "!Redefined %s %s", "%s %s", "world", result->name);
    return result;
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
    int flags = LW_TABLE, mflag = matching, error = 0, result;
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
            case 's':  flags |= LW_SHORT;   /* fall through */
            case 'c':  flags &= ~LW_TABLE;  break;
            case 'm':  error = ((mflag = enum2int(args, enum_match, "-m")) < 0);
                       break;
            default:   return newint(0);
        }
    }
    if (error) return newint(0);
    init_pattern_mflag(&type, mflag);
    if (*args) error += !init_pattern(&name, args, mflag);
    if (error) return newint(0);
    result = list_worlds(*args?&name:NULL, type.str?&type:NULL, tfout, flags);
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
    int first = 1, count = 0, need;
    STATIC_BUFFER(buf);

    if (flags & LW_TABLE) {
        tfprintf(file, "%-15s %-10s %26s %-6s %s",
            "NAME", "TYPE", "HOST", "PORT", "CHARACTER");
    }

    Stringterm(buf, 0);
    for (p = defaultworld; p || first; p = first ? hworld : p->next, first=0)
    {
        if (!p || (!(flags & LW_UNNAMED) && p->flags & WORLD_TEMP)) continue;
        if (name && !patmatch(name, p->name)) continue;
        if (type && !patmatch(type, p->type ? p->type : "")) continue;
        count++;
        if (flags & LW_SHORT) {
            tfputs(p->name, file);
        } else if (flags & LW_TABLE) {
            tfprintf(file, "%-15.15s %-10.10s %26.26s %-6.6s %s",
                p->name, p->type, p->host, p->port, p->character);
        } else {
            if (p->flags & WORLD_NOPROXY) need = 8;
            else if (p->mfile) need = 7;
            else if (p->character || p->pass) need = 6;
            else need = 4;

            Stringcpy(buf, "/test addworld(");
            Sprintf(buf, SP_APPEND, "\"%q\"", '"', p->name);
            Sprintf(buf, SP_APPEND, ", \"%q\"", '"', p->type);
            Sprintf(buf, SP_APPEND, ", \"%q\"", '"', p->host);
            Sprintf(buf, SP_APPEND, ", \"%q\"", '"', p->port);

            if (need < 5) goto listworld_tail;
            Sprintf(buf, SP_APPEND, ", \"%q\"", '"', p->character);

            if (need < 6) goto listworld_tail;
            Sprintf(buf, SP_APPEND, ", \"%q\"", '"', p->pass);

            if (need < 7) goto listworld_tail;
            Sprintf(buf, SP_APPEND, ", \"%q\"", '"', p->mfile);

            if (need < 8) goto listworld_tail;
            Sprintf(buf, SP_APPEND, ", \"%s\"",
                enum_flag[!(p->flags & WORLD_NOPROXY)]);

listworld_tail:
            Sprintf(buf, SP_APPEND, ")");
            tfputs(buf->s, file);
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

    if (restriction >= RESTRICT_FILE) {
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

