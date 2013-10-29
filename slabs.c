/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Slabs memory allocation, based on powers-of-N. Slabs are up to 1MB in size
 * and are divided into chunks. The chunk sizes start off at the size of the
 * "item" structure plus space for a small key and value. They increase by
 * a multiplier factor from there, up to half the maximum slab size. The last
 * slab size is always 1MB, since that's the maximum item size allowed by the
 * memcached protocol.
 */
//#include "memcached.h"



#define _GNU_SOURCE


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


#include <sys/mman.h>
#include <unistd.h>


#include "vaddr.h"
#include "myhmem.h"
#include "slabs.h"


/*
 * Forward Declarations
 */
static int do_slabs_newslab(const unsigned int id, slabs_t* sb);
static void *memory_allocate(size_t size, slabs_t* sb);
static void do_slabs_free(void *ptr, const size_t size, unsigned int id, slabs_t* sb);

#ifndef DONT_PREALLOC_SLABS
/* Preallocate as many slab pages as possible (called from slabs_init)
   on start-up, so users don't get confused out-of-memory errors when
   they do have free (in-slab) space, but no space to make new slabs.
   if maxslabs is 18 (POWER_LARGEST - POWER_SMALLEST + 1), then all
   slab types can be made.  if max memory is less than 18 MB, only the
   smaller ones will be made.  */
static void slabs_preallocate (const unsigned int maxslabs, slabs_t* sb);
#endif

char HB_DIR[1024];

/*
 * Figures out which slab class (chunk size) is required to store an item of
 * a given size.
 *
 * Given object size, return id to use when allocating/freeing memory for object
 * 0 means error: can't store such a large object
 */
unsigned int slabs_clsid(const size_t size, slabs_t* sb)
{
    int res = POWER_SMALLEST;

    if (size == 0)
        return 0;
        
    while (size > sb->slabclass[res].size)
        if (res++ == sb->power_largest)     /* won't fit in the biggest slab */
            return 0;
    return res;
}


void    dump_slabclass(slabclass_t* st, int id)
{
    int i;
    
    // compute the used mem-items in this slabclass    
    i = st->slabs * st->perslab - st->end_page_free - st->sl_curr;
    
    printf("slabclass[%d]: item-size=%d, item-perslab=%d, live-mem-items=%d, "
        "slot_total=%d, slot_curr=%d, slabs=%d, end-pg=%p, end-pg-free=%d\n",
        id, st->size, st->perslab, i, st->sl_total, st->sl_curr, st->slabs, st->end_page_ptr, st->end_page_free );
}


void    dump_slabs(slabs_t* sb)
{
    int i;

    printf("\n=========================\n");
    if( sb->mem_base ) { // has pre-allocated mem::
        printf("slabs \"%s\": num-of-slab=%d, use-mmap=%d, pre-allocated=%ld, used-mem=%ld\n", 
        sb->name, sb->power_largest, sb->use_mmap, sb->mem_limit, sb->mem_malloced );
    }
    else{
        printf("slabs \"%s\": num-of-slab=%d, mem_limit=%ld, alloced-mem=%ld\n", 
            sb->name, sb->power_largest, sb->mem_limit, sb->mem_malloced );
    }
    
    for(i=POWER_SMALLEST; i<=sb->power_largest; i++)
    {
        if( sb->slabclass[i].sl_total || sb->slabclass[i].slabs )
            dump_slabclass(&sb->slabclass[i], i);
    }
    printf("=========================\n\n");

}


size_t total_evict_cnt = 0;  // cumulative count of evictions
size_t total_evict_items = 0;
size_t total_evict_size = 0;  // cumu size written to ssd
size_t total_release_size = 0; // cumu size of released slab mem

void    show_evict_stats()
{
    printf("\n======= Evicts:  %ld evicts, %ld items evicted. %ld written to ssd, %ld mem released\n",
        total_evict_cnt,total_evict_items, total_evict_size, total_release_size);

    total_evict_cnt=0;
    total_evict_items=0;
    total_evict_size=0;
    total_release_size=0;
}



