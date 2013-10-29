/******************

When sigsegv handler has instantiated enough RAM units, 
shall release the RAM beyond certain limit so as to work with
huge vaddr-units.

*****************/
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

#include <semaphore.h>


#include "myhmem.h"
#include "vaddr.h"

/*
mem_unit_t *ram_list;   // array of materialized ram-units (pg-size)
volatile unsigned long  head=0;  // head ptr
volatile unsigned long  tail=0;  // tail ptr
sem_t   ram_sem;

static volatile int do_ram_reclaim = 0;
pthread_mutex_t ram_lock;
*/

mem_queue_t     memQ;



void    dump_ram_queue(mem_queue_t *q)
{
    printf("\n----  mem-q:  head=%ld, tail=%ld, num-units=%ld, num-rel=%ld, rel-size=%ld\n",
        q->head, q->tail, q->num_units, q->num_release, q->size_release );

}

inline void clear_ram_queue(mem_queue_t* q)
{
    int t;
    int alloctype;

    pthread_mutex_lock( &q->ram_lock );
    while( q->head < q->tail )
    {
        // release the first units 
        t = (q->head++) % q->list_size;

        //int state;
        void *va = q->ram_list[t].address;
        vaddr_range_t *vr = q->ram_list[t].vrange;
        alloctype = vr->type;
        obj_item_t *om;
        if( alloctype == allocator_pool){
            om = get_pool_obj_item(vr, va);
        }
        else{
            get_coalesce_obj_item(vr, va, &om);
        }
        
		item *it = q->ram_list[t].slabitem;
		
        /// if pg-buf contains dirty data, save the obj contents into RAM-slab cache
        if( om->pgbuf_dirty )
        {
            void *objvaddr;
            if( alloctype == allocator_pool ){
                get_pool_obj_vaddr(vr, va, &objvaddr);
            } else{
                get_coalesce_obj_vaddr(vr, va, &objvaddr);
            }
            dbg("pgbuf-addr=%p, obj-addr=%p\n", va, objvaddr);
            //hb_pool_item_get(objvaddr, vr, &it, 0);//either in slab-cache, or load from SSD
            
            void *slabaddr;
            int itsz;
            if( !it ){
                err("fail to get item: va=%p\n", va);
            }

            if( it ){
                // save contents to RAM slab
                assert( it->nbytes <= vr->pool.obj_size );
                slabaddr= ITEM_data(it); // + (va-objvaddr);
                itsz = it->nbytes; // - (va-objvaddr);
                dbg("vaddr=%p, move dirty data to slab=%p, size=%d\n", va, slabaddr, itsz );
                //memcpy(slabaddr, va, itsz);
                memcpy(slabaddr, objvaddr, itsz);

                if( alloctype == allocator_pool ){
                    set_pool_obj_state(vr, va, STATE_ALLOC_DIRTY);                
                } else {
                    set_coalesce_obj_state(vr, va, STATE_ALLOC_DIRTY); 
                }
                it->is_dirty = 1;
                ///hb_item_remove(it);
                //it->refcount = 0;  // dec the refcount
            }
            else{
                err("will mv obj %p from pgbuf, but it-hdr is null...\n", va);
            }
        }
        
        om->pgbuf_dirty = 0;
        it->refcount = 0;  // dec the refcount, this slab item can be released.
        
        /// release the RAM
        madvise(q->ram_list[t].address, q->ram_list[t].length, MADV_DONTNEED);
        mprotect(q->ram_list[t].address, q->ram_list[t].length, PROT_NONE);
        q->num_units--;
        q->num_release++;
        q->size_release += q->ram_list[t].length;

    }
    pthread_mutex_unlock( &q->ram_lock );

}

