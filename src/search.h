/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993 - 1999 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.h,v 35004.10 1999/01/31 00:27:51 hawkeye Exp $ */

#ifndef SEARCH_H
#define SEARCH_H

/*************************************************************
 * trie, hash table, linked list, and binary search routines *
 *************************************************************/

/*
 * Trie.
 * Any type can be used in a trie.
 */
/*
 * Linked List.
 * Any type can be used in a linked list.
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

#define TRIE_SUB	(-1)
#define TRIE_SUPER	(-2)
#define TRIE_DUP	(-3)

typedef struct TrieNode {
    int children;
    union {
        struct TrieNode **child;
        GENERIC *datum;
    } u;
} TrieNode;

typedef int FDECL(Cmp,(CONST GENERIC *, CONST GENERIC *)); /* generic compare */

typedef struct ListEntry {
    struct ListEntry *next, *prev;
    GENERIC *datum;
} ListEntry;

typedef struct List {
    ListEntry *head, *tail;
} List;

typedef struct HashTable {
    int size;
    Cmp *cmp;
    List **bucket;
} HashTable;

#define init_queue(Q)     init_list(Q)
#define dequeue(Q)        ((Aline *)((Q)->tail ? unlist((Q)->tail, (Q)) : NULL))
#define enqueue(Q, aline) inlist((GENERIC *)(aline), (Q), NULL)

#define inlist(datum, list, where) \
                          inlist_fl((datum), (list), (where), __FILE__,__LINE__)

extern void      FDECL(init_list,(List *list));
extern GENERIC  *FDECL(unlist,(ListEntry *node, List *list));
extern ListEntry*FDECL(inlist_fl,(GENERIC *datum, List *list, ListEntry *where,
                     CONST char *file, int line));
extern ListEntry*FDECL(sinsert,(GENERIC *datum, List *list, Cmp *cmp));
extern void      FDECL(hash_remove,(ListEntry *node, HashTable *table));
extern ListEntry*FDECL(hash_insert,(GENERIC *datum, HashTable *table));
extern GENERIC  *FDECL(hash_find,(CONST char *name, HashTable *table));
extern void      FDECL(init_hashtable,(HashTable *table, int size, Cmp *cmp));

extern int      FDECL(strstructcmp,(CONST GENERIC *key, CONST GENERIC *datum));
extern int      FDECL(cstrstructcmp,(CONST GENERIC *key, CONST GENERIC *datum));

extern int       FDECL(intrie,(TrieNode **root, GENERIC *datum,
                     CONST unsigned char *key));
extern TrieNode *FDECL(untrie,(TrieNode **root, CONST unsigned char *s));
extern GENERIC  *FDECL(trie_find,(TrieNode *root, CONST unsigned char *key));

#ifdef HAVE_bsearch
# define binsearch bsearch
#else
extern GENERIC   *FDECL(binsearch,(CONST GENERIC *key, CONST GENERIC *base,
                      int nel, int size,
                      int FDECL((*cmp),(CONST GENERIC *, CONST GENERIC *))));
#endif

#ifdef DMALLOC
extern void   NDECL(free_search);
extern void   FDECL(free_hash,(HashTable *table));
#endif

#endif /* SEARCH_H */

