/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.c,v 33000.1 1994/04/26 08:56:29 hawkeye Exp $ */


/**********************************************
 * hash table and linked list routines        *
 **********************************************/

#include "config.h"
#include "port.h"
#include "tf.h"
#include "util.h"
#include "search.h"

static unsigned int FDECL(hash_string,(char *str));

void init_list(list)
    List *list;
{
    list->head = list->tail = NULL;
}

GENERIC *unlist(node, list)                /* delete Node from linked list */
    ListEntry *node;
    List *list;
{
    GENERIC *result;

    *(node->next ? &node->next->prev : &list->tail) = node->prev;
    *(node->prev ? &node->prev->next : &list->head) = node->next;
    result = node->datum;
    FREE(node);
    return result;
}

/* Create new node for datum and insert into list in sorted order.
 */
ListEntry *sinsert(datum, list, cmp)
    GENERIC *datum;
    List *list;
    Cmp *cmp;
{
    ListEntry *node, *prev;

    prev = NULL;
    node = list->head;
    while (node && (*cmp)(datum, node->datum) > 0) {
        prev = node;
        node = node->next;
    }
    return inlist(datum, list, prev);
}

/* Create new node for datum and insert into list.
 * If where is non-null, insert after it; else, insert at beginning
 */
ListEntry *inlist(datum, list, where)
    GENERIC *datum;
    List *list;
    ListEntry *where;
{
    ListEntry *node;

    node = (ListEntry *)MALLOC(sizeof(ListEntry));
    node->datum = datum;
    if (!where) {
        if (list->head) list->head->prev = node;
        node->next = list->head;
        list->head = node;
    } else {
        node->next = where->next;
        where->next = node;
    }
    node->prev = where;
    *(node->next ? &node->next->prev : &list->tail) = node;
    return node;
}

void free_queue(q)
    Queue *q;
{
    ListEntry *node;

    while ((node = q->tail)) {
        q->tail = q->tail->prev;
        free_aline((Aline *)node->datum);
        FREE(node);
    }
    q->head = NULL;
}

void init_hashtable(table, size, cmp)
    HashTable *table;
    int size;
    int FDECL((*cmp),(CONST char *, CONST char *));
{
    int i;

    table->size = size;
    table->cmp = cmp;
    table->bucket = (List **)MALLOC(sizeof(List *) * size);
    for (i = 0; i < size; i++) {
        table->bucket[i] = NULL;
    }
}

GENERIC *hash_find(name, table)       /* find entry by name */
    char *name;
    HashTable *table;
{
    List *bucket;
    ListEntry *node;

    bucket = table->bucket[hash_string(name) % table->size];
    if (bucket) {
        for (node = bucket->head; node; node = node->next)
            if ((*table->cmp)(name, *(char**)node->datum) == 0)
                return node->datum;
    }
    return NULL;
}

static unsigned int hash_string(str)
    char *str;
{
    unsigned int h;

    for (h = 0; *str; str++)
        h = (h << 5) + h + lcase(*str);
    return h;
}

ListEntry *hash_insert(datum, table)     /* add node to hash table */
    GENERIC *datum;
    HashTable *table;
{
    int indx;

    indx = hash_string(*(char**)datum) % table->size;
    if (!table->bucket[indx]) {
        table->bucket[indx] = (List *)MALLOC(sizeof(List));
        init_list(table->bucket[indx]);
    }
    return inlist(datum, table->bucket[indx], NULL);
}


void hash_remove(node, table)         /* remove macro from hash table */
    ListEntry *node;
    HashTable *table;
{
    int indx;

    indx = hash_string(*(char**)node->datum) % table->size;
    unlist(node, table->bucket[indx]);
}

#ifndef HAVE_BSEARCH
/*
 * binsearch - replacement for bsearch().
 */
GENERIC *binsearch(key, base, nel, size, cmp)
    CONST GENERIC *key, *base;
    int nel, size;
    int FDECL((*cmp),(CONST GENERIC *, CONST GENERIC *));
{
    int bottom, top, mid, value;

    bottom = 0;
    top = nel - 1;
    while (bottom <= top) {
        mid = (top + bottom) / 2;
        value = (*cmp)(key, (GENERIC *)((char *)base + size * mid));
        if (value < 0) top = mid - 1;
        else if (value > 0) bottom = mid + 1;
        else return (GENERIC *)((char *)base + size * mid);
    }
    return NULL;
}
#endif

/* genstrcmp - does a strcmp on the first field of two structures */
int genstrcmp(key, datum)
    CONST GENERIC *key, *datum;
{
    return strcmp(*(char **)key, *(char **)datum);
}

/* gencstrcmp - does a cstrcmp on the first field of two structures */
int gencstrcmp(key, datum)
    CONST GENERIC *key, *datum;
{
    return cstrcmp(*(char **)key, *(char **)datum);
}

#ifdef DMALLOC
void free_hash(table)
    HashTable *table;
{
    int i;
    for (i = 0; i < table->size; i++) {
        if (table->bucket[i]) FREE(table->bucket[i]);
    }
    FREE(table->bucket);
}
#endif
