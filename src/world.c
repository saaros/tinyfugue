/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: world.c,v 33000.1 1994/03/23 01:48:53 hawkeye Exp $ */


/********************************************************
 * Fugue world routines.                                *
 ********************************************************/

#include "config.h"
#include <ctype.h>
#include "port.h"
#include "dstring.h"
#include "tf.h"
#include "util.h"
#include "history.h"
#include "world.h"
#include "output.h"
#include "process.h"
#include "macro.h"
#include "search.h"
#include "commands.h"

extern int restrict;

static void FDECL(free_fields,(World *w));
static void FDECL(replace_world,(World *old, World *new));
static World *FDECL(insertworld,(World *world));

static World *hworld = NULL, *defaultworld = NULL;

static void free_fields(w)
    World *w;
{
    FREE(w->name);
    FREE(w->character);
    FREE(w->pass);
    FREE(w->address);
    FREE(w->port);
    FREE(w->mfile);
    FREE(w->type);
}

void free_world(w)
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

static void replace_world(old, new)
    World *old, *new;
{
    free_fields(old);
    old->name      = new->name;
    old->character = new->character;
    old->pass      = new->pass;
    old->address   = new->address;
    old->port      = new->port;
    old->mfile     = new->mfile;
    old->type      = new->type;
    FREE(new);
    do_hook(H_REDEF, "%% Redefined %s %s", "%s %s", "world", old->name);
}

World *new_world(name, character, pass, address, port, mfile, type)
    char *name, *character, *pass, *address, *port, *mfile, *type;
{
    World *result;
    static int unnamed = 1;
    char buffer[16];               /* big enough for "(unnamedNNNNNN)" */
 
    result = (World *) MALLOC(sizeof(World));
    if (name) {
        result->name      = STRDUP(name);
    } else {
        sprintf(buffer, "(unnamed%d)", unnamed++);
        result->name      = STRDUP(buffer);
    }
    result->character = STRDUP(character);
    result->pass      = STRDUP(pass);
    result->address   = STRDUP(address);
    result->port      = STRDUP(port);
    result->mfile     = STRDUP(mfile);
    result->type      = STRDUP(type);
    result->flags = 0;
    result->sock = NULL;
#ifndef NO_HISTORY
    /* Don't allocate the history's queue until we actually need it. */
    init_history((result->history = (History *)MALLOC(sizeof(History))), 0);
#endif
    return insertworld(result);
}

int handle_addworld_command(args)
    char *args;
{
    int count;
    World *new = NULL;
    char opt, *in, *type = NULL;
    char *fields[6];

    if (restrict >= RESTRICT_WORLD) {
        tfputs("% /addworld restricted", tferr);
        return 0;
    }
    if (!*args) return 0;

    startopt(args, "T:");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
        case 'T':  type = STRDUP(args);  break;
        default:   return 0;
        }
    }
    if (!type) type = STRNDUP("", 0);

    for (in = args = STRDUP(args), count = 0; *in && count < 6; count++) {
        fields[count] = stringarg(&in, NULL);
    }
    if (count == 1) {
        if (!(new = find_world(fields[0]))) {
            tfprintf(tferr, "%% %s: no such world", fields[0]);
        } else {
            FREE(new->type);
            new->type = STRDUP(type);
            do_hook(H_REDEF, "%% Redefined %s %s", "%s %s", "world", new->name);
        }
    } else if (count < 3 || count > 6) {
        tfputs("% Illegal world format", tferr);
    } else if (count >= 5) {
        new = new_world(fields[0], fields[1], fields[2], fields[3],
            fields[4], count == 6 ? fields[5] : "", type);
    } else if (cstrcmp(fields[0], "default") == 0) {
        new = new_world(fields[0], fields[1], fields[2], "", "",
            count == 4 ? fields[3] : "", type);
    } else {
        new = new_world(fields[0], "", "", fields[1], fields[2],
            count == 4 ? fields[3] : "", type);
    }
    if (new) new->flags &= ~WORLD_TEMP;
    FREE(type);
    FREE(args);
    return 1;
}

