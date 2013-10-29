/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */

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
#include <time.h>
#include <assert.h>



#include "myhmem.h"

#include "items.h"

#include "assoc.h"
#include "slabs.h"
#include "vaddr.h"



///////////////    global vars
slabs_t*  sb_hbmem;    // slab allocator for the hybrid-mem
slabs_t*  sb_htable; // slab allocator for the item-hash-table
struct settings settings;


//////////////////////

/* Forward Declarations */
static void item_link_q(item *it);
//static void item_unlink_q(item *it);
void item_unlink_q(item *it);

/*
 * We only reposition items in the LRU queue if they haven't been repositioned
 * in this many seconds. That saves us from churning on frequently-accessed
 * items.
 */
#define ITEM_UPDATE_INTERVAL 60

//#define LARGEST_ID POWER_LARGEST
typedef struct {
    unsigned int evicted;
    unsigned int evicted_nonzero;
    rel_time_t evicted_time;
    unsigned int reclaimed;
    unsigned int outofmemory;
    unsigned int tailrepairs;
} itemstats_t;

// LRU list for the value's slab-allocators
// static item *heads[LARGEST_ID];
// static item *tails[LARGEST_ID];
// static unsigned int sizes[LARGEST_ID]; 
item *heads[LARGEST_ID];
item *tails[LARGEST_ID];
unsigned int sizes[LARGEST_ID]; // num of items in the value's slab-allocator 

static itemstats_t itemstats[LARGEST_ID];



void item_stats_reset(void) {
    pthread_mutex_lock(&cache_lock);
    memset(itemstats, 0, sizeof(itemstats));
    pthread_mutex_unlock(&cache_lock);
}

//pthread_mutex_init(&cache_lock, NULL);

/* Get the next CAS id for a new item. */
uint64_t get_cas_id(void) {
    static uint64_t cas_id = 0;
    return ++cas_id;
}

/* Enable this for reference-count debugging. */
#if 0
# define DEBUG_REFCNT(it,op) \
                fprintf(stderr, "item %x refcnt(%c) %d %c%c%c\n", \
                        it, op, it->refcount, \
                        (it->it_flags & ITEM_LINKED) ? 'L' : ' ', \
                        (it->it_flags & ITEM_SLABBED) ? 'S' : ' ')
#else
# define DEBUG_REFCNT(it,op) while(0)
#endif


/**
 * Generates the variable-sized part of the header for an object.
 *
 * key     - The key
 * nkey    - The length of the key
 * flags   - key flags
 * nbytes  - Number of bytes to hold value and addition CRLF terminator
 * suffix  - Buffer for the "VALUE" line suffix (flags, size).
 * nsuffix - The length of the suffix is stored here.
 *
 * Returns the total size of the header.
 */
static size_t item_make_header(const uint8_t nkey, const int flags, const int nbytes,
                     char *suffix, uint8_t *nsuffix) {
    /* suffix is defined at 40 chars elsewhere.. */
    *nsuffix = (uint8_t) snprintf(suffix, 40, " %d %d\r\n", flags, nbytes - 2);

    return sizeof(item) + nkey + *nsuffix + nbytes;
}


/*
In virt-addr-hybrid-mem, key is virtual-address embedded in "item". 
(data) field is embedded at end of an item.
*/
static size_t my_item_make_header(const uint8_t nkey, 
    const int flags, const int nbytes,char *suffix, uint8_t *nsuffix) 
{
    //suffix is defined at 40 chars elsewhere..
    //*nsuffix = (uint8_t) snprintf(suffix, 40, " %d %d\r\n", flags, nbytes - 2);
    //return sizeof(item) + nkey + *nsuffix;
    return sizeof(item) + nbytes;
} 


void    dump_item(item* it)
{
    if(!it) return;
    
    printf("item: key=\"%p\"(%ld), refcount=%d, flag=%d, nbytes=%ld, "
        "val-slabcls=%d, is-dirty=%d\n", 
        ITEM_key(it), sizeof(void*), it->refcount, it->it_flags, 
        it->nbytes, it->slabs_clsid, it->is_dirty  );

}


