/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.h,v 33000.0 1994/03/05 09:34:14 hawkeye Exp $ */

#ifndef SEARCH_H
#define SEARCH_H

/*******************************************************
 * hash table, linked list, and binary search routines *
 *******************************************************/

/*
 * Linked List.
 * Any structure can be used in a linked list.
 */
/*
 * Hash Table.
 * All structs used by hash table routines must have a string as the
 * first member.  This first string field is used as the hash key.
 */
/*
 * Binary Search.
 * binsearch() is just like ansi bsearch().  Generic comparison functions
 * gen[c]strcmp() are provided to perform [c]strcmp() on any structure
 * whose first field is a string.
 */

typedef struct ListEntry {
    struct ListEntry *next, *prev;
    GENERIC *datum;
} ListEntry;

typedef struct List {
    ListEntry *head, *tail;
} List;

typedef struct HashTable {
    int size;
    int FDECL((*cmp),(CONST char *, CONST char *));
    List **bucket;
} HashTable;

#define init_queue(Q)     init_list(Q)
#define dequeue(Q)        ((Aline *)((Q)->tail ? unlist((Q)->tail, (Q)) : NULL))
#define enqueue(Q, aline) inlist((GENERIC *)(aline), (Q), NULL)

extern void       FDECL(init_list,(List *list));
extern GENERIC   *FDECL(unlist,(ListEntry *node, List *list));
extern ListEntry *FDECL(inlist,(GENERIC *datum, List *list, ListEntry *where));
extern ListEntry *FDECL(sinsert,(GENERIC *datum, List *list, Cmp *cmp));
extern void       FDECL(free_queue,(Queue *q));
extern void       FDECL(hash_remove,(ListEntry *node, HashTable *table));
extern ListEntry *FDECL(hash_insert,(GENERIC *datum, HashTable *table));
extern GENERIC   *FDECL(hash_find,(char *name, HashTable *table));
extern void       FDECL(init_hashtable,(HashTable *table, int size,
                      int FDECL((*cmp),(CONST char *, CONST char *))));

extern int        FDECL(genstrcmp,(CONST GENERIC *key, CONST GENERIC *datum));
extern int        FDECL(gencstrcmp,(CONST GENERIC *key, CONST GENERIC *datum));

#ifdef HAVE_BSEARCH
# define binsearch(key, base, nel, size, cmp) bsearch(key, base, nel, size, cmp)
#else
extern GENERIC   *FDECL(binsearch,(CONST GENERIC *key, CONST GENERIC *base,
                      int nel, int size,
                      int FDECL((*cmp),(CONST GENERIC *, CONST GENERIC *))));
#endif

#endif /* SEARCH_H */

