/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993  Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.h,v 32101.0 1993/12/20 07:10:00 hawkeye Stab $ */

#ifndef SEARCH_H
#define SEARCH_H

/**********************************************
 * hash table and linked list routines        *
 **********************************************/

typedef struct ListEntry {
    struct ListEntry *next, *prev;
    GENERIC *data;
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
#define enqueue(Q, aline) inlist((aline), (Q), NULL)

extern void       FDECL(init_list,(List *list));
extern GENERIC   *FDECL(unlist,(ListEntry *node, List *list));
extern ListEntry *FDECL(inlist,(GENERIC *data, List *list, ListEntry *where));
extern ListEntry *FDECL(sinsert,(GENERIC *data, List *list, Cmp *cmp));
extern void       FDECL(queuequeue,(Queue *src, Queue *dest));
extern void       FDECL(free_queue,(Queue *q));
extern void       FDECL(hash_remove,(ListEntry *node, HashTable *table));
extern ListEntry *FDECL(hash_insert,(GENERIC *data, HashTable *table));
extern GENERIC   *FDECL(hash_find,(char *name, HashTable *table));
extern void       FDECL(init_hashtable,(HashTable *table, int size,
                      int FDECL((*cmp),(CONST char *, CONST char *))));
extern int        FDECL(binsearch,(char *key, GENERIC *base, int nel, int size,
                      int FDECL((*cmp),(CONST char *, CONST char *))));

#endif /* SEARCH_H */

