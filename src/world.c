/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: world.c,v 35004.16 1997/03/27 01:04:57 hawkeye Exp $ */


/********************************************************
 * Fugue world routines.                                *
 ********************************************************/

#include <sys/types.h>
#include <sys/stat.h>
#ifndef S_IROTH
# define S_IROTH 00004
# define S_IRGRP 00040
#endif

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

extern int restrict;

static int  FDECL(list_worlds,(CONST char *pattern, struct TFILE *file,
    int complete, int shortflag));
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

/* A NULL name means unnamed; world will be given a temp name. */
World *new_world(name, character, pass, host, port, mfile, type, flags)
    CONST char *name, *character, *pass, *host, *port, *mfile, *type;
    int flags;
{
    World *result;
    static int unnamed = 1;
    smallstr buffer;
    int is_redef = FALSE;
    extern TFILE *loadfile;
 
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
    if (*pass && loadfile && (loadfile->mode & (S_IROTH | S_IRGRP)) &&
        !loadfile->warned)
    {
        eprintf("Warning: %s  %s \"chmod 600 '%s'\".",
            "file contains passwords readable by others.",
            "It can be fixed with the shell command", loadfile->name);
        loadfile->warned++;
    }
#endif /* PLATFORM_UNIX */

    if (is_redef)
        do_hook(H_REDEF, "%% Redefined %s %s", "%s %s", "world", result->name);
    return result;
}

int handle_addworld_command(args)
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
        return 0;
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
            return 0;
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
    return !error;
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

int handle_unworld_command(args)
    char *args;
{
    World *w;

    if (defaultworld && cstrcmp(args, "default") == 0) {
        free_world(defaultworld);
        defaultworld = NULL;
        return 1;
    } else if (*args && (w = find_world(args))) {
        return nuke_world(w);
    }
    eprintf("No world %s", args);
    return 0;
}

int handle_purgeworld_command(args)
    char *args;
{
    World *world, *next;
    Pattern pat;

    if (!*args || !init_pattern(&pat, args, matching)) return 0;
    if (defaultworld && patmatch(&pat, "default", matching)) {
        free_world(defaultworld);
        defaultworld = NULL;
    }
    for (world = hworld; world; world = next) {
        next = world->next;
        if (patmatch(&pat, world->name, matching)) nuke_world(world);
    }
    free_pattern(&pat);
    return 1;
}


int handle_listworlds_command(args)
    char *args;
{
    int complete = FALSE, shortflag = FALSE;
    char c;

    startopt(args, "cs");
    while ((c = nextopt(&args, NULL))) {
        switch (c) {
            case 'c':  complete = TRUE; break;
            case 's':  shortflag = TRUE; break;
            default:   return 0;
        }
    }
    return list_worlds(*args ? args : NULL, NULL, complete, shortflag);
}

static int list_worlds(args, file, complete, shortflag)
    CONST char *args;
    TFILE *file;
    int complete, shortflag;
{
    World *p;
    int first = 1, count = 0;
    Pattern pat;
    STATIC_BUFFER(buf);

    if (!init_pattern(&pat, args, matching)) return 0;
    Stringterm(buf, 0);
    for (p = defaultworld; p || first; p = first ? hworld : p->next, first=0)
    {
        if (!p || p->flags & WORLD_TEMP) continue;
        if (args && !patmatch(&pat, p->name, matching)) continue;
        count++;
        if (shortflag) {
            Sprintf(buf, SP_APPEND, "%s ", p->name);
        } else {
            Stringcpy(buf, "/addworld ");
            if (*p->type) Sprintf(buf, SP_APPEND, "-T'%q' ", '\'', p->type);
            if (p->flags & WORLD_NOPROXY) Stringcat(buf, "-p ");
            Sprintf(buf, SP_APPEND, "%s ", p->name);
            if (*p->character) {
                Sprintf(buf, SP_APPEND, "%s ", p->character);
                if (complete) Sprintf(buf, SP_APPEND, "%s ", p->pass);
            }
            if (*p->host) Sprintf(buf, SP_APPEND, "%s %s ", p->host, p->port);
            if (*p->mfile) Stringcat(buf, p->mfile);
            tfputs(buf->s, file ? file : tfout);
        }
    }
    if (shortflag) {
        tfputs(buf->s, file ? file : tfout);
    }
    free_pattern(&pat);
    return count;
}

int handle_saveworld_command(args)
    char *args;
{
    TFILE *file;
    char opt;
    CONST char *mode = "w";
    char *name;
    int result;

    if (restrict >= RESTRICT_FILE) {
        eprintf("restricted");
        return 0;
    }

    startopt(args, "a");
    while ((opt = nextopt(&args, NULL))) {
        if (opt != 'a') return 0;
        mode = "a";
    }
    if ((name = tfname(args, "WORLDFILE")) == NULL)
        return 0;
    if ((file = tfopen(name, mode)) == NULL) {
        operror(args);
        return 0;
    }
    oprintf("%% %sing world definitions to %s", *mode == 'a' ? "Append" :
        "Writ", file->name);
    result = list_worlds(NULL, file, TRUE, FALSE);
    tfclose(file);
    return result;
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