/*
dump the LRU list items 
*/
void    dump_LRU_list(int id)
{
    item* it = heads[id];
    int numits = sizes[id];
    
    printf("\n******************\n"
        "will dump LRU list for slabclass-%d: %d items\n", id, numits);
    for(; it!=NULL; it=it->next)
    {
        dump_item(it);    
    }
    printf("******************\n\n");
}

/*
nkey: actual length of key, not including the ending \0
nbytes: the actual-length of value + 2 ("\r\n")
*/
item *do_item_alloc(char *key, const size_t nkey, const int flags, const rel_time_t exptime, const int nbytes, void** retaddr) 
{
    uint8_t nsuffix;
    char suffix[40];
    //size_t ntotal = item_make_header(nkey + 1, flags, nbytes, suffix, &nsuffix);
    //size_t  itemhdr_size = my_item_make_header(nkey + 1, flags, nbytes, suffix, &nsuffix);
    size_t  ntotal = my_item_make_header(nkey, flags, nbytes, suffix, &nsuffix);
    /*if (settings.use_cas) {
        ntotal += sizeof(uint64_t);
        //itemhdr_size += sizeof(uint64_t);
    } */

    dbg("key=%p(%ld), header-size=%ld, val-size=%d\n", 
        key, nkey, sizeof(item), nbytes);

    item *it = NULL;
    
    // For "value",  "id" is the slabclass id in the hybrid-mem slab-allocator
    unsigned int val_slabid = slabs_clsid(ntotal, sb_hbmem); // ntotal);
    if (val_slabid == 0)
        return 0;

    /// do a quick check if we have any expired items in the tail.. 
    int tries = 0;
    item *search;

    ////////////////////////////////////
    /////// From LRU list for this slabclass, find an expired item-value mem
    /*
    tries = 0;
    for (search = tails[val_slabid]; tries > 0 && search != NULL; tries--, search=search->prev) 
    {
        if ( search->refcount == 0 &&
            (search->exptime != 0 && search->exptime < current_time)) 
        {
            it = search;
           // I don't want to actually free the object, just steal
           // the item to avoid to grab the slab mutex twice ;-)
           ///
           // stats.reclaimed++;
            //STATS_UNLOCK();
            itemstats[val_slabid].reclaimed++;
            it->refcount = 1;
            do_item_unlink(it);
            /// Initialize the item block:
            it->slabs_clsid = 0;
            it->refcount = 0;
            break;
        }
    }
    */

    if( it ){  // has reclaimed an expired item
        dbg("From slab-%d: Reclaimed an expired item ...\n", val_slabid );
    }
    else { // alloc from slab 
        //pthread_mutex_lock(&cache_lock);
        it = slabs_alloc(ntotal, val_slabid, sb_hbmem);
        //pthread_mutex_unlock(&cache_lock);
        dbg(" alloc item, get %p\n", it);
        if( !it ){
            //evict_slabclass( val_slabid, sb_hbmem );            
            err(" failed to slab-alloc...\n");
        }
    }
    
    if( !it ){
       err("  unable to alloc...%d\n", 2);
       return NULL;
    }
    
    ///////////
    assert(it->slabs_clsid == 0);

    it->slabs_clsid = val_slabid; // use the slab-class for the "value" part

    assert(it!= heads[val_slabid]);

    it->next = it->prev = it->h_next = 0;
    it->refcount = 0; //no ref at alloc. User will sig-fault to acquire a ref to it
    it->is_dirty = 0; // empty when alloc
    it->it_flags = 0;

    ///it->nkey = nkey;  // key is an (void*), so nkey is fixed = 8
    it->nbytes = nbytes;

    //it->address = key;
    ITEM_key(it) = key;

    *retaddr = it;

    return it;  
    
}