/**
if ram-q contains too many items, release some items.
**/
inline void relax_ram_queue(mem_queue_t* q)
{
    int t;
    int alloctype;

    pthread_mutex_lock( &q->ram_lock );
    if(q->head < q->tail)
    {

        // release the first units 
        t = (q->head++) % q->list_size;

        //int state;
        void *va = q->ram_list[t].address;
        vaddr_range_t *vr = q->ram_list[t].vrange;
        alloctype = vr->type;
        obj_item_t *om;
        if( alloctype == allocator_pool){
            om = get_pool_obj_item(vr, va);
        }
        else{
            get_coalesce_obj_item(vr, va, &om);
        }
        
		item *it = q->ram_list[t].slabitem;
		
        /// if pg-buf contains dirty data, save the obj contents into RAM-slab cache
        if( om->pgbuf_dirty )
        {
            void *objvaddr;
            if( alloctype == allocator_pool ){
                get_pool_obj_vaddr(vr, va, &objvaddr);
            } else{
                get_coalesce_obj_vaddr(vr, va, &objvaddr);
            }
            dbg("pgbuf-addr=%p, obj-addr=%p\n", va, objvaddr);
            //hb_pool_item_get(objvaddr, vr, &it, 0);//either in slab-cache, or load from SSD
            
            void *slabaddr;
            int itsz;
            if( !it ){
                err("fail to get item: va=%p\n", va);
            }

            if( it ){
                // save contents to RAM slab
                assert( it->nbytes <= vr->pool.obj_size );
                slabaddr= ITEM_data(it); // + (va-objvaddr);
                itsz = it->nbytes; // - (va-objvaddr);
                dbg("vaddr=%p, move dirty data to slab=%p, size=%d\n", va, slabaddr, itsz );
                //memcpy(slabaddr, va, itsz);
                memcpy(slabaddr, objvaddr, itsz);

                if( alloctype == allocator_pool ){
                    set_pool_obj_state(vr, va, STATE_ALLOC_DIRTY);                
                } else {
                    set_coalesce_obj_state(vr, va, STATE_ALLOC_DIRTY); 
                }
                it->is_dirty = 1;
                ///hb_item_remove(it);
                //it->refcount = 0;  // dec the refcount
            }
            else{
                err("will mv obj %p from pgbuf, but it-hdr is null...\n", va);
            }
        }
        
        om->pgbuf_dirty = 0;
        it->refcount = 0;  // dec the refcount, this slab item can be released.
        
        /// release the RAM
        madvise(q->ram_list[t].address, q->ram_list[t].length, MADV_DONTNEED);
        mprotect(q->ram_list[t].address, q->ram_list[t].length, PROT_NONE);
        q->num_units--;
        q->num_release++;
        q->size_release += q->ram_list[t].length;

    }
    pthread_mutex_unlock( &q->ram_lock );

}