/*****
evict some items from sb->slabclass[id] slab-class.
Shall already acquired locks on:
    (cache_lock in items.c) on the LRU list,
    slabclass[id].lock

Scan the LRU list for the slabclass backwards, release certain amount of items to recycle slab-ram.
Num of items is determined by the followings, whichever exceeds first:
- size of dirty items <= 1 cbuf;
- total released item size (dirty, clean) <= 2 cbuf.

Dirty items are coalesced to collective-buf (cbuf) and written to ssd.
then all items( dirty, clean) are recycled.

**********/
static int slabclass_evict_to_file( int id, slabs_t* sb )
{
    extern item *heads[LARGEST_ID]; // LRU head/tail of the value-items 
    extern item *tails[LARGEST_ID];
    extern unsigned int sizes[LARGEST_ID];
    
    unsigned char* buf;
    unsigned char* pb;
    item*   search;
    item** its;
    int i;
    
    slabclass_t *p = &sb->slabclass[id];
    int item_size = p->size; // size of a unit in this slabclass
    
    long buf_size = p->cbuf_size; //1024*1024;  // coll-buf size, will recycle this much buffer


    long ltmp, lt2;
    long fdoffset = 0;
    
    if( p->hb_fd <= 0 ){ // only hybrid-slab-allocator can evict to SSD
        err("slab \"%s\": class_%d fd=%d, cannot evict!!\n", 
            sb->name, id, p->hb_fd );
        return -1;
    }
    
    //if( sb->inmem_item[id] == NULL ) /// the last inmem item for this slabclass
        sb->inmem_item[id] = tails[id];
        
    /////////////  1. count items that shall be evicted to make mem-space
    int evict_items=0; // will evict this many items to fill
    int release_items = 0; // this many items will be released to recycle slab-cache ram 

    long evict_size = 0;  // actual data size of all items written to SSD
    long release_memsize = 0; // how much slab mem is released. = (slab-slot size * release-items)
    int retry = 0;
    // the format to write an item to ssd:  whole item size, (item-hdr, item-data)
    ltmp = (item_size + sizeof(long));

    pthread_mutex_lock( &cache_lock);
find_avail_items:
    //for(search = tails[id]; search != NULL && evict_size<buf_size; search=search->prev )
    for(search = sb->inmem_item[id]; 
        search != NULL && evict_size<buf_size && (release_memsize+ltmp)<buf_size*2; search=search->prev )
    {
        if( search->refcount>0 ) //   
            continue; // this item is being use, don't evict it.
            
        if( search->is_dirty ){ // need to write this item to SSD
            lt2 = sizeof(long) + ITEM_ntotal(search);
            if( evict_size + lt2 < buf_size ){
                evict_size += lt2;  // size of write for this item
                evict_items++; // this item will be evicted to ssd
                /// eviction data for an item shoud be: ITEM_ntotal(search) + sizeof(long)
            }
            else
                break;
        }
        
        release_memsize += item_size; // can reclaim this much mem
        release_items++;
    }
    if( evict_size % 512 ) // for direct-IO, write-size shall align to a block
        evict_size += (512-(evict_size%512));
        
    release_items = (release_items < sizes[id]) ? release_items : sizes[id];
    if( release_items < 1 ){
        dbg("try=%d: slab-id=%d: not enough items to evict: %d\n", retry, id, release_items );
        //relax_ram_queue(&memQ);
        clear_ram_queue(&memQ);
        if(++retry%100==0){
            err("try=%d: slab-id=%d: not enough items to evict: %d\n", retry, id, release_items );
            pthread_mutex_unlock( &cache_lock);
            return 0;
            assert(0);
        }
        goto find_avail_items; 
        //sleep(1000000);
        //return -1;
    }

    ////////////  2.  alloc coalesce buf if needed
    assert( evict_size <= buf_size );
    buf = p->cbuf;
    if( !buf ){
        posix_memalign((void**)&buf, 4096, buf_size); // coalesce-buf is page-aligned
        dbg("alloc c-buf = %p, size=%ld\n", buf, buf_size );
        p->cbuf = buf;
    }
        
    its = malloc(release_items * sizeof(item*));
    if( !buf || !its ){
        err("Failed to alloc mem:: buf=%p, its=%p\n", buf, its);
        goto err_out_1;
    }
    
    //// get the offset into the SSD file to write to
    //pthread_mutex_lock(&p->lock); // has already acquired the lock
    fdoffset = p->hb_fdoffset;
    p->hb_fdoffset += evict_size; // buf_size;
    //pthread_mutex_unlock(&p->lock);
    
    ////////// 3.  coalesce the item objs into the buf, from LRU-tail to head
    i=0;
    long csize = 0;
    pb = buf;
        
    //for(search = tails[id]; search != NULL && i<evict_items; search=search->prev )
    for(search = sb->inmem_item[id]; search != NULL && i<release_items; search=search->prev )
    {
        if( search->refcount>0 ) //   
            continue; 
        
        if( ! search->is_dirty )
        {
            // this item is clean, i.e., it has been written to SSD already
            its[i] = search;// just release its mem, but don't bother writting to SSD
            i++;
        }
        else // dirty item: shall write to ssd
        {
            its[i] = search;
            //its[i].pos_in_ssd = fdoffset + (unsigned long)(pb-buf);

            /// gather a new entry:  { size of item + long,  item(hdr+data) }
            //search->is_dirty = 0; 
            // clear the flag, so it's clean when the obj is loaded from file later.
            ltmp = ITEM_ntotal(search) + sizeof(long); 
            memcpy(pb, &ltmp, sizeof(long)); // item data size
            pb+= sizeof(long);
            
            //memcpy(pb, ITEM_data(search), ltmp); // copy the real data
            memcpy(pb, search, ITEM_ntotal(search)); // copy the item(hdr+data) as a while
            pb+= ITEM_ntotal(search);  // there may be some trailing empty data
            i++;
        }
    }
    assert( i==release_items ); 

    csize = (long)(pb - buf); // it's possible that csize ==0, when no items are dirty
    //assert( csize==evict_size && i==release_items );
    if( csize>0 && csize!=evict_size && (csize+(512-csize%512) != evict_size) ) 
    {
        err("evict %d items:  write-item size=%ld, evict_size=%ld\n",
                evict_items, csize, evict_size );
        assert(0);
    }
    sb->inmem_item[id] = search;
    
    dbg("evict %d items to ssd: write-item size=%ld, aligned write-size = %ld\n", 
        evict_items, csize, evict_size );
        
    ///////////////   4.  write to SSD
    //TODO:  the write logic shall take into account how GC is designed ...
    //evict_size = buf_size;  // write the whole buf-chunk, for simplicity, 
    
    if( csize>0 && csize < evict_size ){ // padding the ending with 0
        memset(buf+csize, 0, (evict_size-csize));
    }
    if( evict_size > 0 )
    {
        ltmp = pwrite(p->hb_fd, buf, evict_size, fdoffset);
        if( ltmp != evict_size ){
            printf("%s: Error!! pwrite ret %ld != %ld\n", __func__,
                ltmp, evict_size );
            goto err_out_1;
        }
    }
    
    total_evict_cnt++;
    total_evict_items += evict_items;
    total_evict_size += evict_size;
    
    /////////////  5.  release the item-memory that have been evicted to SSD
    release_memsize = 0;
    item *tit;
    vaddr_range_t *vr;

    //pthread_mutex_lock( &cache_lock);
    for(i=0; i<release_items; i++)
    {
        tit = its[i];
        if( tit->is_dirty ) {
            // this item is written to disk,  need to update its disk-offset
            //its[i]->pos_in_ssd = fdoffset; 
            vr = (vaddr_range_t*)tit->vaddr_range;
            pthread_mutex_lock( &vr->lock);
            if( vr->type == allocator_pool ){
                set_pool_obj_ssd_pos(vr, tit->address, fdoffset);
                set_pool_obj_state(vr, tit->address, STATE_ALLOC_CLEAN);
            } else {
                set_coalesce_obj_ssd_pos(vr, tit->address, fdoffset);
                set_coalesce_obj_state(vr, tit->address, STATE_ALLOC_CLEAN);
            }
            pthread_mutex_unlock( &vr->lock);

            fdoffset += (ITEM_ntotal(tit) + sizeof(long));
            tit->is_dirty = 0;
        }

        /////do_item_remove(its[i]); // dec refcount to 0
        tit->refcount = 0;

        //////do_item_unlink(its[i]); // take out of LRU and hash table, then free the item to slab
        tit->it_flags &= ~ITEM_LINKED;
        assoc_delete(ITEM_key(tit), sizeof(void*)); 
        item_unlink_q(tit);

        /////// item_free() ==> slab_free()
        ltmp = ITEM_ntotal(tit);
        tit->slabs_clsid = 0;
        tit->it_flags = ITEM_SLABBED;
        //// at slab_alloc(), already holds slabclass->lock, no need to get lock in slab_free().
        do_slabs_free(tit, ltmp, id, sb);

        release_memsize += item_size;
    }
    pthread_mutex_unlock( &cache_lock);

    dbg("has released %d items, each-item=%d, release-mem=%ld\n", 
        release_items, item_size, release_memsize);
        
    total_release_size += release_memsize;
    
    if(its)    free(its);
    return release_items;
    
err_out_1:
    if(its)    free(its);
    return -1;    
}