void item_free(item *it) 
{
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    size_t ntotal = ITEM_ntotal(it);
    unsigned int clsid;
    assert((it->it_flags & ITEM_LINKED) == 0);
    assert(it != heads[it->slabs_clsid]);
    assert(it != tails[it->slabs_clsid]);
    assert(it->refcount == 0);

    clsid = it->slabs_clsid;
    it->slabs_clsid = 0;
    it->it_flags |= ITEM_SLABBED;
    DEBUG_REFCNT(it, 'F');

    slabs_free(it, ntotal, clsid, sb_hbmem); // return this item to slab

    //slabs_free(it, ntotal, clsid); // return this item to slab

    ///1.  release the data-mem
    //if( it->data )
    //    myslabs_free( it->data, it->nbytes, sb_hbmem );

    ///2.  release the item-header 
    //myslabs_free( it, ITEM_hdrsize(it), sb_htable ); 
}

/**
 * Returns true if an item will fit in the cache (its size does not exceed
 * the maximum for a cache entry.)
 */
bool item_size_ok(const size_t nkey, const int flags, const int nbytes) 
{
    char prefix[40];
    uint8_t nsuffix;
    int hdrsize = my_item_make_header(nkey+1, flags, nbytes, prefix, &nsuffix);

    if( slabs_clsid( hdrsize, sb_htable )!= 0 ||
        slabs_clsid( nbytes, sb_hbmem ) != 0 )
        return false;
    else
        return true;
}


/*
Include an item into the LRU list for "value" slab-class
*/
static void item_link_q(item *it) 
{ 
    ///  item will become the new head in LUR list
    item **head, **tail;
    assert(it->slabs_clsid < LARGEST_ID);
    assert((it->it_flags & ITEM_SLABBED) == 0);
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    head = &heads[it->slabs_clsid];
    tail = &tails[it->slabs_clsid];
    assert(it != *head);
    assert((*head && *tail) || (*head == 0 && *tail == 0));
    it->prev = 0;
    it->next = *head;
    if (it->next) it->next->prev = it;
    *head = it;
    if (*tail == 0) *tail = it;
    sizes[it->slabs_clsid]++;
    return;
}

/*
Rm an item from the LRU list in its slab-class.
*/
//static void item_unlink_q(item *it) 
void item_unlink_q(item *it) 
{
    item **head, **tail;
    assert(it->slabs_clsid < LARGEST_ID);
    head = &heads[it->slabs_clsid];
    tail = &tails[it->slabs_clsid];
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    if (*head == it) {
        assert(it->prev == 0);
        *head = it->next;
    }
    if (*tail == it) {
        assert(it->next == 0);
        *tail = it->prev;
    }
    assert(it->next != it);
    assert(it->prev != it);

    if (it->next) it->next->prev = it->prev;
    if (it->prev) it->prev->next = it->next;
    sizes[it->slabs_clsid]--;
    
    /////// which item is the last in-mem item for this slabclass?
    int id = it->slabs_clsid;
    if( sb_hbmem->inmem_item[id] == it )
        sb_hbmem->inmem_item[id] = tails[id];
    //////////////////
    
    return;
}


int do_item_link(item *it) 
{
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    //MEMCACHED_ITEM_LINK(ITEM_key(it), it->nkey, it->nbytes);
    assert((it->it_flags & (ITEM_LINKED|ITEM_SLABBED)) == 0);
    it->it_flags |= ITEM_LINKED;

    //it->time = current_time;

    assoc_insert(it); // put into hash table

    item_link_q(it); // link into LRU list

    return 1;
}


/*
take the itme out of LRU list, 
take the itme away from hash table,
then release its mem back to slab.
*/
void do_item_unlink(item *it) 
{
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    if ((it->it_flags & ITEM_LINKED) != 0) 
    {
        it->it_flags &= ~ITEM_LINKED;

        assoc_delete(ITEM_key(it), sizeof(void*)); //it->nkey);

        item_unlink_q(it);

        if (it->refcount == 0) item_free(it);
        else{
            err("at unlink: key=%p, refcount=%d\n", ITEM_key(it), it->refcount);
            sleep(1000000);
        }
    }
}

/*
Dec the item's refcount by 1.  
If its refcount becomes 0 and not linked in LRU (! ITEM_LINKED), free the item memory
*/
void do_item_remove(item *it) 
{
    assert((it->it_flags & ITEM_SLABBED) == 0);
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    //if (it->refcount != 0) {
    if (it->refcount > 0) {
        it->refcount--;
    }

    /// usually won't come here
    if (it->refcount == 0 && (it->it_flags & ITEM_LINKED) == 0) {
        item_free(it);
    }
}