/***
Add a newly instantiated ram-unit into the queue.
If ram-usage hit some limit, release the first ram-unit in the queue.

***/
inline void enqueue_ram_queue(void *addr, size_t sz, item* slabitem, vaddr_range_t* vrange, mem_queue_t *q) 
{
    int t;
    //dump_ram_queue(q);
    int alloctype;

    pthread_mutex_lock( &q->ram_lock );

    // ignoring re-used ram units
    while( q->head < q->tail && 
           (q->ram_list[q->head % q->list_size].address == addr) )
    {
        q->head++;
        q->num_units--;
    }
    
    if( q->num_units >= q->list_size &&  // has hit the ram-usage limit, shall release some ram
         (q->head < q->tail)   ) 
    {
        // release the first units 
        t = (q->head++) % q->list_size;

        //int state;
        void *va = q->ram_list[t].address;
        vaddr_range_t *vr = q->ram_list[t].vrange;
        alloctype = vr->type;
        obj_item_t *om;
        slabclass_t* p;
 
        if( alloctype == allocator_pool){
            om = get_pool_obj_item(vr, va);
            //state = get_pool_obj_state(vr, va);
        }
        else{
            get_coalesce_obj_item(vr, va, &om);
        }
        
		item *it=NULL;
		it = q->ram_list[t].slabitem;
        if( !it ){
            err("fail to get item: va=%p\n", va);
        }
	    p = &sb_hbmem->slabclass[it->slabs_clsid];	

  /**  if use write-lock: concurrent read are stable, but with less throughput (11.5K reads/sec), 
       since it blocks reader threads. 
       If use read-lock:  can produce higher concurrent read throughput(16K reads/sec), but 
       occasionally read results are incorrect...
  **/
        //pthread_rwlock_rdlock( &p->rwlock ); 
        pthread_rwlock_wrlock( &p->rwlock );
        pthread_mutex_lock( &vr->lock); 
        /// save the obj contents into RAM-slab cache
        if( om->pgbuf_dirty )
        {
            void *objvaddr;

            if( alloctype == allocator_pool ){
                get_pool_obj_vaddr(vr, va, &objvaddr);
                assert( it->nbytes <= vr->pool.obj_size );
            } else{
                get_coalesce_obj_vaddr(vr, va, &objvaddr);
                assert( it->nbytes <= vr->coalesce.obj_size );
            }
            // here, the whole range of [objvaddr, obj-size) has been instantiated in pg-buf.
            dbg("pgbuf-addr=%p, obj-addr=%p\n", va, objvaddr);
            
            void *slabaddr;
            int itsz;

            // save contents to RAM slab
            slabaddr= ITEM_data(it); // + (va-objvaddr);
            itsz = it->nbytes; // - (va-objvaddr);
            dbg("vaddr=%p, move dirty data to slab=%p, size=%d\n", va, slabaddr, itsz );
            //memcpy(slabaddr, va, itsz);
            memcpy(slabaddr, objvaddr, itsz);

            //pthread_mutex_lock( &cache_lock ); 

            if( alloctype == allocator_pool ){
                set_pool_obj_state(vr, va, STATE_ALLOC_DIRTY);                
            } else {
                set_coalesce_obj_state(vr, va, STATE_ALLOC_DIRTY); 
            }
            it->is_dirty = 1;
            it->refcount = 0;  // dec the refcount, this slab item can be released.
            om->pgbuf_dirty = 0;
            //pthread_mutex_unlock( &cache_lock );  // no need
        }
        else {
            //pthread_mutex_lock( &cache_lock ); 
            it->refcount = 0;  // dec the refcount, this slab item can be released.
            //pthread_mutex_unlock( &cache_lock ); 
        }
        //pthread_mutex_unlock( &cache_lock ); 
        pthread_mutex_unlock( &vr->lock); 
        pthread_rwlock_unlock( &p->rwlock );
       
        /// release the RAM
        madvise(q->ram_list[t].address, q->ram_list[t].length, MADV_DONTNEED);
        mprotect(q->ram_list[t].address, q->ram_list[t].length, PROT_NONE);
        q->num_units--;
        q->num_release++;
        q->size_release += q->ram_list[t].length;
    }

    // enq the new ram-item
    t = (q->tail++) % q->list_size; 
    q->ram_list[t].address = addr;
    q->ram_list[t].length = sz;
    q->ram_list[t].vrange = vrange;
    q->ram_list[t].slabitem = slabitem;
    q->num_units++;
    dbg("enq:  addr=%p, size=%ld\n", addr, sz); 
    pthread_mutex_unlock( &q->ram_lock );
	//dump_ram_queue(q);
}


/***
main thread to release RAM when sigsegv ram usage has hit some limit.
**/
static void *ram_reclaim(void* arg)
{
    dbg("begin...\n");
   

    dbg("exit now...\n");    
}

int start_ram_reclaim(size_t ramq_size, size_t unitsize )
{
	//size_t listsize = ramq_size / PGSIZE;
	size_t listsize = ramq_size / unitsize;
	
    printf("***  RAM-instantiate limit: max-usage=%ld, ram-list-size=%ld\n", 
    	ramq_size, listsize );

    memQ.ram_list = malloc(sizeof(mem_unit_t)*listsize);
    if( !memQ.ram_list ){
        err("fail to alloc ram list...\n");
        return -1;
    }

	memQ.list_size = listsize;
	
    memQ.head = 0;
    memQ.tail = 0;

    sem_init(&memQ.ram_sem, 0, 0);

    pthread_mutex_init( &memQ.ram_lock, NULL );

    return 0;
}


int stop_ram_reclaim()
{
    /// free the array
    if( memQ.ram_list )
	    free(memQ.ram_list);

    sem_destroy(&memQ.ram_sem);

    pthread_mutex_destroy( &memQ.ram_lock );

    memset(&memQ, 0, sizeof(memQ));

    return 0;
}