////// evict and free all objs from slab classes.
int evict_all_slabs(slabs_t *sb)
{
    int i;
    slabclass_t  *st;
    int cnt;

    for(i=POWER_SMALLEST; i<=sb->power_largest; i++)
    {
        st = &sb->slabclass[i];
        cnt = st->slabs * st->perslab - st->end_page_free - st->sl_curr;

        while( cnt > 0 ){
            printf("slab-%d: has %d live objs: will evict...\n", i, cnt );
            slabclass_evict_to_file( i, sb ); //sb->slabclass[i] );
            st = &sb->slabclass[i];
            cnt = st->slabs * st->perslab - st->end_page_free - st->sl_curr;
        }
    }
    return 0;
}



static void*  map_file(char* file, size_t len, int *pfd)
{
	int fd;
	int ret;
	
	fd = open(file, O_CREAT|O_RDWR|O_TRUNC|O_DIRECT, 0666);
	if( fd <0 ){
		printf("fail to open map file\n");
		return NULL;
	}
	
	if( len % 4096 )
	    len += (4096-len%4096);
	    
	ret = ftruncate( fd, len );
	if( ret ){
		printf("trunc failed: file=%s, len=%ld:  %s\n", file, len, strerror(errno) );
		close(fd);
		return NULL;
	}
	
	void *m = mmap(NULL, len, PROT_READ|PROT_WRITE,MAP_SHARED,fd,(off_t)0);
	if( m==(void*)-1 ){
		printf("mmap failed::  file=%s: len=%ld, %s\n", file, len, strerror(errno) );
		close(fd);
		return NULL;
	}
	printf("map %s: len=%ld, ret=%p\n", file, len, m);
	*pfd = fd;
	return m;
}