static World *insertworld(world)
    World *world;
{
    World *w;

    if (cstrcmp(world->name, "default") == 0) {
        if (defaultworld) replace_world(defaultworld, world);
        else defaultworld = world;
    } else if ((w = find_world(world->name)) != NULL) {
        replace_world(w, world);
        return w;
    } else if (hworld == NULL) {
        hworld = world;
        world->next = NULL;
    } else {
        for (w = hworld; w->next != NULL; w = w->next);
        w->next = world;
        world->next = NULL;
    }
    return world;
}

void nuke_world(w)
    World *w;
{
    World *t;

    if (w->sock) {
        tfprintf(tferr, "%% %s: Cannot nuke world currently in use.", w->name);
    } else {
        if (w == hworld) hworld = w->next;
        else {
            for (t = hworld; t->next != w; t = t->next);
            t->next = w->next;
        }
        remove_world_macros(w);
        kill_procs_by_world(w);
        free_world(w);
    }
}

int handle_unworld_command(args)
    char *args;
{
    World *w;

    if (*args && (w = find_world(args))) {
        nuke_world(w);
        return 1;
    }
    tfprintf(tferr, "%% No world %s", args);
    return 0;
}

int handle_purgeworld_command(args)
    char *args;
{
    World *world, *next;
    Pattern pat;

    if (!*args || !init_pattern(&pat, args, matching)) return 0;
    for (world = hworld; world; world = next) {
        next = world->next;
        if (patmatch(&pat, world->name, matching, TRUE)) nuke_world(world);
    }
    free_pattern(&pat);
    return 1;
}

int list_worlds(full, args, file)
    int full;
    char *args;
    TFILE *file;
{
    World *p;
    int first = 1;
    Pattern pat;
    STATIC_BUFFER(buf);

    if (!init_pattern(&pat, args, matching)) return 0;
    for (p = defaultworld; p || first; p = first ? hworld : p->next, first=0) {
        if (!p || p->flags & WORLD_TEMP) continue;
        if (args && !patmatch(&pat, p->name, matching, TRUE)) continue;
        Stringcpy(buf, "/addworld ");
        if (*p->type) Sprintf(buf, SP_APPEND, "-T'%q' ", '\'', p->type);
        Sprintf(buf, SP_APPEND, "%s ", p->name);
        if (*p->character) {
            Sprintf(buf, SP_APPEND, "%s ", p->character);
            if (full) Sprintf(buf, SP_APPEND, "%s ", p->pass);
        }
        if (*p->address) Sprintf(buf, SP_APPEND, "%s %s ", p->address, p->port);
        if (*p->mfile) Stringcat(buf, p->mfile);
        if (file) tfputs(buf->s, file);
        else oputs(buf->s);
    }
    free_pattern(&pat);
    return 1;
}

int handle_saveworld_command(args)
    char *args;
{
    TFILE *file;
    char opt, *mode = "w";

    if (restrict >= RESTRICT_FILE) {
        tfputs("% /saveworld: restricted", tferr);
        return 0;
    }

    startopt(args, "a");
    while ((opt = nextopt(&args, NULL))) {
        switch (opt) {
        case 'a': mode = "a"; break;
        default:  return 0;
        }
    }
    if ((file = tfopen(tfname(args, "WORLDFILE"), mode)) == NULL) {
        operror(args);
        return 0;
    }
    oprintf("%% %sing world definitions to %s", *mode == 'a' ? "Append" :
        "Writ", file->name);
    list_worlds(TRUE, NULL, file);
    tfclose(file);
    return 1;
}

World *get_default_world()
{
    return defaultworld;
}

World *find_world(name)
    char *name;
{
    World *p;

    if (!name || !*name) return hworld;
    for (p=hworld; p && (!p->name || cstrcmp(name, p->name) != 0); p = p->next);
    return p;
}

