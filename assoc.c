/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Hash table
 *
 * The hash function used here is by Bob Jenkins, 1996:
 *    <http://burtleburtle.net/bob/hash/doobs.html>
 *       "By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.
 *       You may use this code any way you wish, private, educational,
 *       or commercial.  It's free."
 *
 * The rest of the file is licensed under the BSD license.  See LICENSE.
 */

//#include "memcached.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

#include "myhmem.h"
#include "assoc.h"

static pthread_cond_t maintenance_cond = PTHREAD_COND_INITIALIZER;


typedef  unsigned long int  ub4;   /* unsigned 4-byte quantities */
typedef  unsigned char ub1;   /* unsigned 1-byte quantities */

/* how many powers of 2's worth of buckets we use */
static unsigned int hashpower = 16;

#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n)-1)

/* Main hash table. This is where we look except during expansion. */
static item** primary_hashtable = 0;

/*
 * Previous hash table. During expansion, we look here for keys that haven't
 * been moved over to the primary yet.
 */
static item** old_hashtable = 0;

/* Number of items in the hash table. */
static unsigned int hash_items = 0;

/* Flag: Are we in the middle of expanding now? */
static bool expanding = false;

/*
 * During expansion we migrate values with bucket granularity; this is how
 * far we've gotten so far. Ranges from 0 .. hashsize(hashpower - 1) - 1.
 */
static unsigned int expand_bucket = 0;


pthread_mutex_t cache_lock;



/////////////////////// some stats
static unsigned long num_collision = 0;
static unsigned long num_lookups = 0;
static unsigned long max_buck_collision=0; // num of collosion in one bucket
static unsigned long min_buck_collision=0x7fff; // num of collosion in one bucket
static unsigned long num_hit = 0;  // perform lookup, and find a hit

static unsigned long hash_size = 0;  // size of hash table
static unsigned long num_items = 0;  // num of items in table

void    clear_hash_stats()
{
    num_lookups = 0;
    num_collision = 0;
    num_hit = 0;
    max_buck_collision = 0;
    min_buck_collision = 0x7fff;
}

void    dump_hash_stats()
{

    hash_size = hashsize(hashpower);
    num_items = hash_items;
    printf("\n==== HashTbl: size=%ld, %ld it, load-factor=%f, %ld lookups, %ld hits, %ld collision, max-buck-col=%ld, min-buck-col=%ld\n",
        hash_size, num_items, num_items*1.0/hash_size, num_lookups, num_hit, num_collision, max_buck_collision, min_buck_collision );

}

void assoc_init(int tbl_bits) 
{
    dbg("init hash table... %d\n", 1);
    
    pthread_mutex_init(&cache_lock, NULL);
    
    hashpower = tbl_bits; 
    primary_hashtable = calloc(hashsize(hashpower), sizeof(void *));
    if (! primary_hashtable) {
        fprintf(stderr, "Failed to init hashtable.\n");
        exit(EXIT_FAILURE);
    }
    
    start_assoc_maintenance_thread();
}



void  assoc_destroy(void)
{
    stop_assoc_maintenance_thread();
}