/**
 * Determines the chunk sizes and initializes the slab class descriptors
 * accordingly.
 */
slabs_t* slabs_init(const size_t limit, size_t min_chunksize, size_t max_chunksize, 
       char* name, const double factor, bool prealloc, int use_hybrid, int use_mmap ) 
{
    int i = 0, ret=0;
    unsigned int size = 0; // sizeof(item) + settings.chunk_size;
    
    slabs_t *sb = malloc(sizeof(slabs_t));
    
    if( !sb ){
        dbg(" failed to alloc slab... %d\n", 1);
        return NULL;
    }
    
    memset(sb, 0, sizeof(slabs_t));    
    strncpy(sb->name, name, MAX_SLABNAME);
    sb->name[MAX_SLABNAME-1] = 0;
    
    sb->mem_limit = limit;

    dbg(" max-total-mem=%ld, factor=%f, prealloc=%d, item-size-max=%d\n", 
           limit, factor, prealloc, settings.item_size_max );

    sb->use_mmap = use_mmap;
    if( use_mmap ){
        prealloc = true;  // if use-mmap, the force to use pre-alloc
        use_hybrid = 0;
        // if use file-mmap, disable ssd/mem hybird, since file-mmap is functionally identical to hybrid
    }
    
    if (prealloc) 
    {
        if( use_mmap ){
            //sprintf(sb->mmap_filename, "/tmp/ouyangx/mmap-%s", name);
            sprintf(sb->mmap_filename, "%s/mmap-%s", HB_DIR, name);
            sb->mem_base = map_file(sb->mmap_filename, sb->mem_limit, &(sb->mmap_fd) );
         
        } else {
            /* Allocate everything in a big chunk with malloc */
            //sb->mem_base = malloc(sb->mem_limit);
            i = posix_memalign((void**)(&sb->mem_base), 4096, sb->mem_limit);
            printf("init_slab: pre-alloc: memlimit=%ld, ret=%d, mem-base=%p\n", 
                        sb->mem_limit, i, sb->mem_base );
            if( mlock(sb->mem_base, sb->mem_limit) != 0 ){
                err_exit("prealloc-mem: unable to mlock...\n");
            }
        }
        
        if (sb->mem_base != NULL) 
        {
            sb->mem_current = sb->mem_base;
            sb->mem_avail = sb->mem_limit;
            sb->mem_malloced = 0; // has pre-alloced, but not used(mallocated) by users 
        } else {
            fprintf(stderr, "Warning: Failed to allocate requested memory in"
                    " one large chunk.\nWill allocate in smaller chunks\n");
        }
    }

    //memset(sb->slabclass, 0, sizeof(sb->slabclass));
    
    unsigned int nsize;
    size=min_chunksize;
    //max_chunksize = (max_chunksize<settings.item_size_max)? max_chunksize : settings.item_size_max;

    i = POWER_SMALLEST;
    dbg("in this slab, min-chunk=%ld, max-chunk=%ld\n", min_chunksize, max_chunksize );
    //while( i < POWER_LARGEST && size <= settings.item_size_max/factor  )
    //while( i < POWER_LARGEST && size <= max_chunksize/factor  )
    while( i < POWER_LARGEST && size <= max_chunksize/factor  )
    {
        /// Make sure items always n-byte aligned 
        if (size % CHUNK_ALIGN_BYTES)
            size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);

        nsize = sizeof(item) + size;
        if (nsize % CHUNK_ALIGN_BYTES)
            nsize += CHUNK_ALIGN_BYTES - (nsize % CHUNK_ALIGN_BYTES);

        sb->slabclass[i].size = nsize;
        sb->slabclass[i].perslab = settings.item_size_max / nsize;
        pthread_mutex_init(&sb->slabclass[i].lock, NULL);
        pthread_rwlock_init( &sb->slabclass[i].rwlock, NULL );
        size *= factor;
        
        if( use_hybrid ){
            sprintf(sb->slabclass[i].hb_filename, "%s/hb-%d", HB_DIR, sb->slabclass[i].size); //MAX_HB_FILENAME
            sb->slabclass[i].hb_fd = open( sb->slabclass[i].hb_filename, O_TRUNC|O_CREAT|O_RDWR|O_DIRECT, 0644);
            if( sb->slabclass[i].hb_fd <= 0 ){
                err("Faile to open hb-file: %s\n", sb->slabclass[i].hb_filename );
                perror(" open file failed!! \n");
                return NULL;
            }           
            sb->slabclass[i].hb_fdoffset = 0;
            sb->slabclass[i].cbuf_size = 1024*1024+4096; //(max_chunksize+4096); //1024*1024;
            sb->slabclass[i].cbuf = NULL; // don't pre-alloc cbuf, will alloc later on-the-fly
        }
        else{
            sb->slabclass[i].hb_fd = -1;
            sb->slabclass[i].cbuf = NULL; 
            sb->slabclass[i].cbuf_size = 0;
        }
        
        dump_slabclass( &(sb->slabclass[i]), i );
        i++;
        if( size >= max_chunksize ){
            break;
        }
    }

    nsize = max_chunksize + sizeof(item);    
    if (nsize % CHUNK_ALIGN_BYTES)
        nsize += CHUNK_ALIGN_BYTES - (nsize % CHUNK_ALIGN_BYTES);
    sb->power_largest = i;
    sb->slabclass[i].size = nsize; //settings.item_size_max; //max_chunksize;
    sb->slabclass[i].perslab = settings.item_size_max/nsize;
    pthread_mutex_init(&sb->slabclass[i].lock, NULL);   
    pthread_rwlock_init( &sb->slabclass[i].rwlock, NULL );

    if( use_hybrid ){
        sprintf(sb->slabclass[i].hb_filename, "%s/hb-%d", HB_DIR, sb->slabclass[i].size); 
        sb->slabclass[i].hb_fd = open( sb->slabclass[i].hb_filename, O_TRUNC|O_CREAT|O_RDWR|O_DIRECT, 0644);
        if( sb->slabclass[i].hb_fd <= 0 ){
            err("%s: faile to open hb-file: %s\n", sb->slabclass[i].hb_filename );
            perror(" open file failed!! \n");
            return NULL;
        }           
        sb->slabclass[i].hb_fdoffset = 0;
        sb->slabclass[i].cbuf_size = 1024*1024+4096; //(max_chunksize+4096); //1024*1024;
        sb->slabclass[i].cbuf = NULL; // don't pre-alloc cbuf, will alloc later on-the-fly
    }
              
    dump_slabclass( &(sb->slabclass[i]), i );
    
    pthread_mutex_init(&sb->slabs_lock, NULL); 

    dump_slabs( sb );
   
    return sb;
}