/*
Relocate the item to the head of LRU list
*/
void do_item_update(item *it) 
{
    ////////////////
    dbg("key=%p, val=%s, refcount=%d, flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    assert((it->it_flags & ITEM_SLABBED) == 0);

    if ((it->it_flags & ITEM_LINKED) != 0) {
        item_unlink_q(it);
        item_link_q(it);
    }
}


int do_item_replace(item *it, item *new_it) 
{
    ////////////////
    dbg("key=%p, old-val=%s, old-refcount=%d, old-flags=0x%x\n", 
            ITEM_key(it), ITEM_data(it),  it->refcount, it->it_flags );
    ////////////////

    assert((it->it_flags & ITEM_SLABBED) == 0);

    do_item_unlink(it);
    return do_item_link(new_it);
}


/*@null@*/
char *do_item_cachedump(const unsigned int slabs_clsid, const unsigned int limit, unsigned int *bytes) 
{
    unsigned int memlimit = 2 * 1024 * 1024;   /* 2MB max response size */
    char *buffer;
    unsigned int bufcurr;
    item *it;
    unsigned int len;
    unsigned int shown = 0;
    char key_temp[KEY_MAX_LENGTH + 1];
    char temp[512];

    it = heads[slabs_clsid];
#if 0
    buffer = malloc((size_t)memlimit);
    if (buffer == 0) return NULL;
    bufcurr = 0;

    while (it != NULL && (limit == 0 || shown < limit)) {
        assert(it->nkey <= KEY_MAX_LENGTH);
        /* Copy the key since it may not be null-terminated in the struct */
        strncpy(key_temp, ITEM_key(it), it->nkey);
        key_temp[it->nkey] = 0x00; /* terminate */
        len = snprintf(temp, sizeof(temp), "ITEM %s [%d b; %lu s]\r\n",
                       key_temp, it->nbytes - 2,
                       (unsigned long)it->exptime + process_started);
        if (bufcurr + len + 6 > memlimit)  /* 6 is END\r\n\0 */
            break;
        memcpy(buffer + bufcurr, temp, len);
        bufcurr += len;
        shown++;
        it = it->next;
    }

    memcpy(buffer + bufcurr, "END\r\n", 6);
    bufcurr += 5;

    *bytes = bufcurr;
#endif
    return buffer;

}


size_t total_load_cnt = 0;  // cumulative count of loads from ssd
size_t total_load_size = 0; // cumu size loaded.

void    show_load_stats()
{
    printf("\n====== Loads:  load items=%ld, size=%ld\n\n",
       total_load_cnt, total_load_size );
    
    total_load_cnt=0;
    total_load_size=0;

}

/*
An item has been evicted to SSD. Reload this item into slabcache.
1. alloc an item at the corresponding slab class
2. Read data from the given ssd-position.
*/
//static long  load_evicted_item(item* it)
static item* load_evicted_item(int objsize, void* key, vaddr_range_t *vr, unsigned long ssdpos )
{
    item* it = NULL;
    
    int clsid = 0;
    int cnt = 0;
 
    slabclass_t* p = NULL;
    
    int datasize = objsize + sizeof(item);
    
    clsid = slabs_clsid( datasize, sb_hbmem );
    p = &sb_hbmem->slabclass[clsid];

    dbg("read from file: %s @ %ld\n", p->hb_filename, ssdpos);    

    /////////////////////////
    //pthread_mutex_lock(&cache_lock);
    //// alloc a slab slot
    it = slabs_alloc(datasize, clsid, sb_hbmem);
    if( !it ){
        err("fail to alloc mem for %p: total=%d(obj=%d), at slab-%d\n", 
            key, datasize, objsize, clsid );
        return NULL;
    }
    //pthread_mutex_unlock(&cache_lock);
    ////////////////////////////

    unsigned char *rdbuf = NULL;
    //size_t rdbuf_size = (objsize + 4096);  // leave some extra space for item* hdr
    size_t rdbuf_size = (objsize & ~(4095)) + 4096 + 4096;  // leave some extra space for item* hdr
    posix_memalign( (void**)&rdbuf, 4096, rdbuf_size );
    if( !rdbuf ){
        err("fail to alloc buf: size = %ld\n", rdbuf_size);
        return NULL;
    }
    
    /////////////////////
    pthread_rwlock_rdlock( &p->rwlock ); //// will read objs from ssd: put read lock
 
    unsigned long start = 0, end=0;
    unsigned long ltmp = 0;
      
    int start_margin = 0;
    int end_margin = 0;
    
    /// compute [start, end) range on file
    start = ssdpos; 
    if( start%512 ){
        start_margin = start%512;
        start -= (start%512);
    }
    
    //end = it->pos_in_ssd + sizeof(item*) + sizeof(long) + it->nbytes;
    end = ssdpos + sizeof(long) + datasize;
    if( end % 512 ){
        end_margin = 512 - end%512;
        end += (512 - end%512);
    }
   
    unsigned long itemsize;
 
    unsigned long toread  = end - start; // it->nbytes + sizeof(item*) + sizeof(long);
    unsigned long tocp = datasize; // + sizeof(long);
    
    unsigned long hasread = 0;
    unsigned long hascp = 0;
    
    unsigned long willread = 0;
    unsigned long willcp = 0;    

    total_load_cnt++;
    total_load_size += toread;
    
     
    cnt = 0;
    while( toread>0 && tocp>0 )
    {
        //willread = toread < p->cbuf_size ? toread : p->cbuf_size;
        willread = toread < rdbuf_size ? toread : rdbuf_size;
        if( willread % 512 ){ // this shall never happen!!
            err(": Error!!  read size =%ld, offset=%ld not aligned!!!\n",
                willread, hasread+start );
            //willread += (512 - willread%512);
            break;
        }
            
        //ltmp = pread( p->hb_fd, p->cbuf, willread, start + hasread );
        ltmp = pread( p->hb_fd, rdbuf, willread, start + hasread );
        if( ltmp != willread ){
            err("File=%s:: Want read %ld, ret %ld, errno=%d, pos=%ld\n", 
                p->hb_filename,  willread, ltmp, errno, start+hasread );
            perror("read file err:: \n");
            sleep(1000000);
            break;            
        }
        
        toread -= ltmp;
        hasread += ltmp;
        
        if( cnt== 0 ){
            //memcpy(&itemsize, p->cbuf+start_margin, sizeof(long) );
            memcpy(&itemsize, rdbuf+start_margin, sizeof(long) );
            tocp = itemsize - sizeof(long);
            willcp = tocp < ltmp ? tocp : ltmp;
            
            //memcpy(it, p->cbuf+start_margin+sizeof(long), willcp); 
            memcpy(it, rdbuf+start_margin+sizeof(long), willcp); 
            if( it->vaddr_range != vr || it->address != key )
            {
                err("Load item from %ld: start-margin=%d, want key=%p, get-key=%p\n",
                     start, start_margin, key, it->address );
                sleep(1000000);
                break;
            }
            
        } else {
            willcp = tocp < ltmp? tocp : ltmp;
            //memcpy( ITEM_data(it) + hascp, p->cbuf, willcp );
            memcpy( ITEM_data(it) + hascp, rdbuf, willcp );
        }

        tocp -= willcp; // sizeof(item*)-sizeof(long);
        hascp += willcp;
                
        cnt++;    
    }
    it->is_dirty = 0;

    pthread_rwlock_unlock( &p->rwlock );/// release read-lock

    free(rdbuf);

    dbg("load item:(%p): nbytes=%d, is-dirty=%d, offset=%ld, read-file=%ld [%ld-%ld), cp=%ld\n", 
        ITEM_key(it), it->nbytes, it->is_dirty, ssdpos, hasread, start, end, hascp );

    return it;
}



/** wrapper around assoc_find which does the lazy expiration logic */
item *do_item_get(const char *key, const size_t nkey) 
{
    item *it = assoc_find(key, nkey);
    int was_found = 0;

    ////////////////
    dbg("key=%p, found it=%p\n", key, (void*)it );


    if (settings.verbose > 2) {
        if (it == NULL) {
            fprintf(stderr, "> NOT FOUND %s", key);
        } else {
            fprintf(stderr, "> FOUND KEY %s", ITEM_key(it));
            was_found++;
        }
    }
/*
    if (it != NULL && settings.oldest_live != 0 && settings.oldest_live <= current_time &&
        it->time <= settings.oldest_live) {
        do_item_unlink(it);          // MTSAFE - cache_lock held
        it = NULL;
    }

    if (it == NULL && was_found) {
        fprintf(stderr, " -nuked by flush");
        was_found--;
    }

    if (it != NULL && it->exptime != 0 && it->exptime <= current_time) {
        do_item_unlink(it);         ////MTSAFE - cache_lock held
        it = NULL;
    }

    if (it == NULL && was_found) {
        fprintf(stderr, " -nuked by expire");
        was_found--;
    }
*/
    if (it != NULL) {
        it->refcount++;
        DEBUG_REFCNT(it, '+');
    }

    if (settings.verbose > 2)
        fprintf(stderr, "\n");
        
    ////////////////
    dbg("key=%s, return =%p\n", key, (void*)it );
    //if( it && it->data==NULL ) // this item has been evicted, alloc mem and load data from SSD
    {
        //load_evicted_item(it);
    }
    ////////////////

    return it;
}

/** returns an item whether or not it's expired. */
item *do_item_get_nocheck(const char *key, const size_t nkey) 
{
    ////////////////
    dbg("key=%s\n", key );
    ////////////////
    item *it = assoc_find(key, nkey);
    if (it) {
        it->refcount++;
        DEBUG_REFCNT(it, '+');
    }
    return it;
}

/* expires items that are more recent than the oldest_live setting. */
void do_item_flush_expired(void) 
{
    ////////////////
    dbg(" enter %d\n", 0 );
    ////////////////
    int i;
    item *iter, *next;
    if (settings.oldest_live == 0)
        return;
    for (i = 0; i < LARGEST_ID; i++) {
        /* The LRU is sorted in decreasing time order, and an item's timestamp
         * is never newer than its last access time, so we only need to walk
         * back until we hit an item older than the oldest_live time.
         * The oldest_live checking will auto-expire the remaining items.
         */
        /*
        for (iter = heads[i]; iter != NULL; iter = next) {
            if (iter->time >= settings.oldest_live) {
                next = iter->next;
                if ((iter->it_flags & ITEM_SLABBED) == 0) {
                    do_item_unlink(iter);
                }
            } else {
               /// We've hit the first old item. Continue to the next queue.
                break;
            }
        }
        */
    }
}



/*
Hybrid-mem:  alloc a item slot from slab cache.
*/
item* hb_item_alloc(void* key, int keysize, int datasize)
{
    item *it = NULL;


    do_item_alloc(key, keysize, 0, 0, datasize, (void**)&it);
    if( it )
    {
        item *newit;
        pthread_mutex_lock(&cache_lock);
        newit = assoc_find( key, sizeof(void*) );
        if( newit ){
            dbg(" key=%p: already loaded by others...\n", key);
            it->it_flags = ITEM_SLABBED;
            slabs_free(it, datasize, it->slabs_clsid, sb_hbmem );
            it = newit;
        } else {
            it->refcount = 1;
            do_item_link(it); // link this item to LRU and hash table
        }
        pthread_mutex_unlock(&cache_lock);
    }

    return it;

}

/*
Release the item identified by (key) from the slab-cache.
*/
int hb_item_delete_by_key(void *key, int keysize)
{
    item *it;

    pthread_mutex_lock(&cache_lock);

    it = assoc_find(key, keysize); 
    if( it ){
        do_item_remove(it); // decrease the refcount
        do_item_unlink(it); // release the item
        pthread_mutex_unlock(&cache_lock);
        dbg("found and del slab-item: key=%p\n", key);
        return 0;
    }
    pthread_mutex_unlock(&cache_lock);

    dbg("Not Exist!!:: slab-item key=%p\n", key);
    return -1;
}

/*
Decrease item reference by 1.
*/
void hb_item_remove(item* it)
{
    pthread_mutex_lock(&cache_lock);
    do_item_remove(it);
    pthread_mutex_unlock(&cache_lock);
}
/*
Given virt-addr (key) which was created from a pool-alloc, 
search it from slab cache. 
If not present in cache, search the vaddr-range (vr).

if upflag==1, relocate this item to head of LRU.
*/
item *hb_item_get(void* key, vaddr_range_t *vr, slabclass_t* slab, void** retaddr, int upflag)
{
    void *p = (void*)((unsigned long)key & ~(PGSIZE-1)); 
    assert(p==key);

    item *it;
    
    pthread_rwlock_rdlock( &slab->rwlock ); // put a lock here, so 
    // this item won't be evicted while I'm searching for it. 
    // after finding it, inc its refcount, so that it won't be evicted.
    pthread_mutex_lock(&cache_lock);

    it = assoc_find(p, sizeof(void*));

    ////////////////
    dbg("key=%p, found it=%p\n", key, (void*)it );

    if( it ){ // present in slab cache
        it->refcount=1; 

        if( upflag )
            do_item_update(it);

        *retaddr = it; 

        pthread_mutex_unlock(&cache_lock);
        pthread_rwlock_unlock( &slab->rwlock ); 
        return it;
    } 
 
    //pthread_mutex_unlock(&cache_lock);
    //pthread_rwlock_unlock( &slab->rwlock ); 

    //// the item might have been evicted.
    //unsigned long off = get_pool_obj_offset(vr, key);
    int state;
    int datasize;
    int alloctype = vr->type;
    size_t ssdpos;

    //pthread_rwlock_rdlock( &slab->rwlock );
    pthread_mutex_lock( &vr->lock); 
    if( alloctype == allocator_pool ){    
        state = get_pool_obj_state(vr, key); 
        datasize = vr->pool.obj_size;  // data size of the obj
        get_pool_obj_ssd_pos(vr, key, &ssdpos); 
    }
    else{
        state = get_coalesce_obj_state(vr, key);
        datasize = vr->coalesce.obj_size;
        get_coalesce_obj_ssd_pos(vr, key, &ssdpos);
    }
    pthread_mutex_unlock( &vr->lock); 

    pthread_mutex_unlock(&cache_lock);
    pthread_rwlock_unlock( &slab->rwlock );

    dbg("key=%p:  state='%s': no it in slab\n", 
            key,  state2string(state));
    //sleep(1000000);
    switch(state)
    {
        case STATE_FREE:
            err("the required key=%p has been freed...\n", key);
            sleep(1000000);
            return NULL;

        case STATE_ALLOC_EMPTY:
            dbg("the key=%p is alloc-empty, re-instantiate it...\n", key );
            it = hb_item_alloc(key, sizeof(key), datasize);
            it->vaddr_range = vr;
            //it->refcount++;
            //dump_item(it);
            break;

            //// may have been evicted into SSD...
        case STATE_ALLOC_DIRTY: // load from ssd
        case STATE_ALLOC_CLEAN:
            dbg("the key=%p non-empty, has been evicted to ssd. re-instantiate it...\n", key );

            it = load_evicted_item(datasize, key, vr, ssdpos);
            assert(it!=NULL);
            it->it_flags = 0;

            ///////////////
            item * newit;
            pthread_mutex_lock(&cache_lock);
            newit = assoc_find( key, sizeof(void*) );
            if( newit ){
                // someone else has inserted it. No need to insert again
                fprintf("%s: key=%p: already loaded by others...\n", __func__,key );
                it->it_flags = ITEM_SLABBED;
                slabs_free(it, datasize, it->slabs_clsid, sb_hbmem );
                //do_slabs_free(it, datasize, it->slabs_clsid, sb_hbmem );
                it = newit;
            } else {
                it->refcount=1;
                it->vaddr_range = vr;
                do_item_link(it);  // put into front of LRU list
            }
            pthread_mutex_unlock(&cache_lock);
            /////////////
            //dump_item(it);
            
            break;
    }

    *retaddr = it;
    return  it;
}

