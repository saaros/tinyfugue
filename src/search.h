/*************************************************************************
 *  TinyFugue - programmable mud client
 *  Copyright (C) 1993, 1994, 1995, 1996, 1997, 1998, 1999, 2002, 2003 Ken Keys
 *
 *  TinyFugue (aka "tf") is protected under the terms of the GNU
 *  General Public License.  See the file "COPYING" for details.
 ************************************************************************/
/* $Id: search.h,v 35004.21 2003/05/27 01:09:24 hawkeye Exp $ */

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
 * Circular Queue.
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

/* Modulo arithmetic: remainder is positive, even if numerator is negative. */
#define nmod(n, d)   (((n) >= 0) ? ((n)%(d)) : ((d) - ((-(n)-1)%(d)) - 1))
#define ndiv(n, d)   (((n) >= 0) ? ((n)/(d)) : (-((-(n)-1)/(d))-1))

#define TRIE_SUB	(-1)
#define TRIE_SUPER	(-2)
#define TRIE_DUP	(-3)

typedef struct TrieNode {
    int children;
    union {
        struct TrieNode **child;
        void *datum;
    } u;
} TrieNode;

typedef int Cmp(const void *, const void *); /* generic compare */

typedef struct ListEntry {
    struct ListEntry *next, *prev;
    void *datum;
} ListEntry;

typedef struct List {
    ListEntry *head, *tail;
} List;

typedef struct HashTable {
    int size;
    Cmp *cmp;
    List **bucket;
} HashTable;

typedef struct CQueue {		/* circular queue of data */
    void **data;		/* array of pointers to data */
    void (*free)(void*, const char*, int);	/* function to free a datum */
    int size;			/* actual number of data currently saved */
    int maxsize;		/* maximum number of data that can be saved */
    int first;			/* position of first datum in circular array */
    int last;			/* position of last datum in circular array */
    int index;			/* current position */
    int total;			/* total number of data ever saved */
} CQueue;

#define init_queue(Q)		(init_list(&(Q)->list))
#define dequeue(Q)		((String*)((Q)->list.tail ? \
				(unlist((Q)->list.tail, &(Q)->list)) : NULL))
#define enqueue(Q, line)	(inlist((void *)(line), &(Q)->list, NULL))

#define inlist(datum, list, where) \
			inlist_fl((datum), (list), (where), __FILE__, __LINE__)

#define hash_find(name, table)	hashed_find(name, hash_string(name), table)

extern void      init_list(List *list);
extern void  *unlist(ListEntry *node, List *list);
extern ListEntry*inlist_fl(void *datum, List *list, ListEntry *where,
                     const char *file, int line);
extern ListEntry*sinsert(void *datum, List *list, Cmp *cmp);
extern unsigned int hash_string(const char *str);
extern void      hash_remove(ListEntry *node, HashTable *table);
extern ListEntry*hash_insert(void *datum, HashTable *table);
extern void  *hashed_find(const char *name, unsigned int hash,
		    HashTable *table);
extern void      init_hashtable(HashTable *table, int size, Cmp *cmp);

extern int      strstructcmp(const void *key, const void *datum);
extern int      cstrstructcmp(const void *key, const void *datum);

extern int       intrie(TrieNode **root, void *datum,
                     const unsigned char *key);
extern TrieNode *untrie(TrieNode **root, const unsigned char *s);
extern void  *trie_find(TrieNode *root, const unsigned char *key);

struct CQueue *init_cqueue(CQueue *cq, int maxsize,
    void (*free_f)(void *, const char *, int));
void free_cqueue(CQueue *cq);
void encqueue(CQueue *cq, void *datum);
void cqueue_replace(CQueue *cq, void *datum, int idx);
int resize_cqueue(CQueue *cq, int maxsize);

#if HAVE_BSEARCH
# define binsearch bsearch
#else
extern void   *binsearch(const void *key, const void *base,
                      int nel, int size,
                      int (*cmp)(const void *, const void *)));
#endif

#if USE_DMALLOC
extern void   free_search(void);
extern void   free_hash(HashTable *table);
#endif

#endif /* SEARCH_H */