/// if a slab-alloc uses file-mmap, msync it
int slabs_msync(slabs_t* sb)
{
    int ret;

    if( sb->use_mmap && sb->mem_base ){
        printf(" begin msync:  %s: mem-size=%ld\n", sb->name, sb->mem_limit);
        ret = msync(sb->mem_base, sb->mem_limit, MS_SYNC );
        printf(" msync finished:  %s, ret=%d\n", sb->name, ret);
        ret = madvise(sb->mem_base, sb->mem_limit, MADV_DONTNEED);
        printf(" madvise() finished:  %s, ret=%d\n", sb->name, ret);
    }

    return ret;
}

int	slabs_destroy(slabs_t* sb)
{
    int i, j;
    int prealloc = 0;    
    slabclass_t *p; // = &sb->slabclass[id];
    
    if( sb )
        fprintf(stderr, " will destroy slab \"%s\"\n", sb->name);
        
    if( sb->mem_base)
    {
        if( sb->use_mmap ){ 
            dbg("unmap file: %s, len=%ld\n", sb->mmap_filename, sb->mem_limit );
            munmap(sb->mem_base, sb->mem_limit);	
            close(sb->mmap_fd);
            
        } else {
            dbg("free pre-alloc mem: %ld\n", sb->mem_limit );
            free(sb->mem_base);
        }
        prealloc = 1; // pre-allocated big-chunk mem for all slab-classes    
    }
    
    for (i = POWER_SMALLEST; i <= sb->power_largest; i++ )
    {
        // free slab-chunks for each slab-class
        p = &sb->slabclass[i];
            
        if( p->cbuf ) {   // release the coalesce buf 
            dbg("free coalesce-buf for slabclass-%d\n", i);
            free(p->cbuf);
        }   
        if( p->hb_fd >0) {// close the hybrid-mem fd
            dbg("close hybrid-mem file: %s\n", p->hb_filename );
            close(p->hb_fd );
        }
            
        /// if not pre-alloc, free each indiv slabs
        if( !prealloc ) {  
            for(j=0; j<p->slabs; j++ ) // free all slab bufs
                free(p->slab_list[j]);
        }
                
        if( p->slab_list ) // release the "free slot list"
        {
            dbg("slaballocator: %s: slab-%d: has freed %d slabs\n", 
                  sb->name, i, p->slabs );
            free(p->slab_list);
        }
            
        if( p->slots ) // releaes the free-slot list
        {
            dbg("  has freed free-slot=%d\n", p->sl_total );
            free( p->slots );
        }
    }
   
    free(sb);
    
    return 0;
}


