/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.c,v 35004.10 1999/01/31 00:27:51 hawkeye Exp $ */


/**********************************************
 * trie, hash table, and linked list routines *
 **********************************************/

#include "config.h"
#include "port.h"
#include "malloc.h"
#include "search.h"


static ListEntry *nodepool = NULL;		/* freelist */

static unsigned int FDECL(hash_string,(CONST char *str));


/********/
/* trie */
/********/

/* Find the datum in trie assosiated with the key. */
GENERIC *trie_find(root, key)
    TrieNode *root;
    CONST unsigned char *key;
{
    TrieNode *n;

    for (n = root; n && n->children && *key; n = n->u.child[*key++]);
    return (n && !n->children && !*key) ? n->u.datum : NULL;
}

/* Insert a datum into the trie pointed to by root.
 * If key is substring, superstring, or duplicate of an existing key, intrie()
 * returns TRIE_SUB, TRIE_SUPER, or TRIE_DUP and does not insert.
 * Otherwise, returns 1 for success.
 */
int intrie(root, datum, key)
    TrieNode **root;
    GENERIC *datum;
    CONST unsigned char *key;
{
    int i;

    if (!*root) {
        *root = (TrieNode *) XMALLOC(sizeof(TrieNode));
        if (*key) {
            (*root)->children = 1;
            (*root)->u.child = (TrieNode**)XMALLOC(0x100 * sizeof(TrieNode *));
            for (i = 0; i < 0x100; i++) (*root)->u.child[i] = NULL;
            return intrie(&(*root)->u.child[*key], datum, key + 1);
        } else {
            (*root)->children = 0;
            (*root)->u.datum = datum;
            return 1;
        }
    } else {
        if (*key) {
            if ((*root)->children) {
                if (!(*root)->u.child[*key]) (*root)->children++;
                return intrie(&(*root)->u.child[*key], datum, key+1);
            } else {
                return TRIE_SUPER;
            }
        } else {
            return ((*root)->children) ? TRIE_SUB : TRIE_DUP;
        }
    }
}

TrieNode *untrie(root, s)
    TrieNode **root;
    CONST unsigned char *s;
{
    if (*s) {
        if (untrie(&((*root)->u.child[*s]), s + 1)) return *root;
        if (--(*root)->children) return *root;
        FREE((*root)->u.child);
    }
    FREE(*root);
    return *root = NULL;
}


/***************/
/* linked list */
/***************/

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
    pfree(node, nodepool, next);
    return result;
}

/* Create new node for datum and insert into list in sorted order.
 * <cmp> is a function that compares two data.
 */
ListEntry *sinsert(datum, list, cmp)
    GENERIC *datum;
    List *list;
    Cmp *cmp;
{
    ListEntry *node;

    node = list->head;
    while (node && (*cmp)(datum, node->datum) > 0) {
        node = node->next;
    }
    return inlist(datum, list, node ? node->prev : list->tail);
}

/* Create new node for datum and insert into list.
 * If where is non-null, insert after it; else, insert at beginning
 */
ListEntry *inlist_fl(datum, list, where, file, line)
    GENERIC *datum;
    List *list;
    ListEntry *where;
    CONST char *file;
    int line;
{
    ListEntry *node;

    palloc(node, ListEntry, nodepool, next, file, line);
    node->datum = datum;
    if (where) {
        node->next = where->next;
        where->next = node;
    } else {
        node->next = list->head;
        list->head = node;
    }
    node->prev = where;
    *(node->next ? &node->next->prev : &list->tail) = node;
    return node;
}


/**************/
/* hash table */
/**************/

void init_hashtable(table, size, cmp)
    HashTable *table;
    int size;
    Cmp *cmp;
{
    table->size = size;
    table->cmp = cmp;
    table->bucket = (List **)XMALLOC(sizeof(List *) * size);
    while (size)
        table->bucket[--size] = NULL;
}

GENERIC *hash_find(name, table)       /* find entry by name */
    CONST char *name;
    HashTable *table;
{
    List *bucket;
    ListEntry *node;

    bucket = table->bucket[hash_string(name) % table->size];
    if (bucket) {
        for (node = bucket->head; node; node = node->next)
            if ((*table->cmp)((GENERIC *)name, node->datum) == 0)
                return node->datum;
    }
    return NULL;
}

static unsigned int hash_string(str)
    CONST char *str;
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
        table->bucket[indx] = (List *)XMALLOC(sizeof(List));
        init_list(table->bucket[indx]);
    }
    return inlist(datum, table->bucket[indx], NULL);
}


void hash_remove(node, tab)         /* remove node from hash table */
    ListEntry *node;
    HashTable *tab;
{
    unlist(node, tab->bucket[hash_string(*(char**)node->datum) % tab->size]);
}


/*****************/
/* binary search */
/*****************/

#ifndef HAVE_bsearch
/*
 * binsearch - replacement for bsearch().
 */
GENERIC *binsearch(key, base, nel, size, cmp)
    CONST GENERIC *key, *base;
    int nel, size;
    Cmp *cmp;
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


/***************/
/* comparisons */
/***************/

/* strstructcmp - compares a string to the first field of a structure */
int strstructcmp(key, datum)
    CONST GENERIC *key, *datum;
{
    return strcmp((char *)key, *(char **)datum);
}

/* cstrstructcmp - compares string to first field of a struct, ignoring case */
int cstrstructcmp(key, datum)
    CONST GENERIC *key, *datum;
{
    return cstrcmp((char *)key, *(char **)datum);
}

#ifdef DMALLOC
void free_search()
{
    ListEntry *node;
    while (nodepool) {
       node = nodepool;
       nodepool = nodepool->next;
       FREE(node);
    }
}

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
