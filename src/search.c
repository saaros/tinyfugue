/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.c,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */


/**********************************************
 * hash table and linked list routines        *
 **********************************************/

#include "config.h"
#include "port.h"
#include "tf.h"
#include "util.h"
#include "search.h"

static unsigned int FDECL(hash_string,(char *str));

#if 1
void init_list(list)
    List *list;
{
    list->head = list->tail = NULL;
}
#endif

GENERIC *unlist(node, list)                /* delete Node from linked list */
    ListEntry *node;
    List *list;
{
    GENERIC *result;

    if (node->next) node->next->prev = node->prev;
    else list->tail = node->prev;
    if (node->prev) node->prev->next = node->next;
    else list->head = node->next;
    result = node->data;
    FREE(node);
    return result;
}

/* Create new node for data and insert into list in sorted order.
 */
ListEntry *sinsert(data, list, cmp)
    GENERIC *data;
    List *list;
    Cmp *cmp;
{
    ListEntry *node, *prev;

    prev = NULL;
    node = list->head;
    while (node && (*cmp)(data, node->data) > 0) {
        prev = node;
        node = node->next;
    }
    return inlist(data, list, prev);
}

/* Create new node for data and insert into list.
 * If where is non-null, insert after it; else, insert at beginning
 */
ListEntry *inlist(data, list, where)
    GENERIC *data;
    List *list;
    ListEntry *where;
{
    ListEntry *node;

    node = (ListEntry *)MALLOC(sizeof(ListEntry));
    node->data = data;
    if (!where) {
        if (list->head) list->head->prev = node;
        node->next = list->head;
        list->head = node;
    } else {
        node->next = where->next;
        where->next = node;
    }
    node->prev = where;
    if (node->next) node->next->prev = node;
    else list->tail = node;
    return node;
}

/* Remove all elements from src queue and put them on dest queue. */
/* Much more efficient than dequeuing and enqueuing every element. */
void queuequeue(src, dest)
    Queue *src, *dest;
{
    if (!src->head) return;
    src->tail->next = dest->head;
    *(dest->head ? &dest->head->prev : &dest->tail) = src->tail;
    dest->head = src->head;
    src->head = src->tail = NULL;
}

void free_queue(q)
    Queue *q;
{
    ListEntry *node;

    while ((node = q->tail)) {
        q->tail = q->tail->prev;
        free_aline((Aline *)node->data);
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
            if ((*table->cmp)(name, *(char**)node->data) == 0)
                return node->data;
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

ListEntry *hash_insert(data, table)     /* add node to hash table */
    GENERIC *data;
    HashTable *table;
{
    int indx;

    indx = hash_string(*(char**)data) % table->size;
    if (!table->bucket[indx]) {
        table->bucket[indx] = (List *)MALLOC(sizeof(List));
        init_list(table->bucket[indx]);
    }
    return inlist(data, table->bucket[indx], NULL);
}


void hash_remove(node, table)         /* remove macro from hash table */
    ListEntry *node;
    HashTable *table;
{
    int indx;

    indx = hash_string(*(char**)node->data) % table->size;
    unlist(node, table->bucket[indx]);
}

/*
 * binsearch - binary search by string
 * (base) points to an array of (nel) structures of (size) bytes.
 * The array must be sorted in ascending order.  Unlike ANSI's bsearch(),
 * the first member of the structures must be the string used for comparison.
 * Returns index of element matching (key), or -1 if not found.
 */

int binsearch(key, base, nel, size, cmp)
    char *key;
    GENERIC *base;
    int nel, size;
    int FDECL((*cmp),(CONST char *, CONST char *));
{
    int bottom, top, mid, value;

    bottom = 0;
    top = nel - 1;
    while (bottom <= top) {
        mid = (top + bottom) / 2;
        value = (*cmp)(key, *(char **)((char *)base + size * mid));
        if (value < 0) top = mid - 1;
        else if (value > 0) bottom = mid + 1;
        else return mid;
    }
    return -1;
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