#ifndef DONT_PREALLOC_SLABS
static void slabs_preallocate (const unsigned int maxslabs, slabs_t* sb) 
{
    int i;
    unsigned int prealloc = 0;

    /* pre-allocate a 1MB slab in every size class so people don't get
       confused by non-intuitive "SERVER_ERROR out of memory"
       messages.  this is the most common question on the mailing
       list.  if you really don't want this, you can rebuild without
       these three lines.  */
    dbg("will pre-alloc for %d slabs\n", maxslabs);
    for (i = POWER_SMALLEST; i <= POWER_LARGEST; i++) 
    {
        if (++prealloc > maxslabs)
            return;
        do_slabs_newslab(i, sb);
    }

}
#endif

static int grow_slab_list (const unsigned int id, slabs_t* sb) 
{
    slabclass_t *p = &sb->slabclass[id];
    dbg(" grow slab %d, slabs=%d, list_size=%d\n", id, p->slabs, p->list_size);
    if (p->slabs == p->list_size) {
        size_t new_size =  (p->list_size != 0) ? p->list_size * 2 : 32;
        void *new_list = realloc(p->slab_list, new_size * sizeof(void *));
        if (new_list == 0) return 0;
        p->list_size = new_size;
        p->slab_list = new_list;
    }
    return 1;
}

static int do_slabs_newslab(const unsigned int id, slabs_t* sb) 
{
    slabclass_t *p = &sb->slabclass[id];
    int len = p->size * p->perslab;
    char *ptr;

    dbg("alloc mem size=%d, for slabclass-%d: unit=%d, has %d slabs\n", 
        len, id, p->size, p->slabs );

    pthread_mutex_lock( &cache_lock );

    if ((sb->mem_limit && (sb->mem_malloced + len > sb->mem_limit) && p->slabs > 0) 
        || (grow_slab_list(id, sb) == 0) 
        || ((ptr = memory_allocate((size_t)len, sb)) == 0) ) 
    {
        pthread_mutex_unlock( &cache_lock );
        return 0;
    }

    pthread_mutex_unlock( &cache_lock );

    memset(ptr, 0, (size_t)len);
    p->end_page_ptr = ptr;
    p->end_page_free = p->perslab;

    p->slab_list[p->slabs++] = ptr;
    sb->mem_malloced += len;


    dbg("grow slabclass-%d  with buf=%p\n", id, ptr);

    return 1;
}