//item *assoc_find(const char *key, const size_t nkey) 
item *assoc_find(const void *key, const size_t nkey) 
{
    uint32_t hv = hash(&key, nkey, 0);

    item *it;
    unsigned int oldbucket;
    dbg("enter...key=%p, hv=%u\n", key, hv);    

    if (expanding &&
        (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
    {
        it = old_hashtable[oldbucket];
    } else {
        it = primary_hashtable[hv & hashmask(hashpower)];
    }

    num_lookups++;

    item *ret = NULL;
    int depth = 0;
    while (it) {
        //if ((nkey == it->nkey) && (memcmp(key, ITEM_key(it), nkey) == 0)) 
        if( key == ITEM_key(it) )
        {
            ret = it;
            num_hit++;
            break;
        }
        num_collision++;
        it = it->h_next;
        ++depth;
    }
    if( depth > max_buck_collision )
        max_buck_collision = depth;
    if( depth < min_buck_collision )
        min_buck_collision = depth;
    return ret;
}

/* returns the address of the item pointer before the key.  if *item == 0,
   the item wasn't found */

static item** _hashitem_before (const char *key, const size_t nkey) 
{
    uint32_t hv = hash(&key, nkey, 0);
    item **pos;
    unsigned int oldbucket;
    dbg("enter... key=%p, hv=%d\n", key, hv);

    if (expanding &&
        (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
    {
        pos = &old_hashtable[oldbucket];
    } else {
        pos = &primary_hashtable[hv & hashmask(hashpower)];
    }

    //while (*pos && ((nkey != (*pos)->nkey) || memcmp(key, ITEM_key(*pos), nkey))) {
    while ( *pos && key!=ITEM_key(*pos) ) 
    {
        pos = &(*pos)->h_next;
    }
    return pos;
}

/* grows the hashtable to the next power of 2. */
static void assoc_expand(void) 
{
    old_hashtable = primary_hashtable;

    dbg("enter ...  begin expand %d...\n", 1);
    primary_hashtable = calloc(hashsize(hashpower + 1), sizeof(void *));
    if (primary_hashtable) {
        //if (settings.verbose > 1)
        fprintf(stderr, "Hash table expansion starting, old=%d, new=%d\n",
            hashsize(hashpower), hashsize(hashpower+1) );
        hashpower++;
        expanding = true;
        expand_bucket = 0;
        pthread_cond_signal(&maintenance_cond);
    } else {
        dbg(" Warning:: calloc failed and unable to expand !! %d\n", 1);
        primary_hashtable = old_hashtable;
        /* Bad news, but we can keep running. */
    }
}

/* Note: this isn't an assoc_update.  The key must not already exist to call this */
int assoc_insert(item *it) 
{
    uint32_t hv;
    unsigned int oldbucket;

    dbg("enter... key=%p\n", ITEM_key(it) );
    assert(assoc_find(ITEM_key(it), sizeof(void*) ) == 0);  /* shouldn't have duplicately named things defined */

    //hv = hash(ITEM_key(it), sizeof(void*), 0);
    hv = hash(&ITEM_key(it), sizeof(void*), 0);
    if (expanding &&
        (oldbucket = (hv & hashmask(hashpower - 1))) >= expand_bucket)
    {
        it->h_next = old_hashtable[oldbucket];
        old_hashtable[oldbucket] = it;
    } else {
        it->h_next = primary_hashtable[hv & hashmask(hashpower)];
        primary_hashtable[hv & hashmask(hashpower)] = it;
    }

    hash_items++;
    if (! expanding && hash_items > (hashsize(hashpower) * 3) / 2) {
        assoc_expand();
    }

    return 1;
}

/*

*/
void assoc_delete(const void *key, const size_t nkey) 
{
    dbg("enter... key=%p\n", key );
    item **before = _hashitem_before(key, nkey);

    if (*before) {
        item *nxt;
        hash_items--;
        /* The DTrace probe cannot be triggered as the last instruction
         * due to possible tail-optimization by the compiler
         */
        nxt = (*before)->h_next;
        (*before)->h_next = 0;   /* probably pointless, but whatever. */
        *before = nxt;
        return;
    }
    /* Note:  we never actually get here.  the callers don't delete things
       they can't find. */
    assert(*before != 0);
}


static volatile int do_run_maintenance_thread = 1;

#define DEFAULT_HASH_BULK_MOVE 1
int hash_bulk_move = DEFAULT_HASH_BULK_MOVE;

static void *assoc_maintenance_thread(void *arg) 
{
    dbg(" enter...%d\n", 1);
    while (do_run_maintenance_thread) {
        int ii = 0;

        /* Lock the cache, and bulk move multiple buckets to the new
         * hash table. */
        pthread_mutex_lock(&cache_lock);

        for (ii = 0; ii < hash_bulk_move && expanding; ++ii) {
            item *it, *next;
            int bucket;

            for (it = old_hashtable[expand_bucket]; NULL != it; it = next) {
                next = it->h_next;

                //bucket = hash(ITEM_key(it), it->nkey, 0) & hashmask(hashpower);
                bucket = hash(&ITEM_key(it), sizeof(void*), 0) & hashmask(hashpower);
                it->h_next = primary_hashtable[bucket];
                primary_hashtable[bucket] = it;
            }

            old_hashtable[expand_bucket] = NULL;

            expand_bucket++;
            if (expand_bucket == hashsize(hashpower - 1)) {
                expanding = false;
                free(old_hashtable);
                //if (settings.verbose > 1)
                    fprintf(stderr, "Hash table expansion done\n");
            }
        }

        if (!expanding) {
            /* We are done expanding.. just wait for next invocation */
            pthread_cond_wait(&maintenance_cond, &cache_lock);
        }

        pthread_mutex_unlock(&cache_lock);
    }

    dbg(" exit now %d\n", 1);
    return NULL;
}

static pthread_t maintenance_tid;

int start_assoc_maintenance_thread() 
{
    int ret;
    char *env = getenv("MEMCACHED_HASH_BULK_MOVE");
    if (env != NULL) {
        hash_bulk_move = atoi(env);
        if (hash_bulk_move == 0) {
            hash_bulk_move = DEFAULT_HASH_BULK_MOVE;
        }
    }
    if ((ret = pthread_create(&maintenance_tid, NULL,
                              assoc_maintenance_thread, NULL)) != 0) {
        fprintf(stderr, "Can't create thread: %s\n", strerror(ret));
        return -1;
    }
    return 0;
}


void stop_assoc_maintenance_thread(void) 
{
    pthread_mutex_lock(&cache_lock);
    do_run_maintenance_thread = 0;
    pthread_cond_signal(&maintenance_cond);
    pthread_mutex_unlock(&cache_lock);

    /* Wait for the maintenance thread to stop */
    pthread_join(maintenance_tid, NULL);
}