/*@null@*/
static void *do_slabs_alloc(const size_t size, unsigned int id, slabs_t* sb) 
{
    slabclass_t *p;
    void *ret = NULL;
    if (id < POWER_SMALLEST || id > sb->power_largest) {
        err(" id=%d out of range\n", id);
        //MEMCACHED_SLABS_ALLOCATE_FAILED(size, 0);
        return NULL;
    }
    
    int tries = 1;
    
    p = &sb->slabclass[id];
    dbg(" -- alloc itemsz=%ld, in slab-%d, slab-unitsz=%d\n", size, id, p->size);
    //dump_slabclass(p, id);
    //assert(p->sl_curr == 0 || ((item *)p->slots[p->sl_curr - 1])->slabs_clsid == 0);

#ifdef USE_SYSTEM_MALLOC
    if (sb->mem_limit && sb->mem_malloced + size > sb->mem_limit) {
        return 0;
    }
    sb->mem_malloced += size;
    ret = malloc(size);
    return ret;
#endif

    tries = 2;
    while(tries)
    {
        /// fail unless we have space at the end of a recently allocated page,
        ///  we have something on our freelist, or we could allocate a new page
        if ( !(p->end_page_ptr != 0 || p->sl_curr != 0 ||
                do_slabs_newslab(id, sb) != 0) ) {
            /// We don't have more memory available
            ret = NULL;
        } else if (p->sl_curr != 0) {
            /// return off our freelist
            ret = p->slots[--p->sl_curr];
        } else {
            /// if we recently allocated a whole page, return from that
            assert(p->end_page_ptr != NULL);
            ret = p->end_page_ptr;
            if (--p->end_page_free != 0) {
                p->end_page_ptr = ((caddr_t)p->end_page_ptr) + p->size;
            } else {
                p->end_page_ptr = 0;
            }
        }

        if( !ret ){ // no more mem, evict some items to make mem
            dbg("---------  slab-%d: try %d, no mem, start evict...\n", id, tries);

            ////  acquire a write-lock on the slabclass ssd-file
            //pthread_mutex_lock( &cache_lock);
            pthread_rwlock_wrlock( &p->rwlock );
            //////////
            slabclass_evict_to_file(id, sb);
            ////////
            pthread_rwlock_unlock( &p->rwlock );
            //pthread_mutex_unlock( &cache_lock);
            ////////////

            tries--;
        }
        else
            break;
    }
    
    if (ret) {
        p->requested += size;
    } else {
    }
    dbg(" get addr=%p\n", ret);
    return ret;
}

static void do_slabs_free(void *ptr, const size_t size, unsigned int id, slabs_t* sb) 
{
    slabclass_t *p;
    
    //assert(((item *)ptr)->slabs_clsid == 0);
    assert(id >= POWER_SMALLEST && id <= sb->power_largest);
    if (id < POWER_SMALLEST || id > sb->power_largest)
        return;

    dbg("slaballoc \"%s\": free slab-%d, size=%ld\n", sb->name, id, size);

    //MEMCACHED_SLABS_FREE(size, id, ptr);
    p = &sb->slabclass[id];

#ifdef USE_SYSTEM_MALLOC
    sb->mem_malloced -= size;
    free(ptr);
    return;
#endif

    if (p->sl_curr == p->sl_total) { /* need more space on the free list */
        int new_size = (p->sl_total != 0) ? p->sl_total * 2 : 32;  /* 16 is arbitrary */
        void **new_slots = realloc(p->slots, new_size * sizeof(void *));
        if (new_slots == 0)
            return;
        p->slots = new_slots;
        p->sl_total = new_size;
    }
    p->slots[p->sl_curr++] = ptr;
    p->requested -= size;
    return;
}

static int nz_strcmp(int nzlength, const char *nz, const char *z) {
    int zlength=strlen(z);
    return (zlength == nzlength) && (strncmp(nz, z, zlength) == 0) ? 0 : -1;
}



bool get_stats(const char *stat_type, int nkey, ADD_STAT add_stats, void *c) {
    bool ret = true;
    /*
    if (add_stats != NULL) {
        if (!stat_type) {
            /// prepare general statistics for the engine 
            STATS_LOCK();
            APPEND_STAT("bytes", "%llu", (unsigned long long)stats.curr_bytes);
            APPEND_STAT("curr_items", "%u", stats.curr_items);
            APPEND_STAT("total_items", "%u", stats.total_items);
            APPEND_STAT("evictions", "%llu",
                        (unsigned long long)stats.evictions);
            APPEND_STAT("reclaimed", "%llu",
                        (unsigned long long)stats.reclaimed);
            STATS_UNLOCK();
        } else if (nz_strcmp(nkey, stat_type, "items") == 0) {
            item_stats(add_stats, c);
        } else if (nz_strcmp(nkey, stat_type, "slabs") == 0) {
            slabs_stats(add_stats, c);
        } else if (nz_strcmp(nkey, stat_type, "sizes") == 0) {
            item_stats_sizes(add_stats, c);
        } else {
            ret = false;
        }
    } else {
        ret = false;
    }
    */
    return ret;
}   

/*@null@*/
/*
static void do_slabs_stats(ADD_STAT add_stats, void *c, slabs_t* sb) {
    int i, total;
    /////Get the per-thread stats which contain some interesting aggregates 
    struct thread_stats thread_stats;
    threadlocal_stats_aggregate(&thread_stats);

    total = 0;
    for(i = POWER_SMALLEST; i <= sb->power_largest; i++) {
        slabclass_t *p = &sb->slabclass[i];
        if (p->slabs != 0) {
            uint32_t perslab, slabs;
            slabs = p->slabs;
            perslab = p->perslab;

            char key_str[STAT_KEY_LEN];
            char val_str[STAT_VAL_LEN];
            int klen = 0, vlen = 0;

            APPEND_NUM_STAT(i, "chunk_size", "%u", p->size);
            APPEND_NUM_STAT(i, "chunks_per_page", "%u", perslab);
            APPEND_NUM_STAT(i, "total_pages", "%u", slabs);
            APPEND_NUM_STAT(i, "total_chunks", "%u", slabs * perslab);
            APPEND_NUM_STAT(i, "used_chunks", "%u",
                            slabs*perslab - p->sl_curr - p->end_page_free);
            APPEND_NUM_STAT(i, "free_chunks", "%u", p->sl_curr);
            APPEND_NUM_STAT(i, "free_chunks_end", "%u", p->end_page_free);
            APPEND_NUM_STAT(i, "mem_requested", "%llu",
                            (unsigned long long)p->requested);
            APPEND_NUM_STAT(i, "get_hits", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].get_hits);
            APPEND_NUM_STAT(i, "cmd_set", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].set_cmds);
            APPEND_NUM_STAT(i, "delete_hits", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].delete_hits);
            APPEND_NUM_STAT(i, "incr_hits", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].incr_hits);
            APPEND_NUM_STAT(i, "decr_hits", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].decr_hits);
            APPEND_NUM_STAT(i, "cas_hits", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].cas_hits);
            APPEND_NUM_STAT(i, "cas_badval", "%llu",
                    (unsigned long long)thread_stats.slab_stats[i].cas_badval);

            total++;
        }
    }

    ////add overall slab stats and append terminator

    APPEND_STAT("active_slabs", "%d", total);
    APPEND_STAT("total_malloced", "%llu", (unsigned long long)mem_malloced);
    add_stats(NULL, 0, NULL, 0, c);
}       */

static void *memory_allocate(size_t size, slabs_t* sb) 
{
    void *ret;

    if (sb->mem_base == NULL) {
        /// We are not using a preallocated large memory chunk
        ret = malloc(size);
    } else {
        ret = sb->mem_current;

        if (size > sb->mem_avail) {
            return NULL;
        }
        size_t  ps = size;
        //// mem_current pointer _must_ be aligned!!!
        if (size % CHUNK_ALIGN_BYTES) {
            size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
        }
        dbg(" will alloc size=%ld, align-size=%ld, prev-avail=%ld, now-avail=%ld\n", 
            size, sb->mem_avail, sb->mem_avail, sb->mem_avail-size );

        sb->mem_current = ((char*)sb->mem_current) + size;
        if (size < sb->mem_avail) {
            sb->mem_avail -= size;
        } else {
            sb->mem_avail = 0;
        }
    }

    return ret;
}


void*  myslabs_alloc(size_t size, slabs_t* sb)
{
    unsigned int id = slabs_clsid(size, sb);
    if( id < POWER_SMALLEST )   return NULL;
    return slabs_alloc(size, id, sb);
}

void    myslabs_free(void* ptr, size_t size, slabs_t* sb)
{
    unsigned int id = slabs_clsid(size, sb);
    if( id < POWER_SMALLEST )   return;
    slabs_free(ptr, size, id, sb);
}

void *slabs_alloc(size_t size, unsigned int id, slabs_t* sb) 
{
    void *ret;

    //pthread_mutex_lock(&sb->slabs_lock);
    pthread_mutex_lock(&sb->slabclass[id].lock);
    ret = do_slabs_alloc(size, id, sb);
    //pthread_mutex_unlock(&sb->slabs_lock);
    pthread_mutex_unlock(&sb->slabclass[id].lock);
    return ret;
}

void slabs_free(void *ptr, size_t size, unsigned int id, slabs_t* sb) 
{
    //pthread_mutex_lock(&sb->slabs_lock);
    pthread_mutex_lock(&sb->slabclass[id].lock);
    do_slabs_free(ptr, size, id, sb);
    //pthread_mutex_unlock(&sb->slabs_lock);
    pthread_mutex_unlock(&sb->slabclass[id].lock);
}

void slabs_stats(ADD_STAT add_stats, void *c) 
{
    //pthread_mutex_lock(&sb->slabs_lock);
    //do_slabs_stats(add_stats, c);
    //pthread_mutex_unlock(&sb->slabs_lock);
}
