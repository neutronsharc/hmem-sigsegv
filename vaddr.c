

#define _GNU_SOURCE // this will open __USE_GNU, needed for ucontext_t


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>



#include <signal.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <assert.h>

#include <sys/ucontext.h>
#include <string.h>


#include "debug.h"
#include "vaddr.h"
#include "sigseg.h"
#include "assoc.h"
#include "items.h"


extern slabs_t*  sb_hbmem;
extern slabs_t*  sb_htable;

/// a list of all pool-allocators
vaddr_pool_list_t   pList;

vaddr_coalesce_list_t  coalList; // 2-d array of coalesce-allcators


// a balanced BST, all vaddr-ranges are included in this tree
avl_tree_t  range_tree;


///////////////
static size_t max_obj_size=0;

char* state2string(int obj_state)
{
    switch( obj_state )
    {
        case STATE_FREE:
            return  "STATE_FREE";
        case STATE_ALLOC_EMPTY:
            return "STATE_ALLOC_EMPTY";
        case STATE_ALLOC_DIRTY:
            return "STATE_ALLOC_DIRTY";
        case STATE_ALLOC_CLEAN:
            return "STATE_ALLOC_CLEAN";
        default:
            return "Unknown";         
    }

    return "";
}


void    dump_allocator(vaddr_range_t* vr)
{
    char *name;
    if( vr->type == allocator_pool )
        name = "PoolAlloc";
    else
        name = "ClsAlloc";

    fprintf(stderr, "\n%s:  start=%p, length=%ld, curr-addr=%p, free-len=%ld\n", 
            name, vr->start, vr->length, vr->curr_vaddr, vr->free_length );

    unsigned int alloced = 0;
    unsigned int freed = 0;
    unsigned int maxobj = 0;
    unsigned int avail = 0;

    if( vr->type == allocator_pool )
    {
        alloced = (vr->length - vr->free_length) / vr->pool.vaddr_unit_size;
        freed = vr->pool.num_free_objs;
        maxobj = vr->length / vr->pool.vaddr_unit_size;
        avail = freed + (vr->free_length/vr->pool.vaddr_unit_size);
        fprintf(stderr, "objsize=%ld, unitsize=%ld, max-obj=%ld, avail=%ld: has alloced %ld, freed %ld, flist-size=%ld\n",
            vr->pool.obj_size, vr->pool.vaddr_unit_size, maxobj, avail, alloced, vr->pool.num_free_objs, vr->pool.free_list_size );
    }
    else
    {
        alloced = (vr->length - vr->free_length) / vr->coalesce.vaddr_unit_size;
        freed = vr->coalesce.num_free_objs;
        maxobj = vr->length / vr->coalesce.vaddr_unit_size;
        avail = freed + (vr->free_length/vr->coalesce.vaddr_unit_size);
        fprintf(stderr, "objsize=%ld, unitsize=%ld, max-obj=%ld: avail=%ld, has alloced %ld, freed %ld, flist-size=%ld\n",
            vr->coalesce.obj_size, vr->coalesce.vaddr_unit_size, maxobj, avail, alloced,
        vr->coalesce.num_free_objs, vr->coalesce.free_list_size );
    
    }

    printf("\n");
}


void dump_pool_list(vaddr_pool_list_t* pools)
{
    int i;
    vaddr_range_t *vr;

    fprintf(stderr, "\n========= pool_lists: has %d pool allocs\n", pools->num_pools-1);

    for(i=POWER_SMALLEST; i<pools->num_pools; i++)
    {
            fprintf(stderr, "pool %3d:  objsize=%ld, unitsize=%ld, has %d ranges\n", i,
              pools->objsize[i], pools->vaddr_unitsize[i], pools->pool_array[i].num_ranges );
        list_for_each_entry(vr, &pools->pool_array[i].head, range_list )
        {
            dump_allocator(vr);
        }
    }

}


void    dump_coalesce_list(vaddr_coalesce_list_t *carray)
{
    int i;

    printf("\n==  coal-list: %d coals, %d objsizes\n", 
        carray->num_coalesce, carray->num_objsize );

    printf("\tcoal-alloc sizes: \n");
    for(i=0; i<carray->num_coalesce; i++){
        printf("[%d]: %ld\n", i, carray->coalesce_sizes[i]);
    }
    printf("\tobj-sizes::: \n");
    for(i=0; i<carray->num_objsize; i++){
        printf("[%d]: %ld\n", i, carray->obj_sizes[i]);
    }

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
///////////////////////////    coalesce-allocator

int get_coal_alloc_index(vaddr_coalesce_list_t *carray, size_t allocsize, size_t objsize, int *row, int *col)
{
    int i=0;

    /// find best fit of coal-size
    for(i=0; i<carray->num_coalesce; i++)
    {
        if( allocsize <= carray->coalesce_sizes[i] )
            break;
    }
    if( i>= carray->num_coalesce ){
        err("alloc-size too big=%ld\n", allocsize);
        return -1;
    }        
    *row = i;
    
    //// find best-fit of obj-size.
    for(i=0; i<carray->num_objsize; i++) 
    {
        if( objsize <= carray->obj_sizes[i] )
            break;
    }
    if( i>= carray->num_objsize ){
        err("obj-size too big=%ld\n", objsize);
        return -1;
    }
    *col = i;

    /////////
    return 0;
}


int init_coal_alloc_array(vaddr_coalesce_list_t *carray)
{
    size_t objsize, allocsize;  
    int i=0, j=0;
    
    /// compute num of rows ( num of coal-allocators )  
    for(allocsize=PGSIZE; allocsize<=MAX_COALESCE_ALLOC_SIZE; allocsize*=2) 
        i++;
    carray->num_coalesce = i;
    
    // compute num of cols ( num of in-ram obj-unit sizes )
    i=0;
    for(objsize=PGSIZE; objsize<=MAX_IN_RAM_UNIT_SIZE; objsize*=2 ) 
        i++;
    carray->num_objsize = i;
    
    /// init row-col array
    carray->coalesce_sizes = malloc(carray->num_coalesce * sizeof(size_t));
    carray->obj_sizes = malloc( carray->num_objsize * sizeof(size_t) );
    for(i=0; i<carray->num_coalesce; i++)
    {
        carray->coalesce_sizes[i] = PGSIZE << i;
    }
    for(i=0; i<carray->num_objsize; i++)
    {
        carray->obj_sizes[i] = PGSIZE << i;
    }
    
    /// alloc the 2-d array
    carray->coalesce_array = (vaddr_range_list_t**)malloc(carray->num_coalesce * sizeof(vaddr_range_list_t*));      
    for(i=0; i<carray->num_coalesce; i++)
    {
        carray->coalesce_array[i] = (vaddr_range_list_t*)malloc(carray->num_objsize * sizeof(vaddr_range_list_t));
        objsize = PGSIZE;
        for(j=0; j<carray->num_objsize; j++) // init the head link for each coal-alloc
        {
            carray->coalesce_array[i][j].num_ranges = 0;
            INIT_LIST_HEAD( &carray->coalesce_array[i][j].head );
            pthread_mutex_init( &carray->coalesce_array[i][j].lock, NULL );
        }
    }

 
    return 0;

}

int destroy_coal_alloc_array(vaddr_coalesce_list_t *carray)
{
    int i, j;
    struct list_head *pos, *n;
    vaddr_range_t  *vr;

    /// release the 2-d array
    for(i=0; i<carray->num_coalesce; i++)
    {
        for(j=0; j<carray->num_objsize; j++)
        {   
            /// release all vaddr-range in the list [i][j] :
            list_for_each_safe(pos, n, &carray->coalesce_array[i][j].head )
            {
                vr = list_entry(pos, vaddr_range_t, range_list);
                destroy_coalesce_allocator(vr, &carray->coalesce_array[i][j]);
            }
        
        }   
        free( carray->coalesce_array[i] ); // free all vaddr-list for coal-alloc "i"
    }
    free(carray->coalesce_sizes);
    free(carray->obj_sizes);
    
    free(carray->coalesce_array);
        
}


/*
Create a coalesce-allocator for given:  (vaddr-alloc size, obj-size),
then link this coal-alloc into the correct "vaddr_range_list_t".
Already acquired a lock on the vaddr-range list.
*/
vaddr_range_t* create_coalesce_allocator(size_t allocsize, size_t objsize, vaddr_range_list_t* vrlist)
{
    vaddr_range_t   *vr = NULL;
    unsigned long len; // 1G
    unsigned long i;

    /// 0. get v-range itself
    vr = malloc(sizeof(vaddr_range_t));
    if( !vr ){
        err("alloc vaddr-range failed\n");
        return NULL;
    } 
    memset(vr, 0, sizeof(vaddr_range_t));
    INIT_LIST_HEAD( &vr->range_list );
    vr->vrlist = vrlist;
    pthread_mutex_init( &vr->lock, NULL );

    /// 1. alloc virtual-address range, and mprotect
    len = VADDR_RANGE_SIZE > allocsize ? VADDR_RANGE_SIZE : allocsize; 
    vr->start = memalign(PGSIZE, len);
    if( !vr->start ){
        err("fail to alloc: %ld\n", len);
        goto err_out_1;
    }
    
    if(mprotect (vr->start, len, PROT_NONE) != 0){
        err("unable to mprotect: %p, len=%ld\n", vr->start, len);
        goto err_out_2;
    }
    
    dbg("has alloc and mprotected:  vaddr=%p, len=%ld\n", vr->start, len);

    vr->length = len;
    vr->curr_vaddr = vr->start;
    vr->free_length = len;

    vr->type = allocator_coalesce;

    vr->coalesce.obj_size = objsize; 
    vr->coalesce.vaddr_unit_size = allocsize; 

    // init the search tree node
    vr->treenode.address = (unsigned long)(vr->start);
    vr->treenode.len = len;

    /// 2. prepare obj-table for this vaddr-range
    len = vr->length / vr->coalesce.obj_size; //// max num of objs in this range

    //vr->coalesce.obj_table = malloc( len *sizeof(obj_item_t) );
    posix_memalign((void**)&(vr->coalesce.obj_table), 4096, len *sizeof(obj_item_t) );
    //vr->coalesce.obj_table = malloc( len *sizeof(obj_item_t) );
    if( !vr->coalesce.obj_table ){
        err("fail to alloc obj-tbl: %ld objs, obj-size=%ld, all-size=%ld\n",
            len, vr->coalesce.obj_size, vr->length  );
        goto err_out_3;
    }

    dbg("has alloc obj-tbl: %ld objs, obj-size=%ld, all-size=%ld\n",
        len, vr->coalesce.obj_size, vr->length );
    
    vr->coalesce.obj_table_size = len;
    for(i=0; i<len; i++){
        vr->coalesce.obj_table[i].state = STATE_FREE;
    }

    //// 3. prepare free-obj-list
    vr->coalesce.free_list = malloc(32*sizeof(void*));
    vr->coalesce.free_list_size = 32;
    vr->coalesce.num_free_objs = 0;

    //// 4.1  link this range to the vaddr-range list
    list_add( &vr->range_list, &vrlist->head );
    vrlist->num_ranges++;
  
    /// 4.2   Insert this range to the BST. 
    insert( &range_tree, &vr->treenode );

    return vr;

////    err  out:

err_out_3:
err_out_2:
    free(vr->start);

err_out_1:
    free(vr);
    return NULL;
}


/*
Take away the coal-alloc from vaddr-list in the coal-array, free its resources.
Assume: already hold the coal-array lock before coming here.
*/
int destroy_coalesce_allocator( vaddr_range_t* vr, vaddr_range_list_t* vrlist)
{
    dbg("*** will destroy coalesce: \n");
    dump_allocator(vr);
    
    assert(vr->type == allocator_coalesce); 
    
    /// take it out of the search tree...
    delete(&range_tree, &vr->treenode);     

    /// take it out of the vaddr-range list
    list_del( &vr->range_list );
    vrlist->num_ranges--;

    /// free the v-address
    if( vr->coalesce.free_list )
        free(vr->coalesce.free_list);
    if( vr->coalesce.obj_table )
        free(vr->coalesce.obj_table);

    if( vr->start )
        free(vr->start);
        
    
    ////////////
    free(vr);

    return 0;

}

/*
In a coalesce-allocator, find the in-obj-tbl offset of a given obj address (p) 
*/
inline unsigned long get_coalesce_obj_offset(vaddr_range_t *vr, void* p)
{
    unsigned long dist = (unsigned long)(p - vr->start);
    return dist/vr->coalesce.obj_size;
}


inline obj_item_t* get_coalesce_obj_item(vaddr_range_t* vr, void* p, void** retaddr)
{
    unsigned long offset = get_coalesce_obj_offset(vr, p);
    //obj_item_t *om = &(vr->coalesce.obj_table[get_coalesce_obj_offset(vr, p)]);
    obj_item_t *om = &(vr->coalesce.obj_table[offset]);
    *retaddr = (void*)om;
    return om;
}

inline int get_coalesce_obj_state(vaddr_range_t *vr, void *p)
{
    obj_item_t*  oit;
    get_coalesce_obj_item(vr, p, &oit);
    return oit->state;  
}

inline void set_coalesce_obj_state(vaddr_range_t *vr, void* p, int state)
{
    obj_item_t*  oit;
    get_coalesce_obj_item(vr, p, &oit);
    oit->state = state;
}

inline unsigned long get_coalesce_obj_ssd_pos(vaddr_range_t *vr, void* p, size_t *retaddr )
{
    //obj_item_t *om = get_coalesce_obj_item(vr, p);
    obj_item_t *om;
    get_coalesce_obj_item(vr, p, &om);
    unsigned long t;

    t = (unsigned long)(om->ssd_pos_msb);
    t = (t<<32);
    t |= (unsigned long)(om->ssd_pos);
    
    *retaddr = t;
    return t;
}

inline void set_coalesce_obj_ssd_pos(vaddr_range_t *vr, void* p, unsigned long position)
{
    obj_item_t *om; //
    get_coalesce_obj_item(vr, p, &om);
    unsigned int t = (unsigned int)(position>>32);
    om->ssd_pos_msb = t;
    om->ssd_pos = (unsigned int)(position);
}


inline void get_coalesce_obj_vaddr(vaddr_range_t *vr, void* p, void**retaddr)
{
    unsigned long off = get_coalesce_obj_offset(vr, p);
    *retaddr = (vr->start + vr->coalesce.obj_size*off);
}


/*
grab a chunk of vaddr from the coal-vaddr-range.
If returning a void* directly,  the resulting high 4 bytes of address are always wrong 
at caller side. So let caller pass the address of result buf, and fill in this result
at callee. 
*/
int coalesce_alloc(size_t size, vaddr_range_t *vr, void** retaddr)
{
    assert( vr->type == allocator_coalesce );
    assert( vr->coalesce.vaddr_unit_size >= size );

    void *p = NULL;
    size_t i;

    ///// 1. alloc a virt-addr unit
    if( vr->coalesce.num_free_objs > 0 ){
        p = vr->coalesce.free_list[--vr->coalesce.num_free_objs];
    }
    else if( vr->free_length >= vr->coalesce.vaddr_unit_size ){
        p = vr->curr_vaddr;
        vr->curr_vaddr += vr->coalesce.vaddr_unit_size;
        vr->free_length -= vr->coalesce.vaddr_unit_size;
    }
    else{
        dbg("no vaddr avail at vaddr-range: %ld...\n", vr->start);
        return -1;
    }

    if(mprotect(p, vr->coalesce.vaddr_unit_size, PROT_NONE) < 0){
        err("fail to mprotect...\n");
        coalesce_free( vr, p );
        return -1;
    }
   

    //////// 2. alloc ram in slab-cache:  
    /// note::  this is not necessary, since at sigseg handler, an item is grabbed from slabcache.
    /// so an in-ram item alloced at that time is enough.
    /*item *it=NULL;
    it = hb_item_alloc(p, sizeof(p), datasize);
    it->vaddr_range = vr;
    */

    //////  3. mark the state of all corresponding obj-tbl slots    
    size_t numobj = vr->coalesce.vaddr_unit_size / vr->coalesce.obj_size;
    for(i=0; i<numobj; i++)
        set_coalesce_obj_state(vr, p+i*vr->coalesce.obj_size, STATE_ALLOC_EMPTY);
        //set_coalesce_obj_state(vr, p+i*vr->coalesce.obj_size, STATE_ALLOC_DIRTY);

    *retaddr = p; 
    return 0;
}


/*
release a chunk of vaddr  to the coalesce-allocator.
Should already hold a lock on this vaddr-range.
*/
int coalesce_free(vaddr_range_t *vr, void* p)
{
    size_t i;

    assert( vr->type == allocator_coalesce );

    //// 1. madvise to release the physical ram
    madvise(p, vr->coalesce.vaddr_unit_size, MADV_DONTNEED);


    //// 2. return this addr to free-list
    if(vr->coalesce.num_free_objs == vr->coalesce.free_list_size)
    {
        int newsize = (vr->coalesce.free_list_size>10000) ? vr->coalesce.free_list_size+10000 :
            vr->coalesce.free_list_size * 2;
        void** newlist = realloc( vr->coalesce.free_list, newsize*sizeof(void*) );
        if( !newlist ){
            err("fail to resize free-list to %ld...\n", newsize);
            return -1;
        }
        vr->coalesce.free_list = newlist;
        vr->coalesce.free_list_size = newsize;
    }

    vr->coalesce.free_list[vr->coalesce.num_free_objs++] = p;

    //// 3.  release the slab-cache
    size_t numobj = vr->coalesce.vaddr_unit_size / vr->coalesce.obj_size;   
    void *tp = p;
    for(i=0; i<numobj; i++){
        hb_item_delete_by_key(tp, sizeof(tp));
        tp+= vr->coalesce.obj_size;
    }

    /// 4. Mark the state of this obj as free in the obj-table    
    for(i=0; i<numobj; i++){
        set_coalesce_obj_state(vr, p+i*vr->coalesce.obj_size, STATE_FREE);
    }
    return 0;
}


/////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////      pool allocator



int add_range_to_pool(vaddr_range_t* vr, vaddr_pool_list_t* pools, int poolid)
{
    list_add( &vr->range_list, &pools->pool_array[poolid].head );
    pools->pool_array[poolid].num_ranges++;

    return 0;
}


int del_range_from_pool(vaddr_range_t* vr, vaddr_pool_list_t* pools, int poolid)
{
    // del this range from the list of ranges in the enclosing pool.
    list_del( &vr->range_list ); 
    pools->pool_array[poolid].num_ranges--;

    return 0;
}

int init_pool_alloc_array(vaddr_pool_list_t* pools, int minobjsize, int maxobjsize, double factor)
{
    int i;
    size_t size = minobjsize;

    i = POWER_SMALLEST;
    while( i<POWER_LARGEST && size< maxobjsize/factor )
    {
        if (size % CHUNK_ALIGN_BYTES)
            size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
        
        pools->objsize[i] = size;

        if( size < PGSIZE )
            pools->vaddr_unitsize[i] = PGSIZE;
        else
            pools->vaddr_unitsize[i] = (size + PGSIZE-1) & (~(PGSIZE-1));

        pools->pool_array[i].num_ranges = 0;
        INIT_LIST_HEAD( &pools->pool_array[i].head );
        pthread_mutex_init( &pools->pool_array[i].lock, NULL );

        dbg("pool %2d: objsize = %ld, unitsize=%ld\n", 
            i, pools->objsize[i], pools->vaddr_unitsize[i] );

        size *= factor;    
        i++;
    } 
    if( i<POWER_LARGEST)
    {
        size = maxobjsize;
        if (size % CHUNK_ALIGN_BYTES)
            size += CHUNK_ALIGN_BYTES - (size % CHUNK_ALIGN_BYTES);
        
        pools->objsize[i] = size;

        if( size < PGSIZE )
            pools->vaddr_unitsize[i] = PGSIZE;
        else
            pools->vaddr_unitsize[i] = (size + PGSIZE-1) & (~(PGSIZE-1));

        pools->pool_array[i].num_ranges = 0;
        INIT_LIST_HEAD( &pools->pool_array[i].head );
        pthread_mutex_init( &pools->pool_array[i].lock, NULL );

        dbg("pool %2d: objsize = %ld, unitsize=%ld\n", 
            i, pools->objsize[i], pools->vaddr_unitsize[i] );

        i++;
    } 

    pools->num_pools = i;
    
    return 0;
}

int destroy_pool_alloc_array(vaddr_pool_list_t *pools)
{

    int i;
    vaddr_range_list_t  *rlist;
    struct list_head *pos, *n;
    vaddr_range_t *vr;

    for(i=POWER_SMALLEST; i < pools->num_pools; i++)
    {
        rlist = &pools->pool_array[i];
  
        list_for_each_safe(pos, n, &rlist->head )
        {
            vr = list_entry(pos, vaddr_range_t, range_list);
            dbg("will destroy pool-alloc:: \n");
            del_range_from_pool(vr, pools, i);
            destroy_pool_allocator(vr);
        } 

    } 
    return 0;
}



int find_pool_id(int objsize, vaddr_pool_list_t* pools)
{
    int i=POWER_SMALLEST;

    while( i<pools->num_pools && objsize > pools->objsize[i] )
        i++;

    if( i<pools->num_pools )
        return i;
    
    return 0;
}



/*
Create a pool-allocator for obj size (objsize).
*/
vaddr_range_t* create_pool_allocator(int objsize)
{
    vaddr_range_t   *vr = NULL;
    unsigned long len; // 1G
    unsigned long i;
    int poolid = 0;

    poolid = find_pool_id(objsize, &pList);
    if( poolid < 1 ){
        err_exit("objsize not match: %d\n", objsize);
    }

    /// 0. get v-range itself
    vr = malloc(sizeof(vaddr_range_t));
    if( !vr ){
        err("alloc vaddr-range failed\n");
        return NULL;
    } 
    memset(vr, 0, sizeof(vaddr_range_t));
    INIT_LIST_HEAD( &vr->range_list );
    pthread_mutex_init( &vr->lock, NULL );
    vr->vrlist = &pList.pool_array[poolid];

    /// 1. alloc virtual-address range, and mprotect
    len = VADDR_RANGE_SIZE; // 1G
    vr->start = memalign(PGSIZE, len);
    if( !vr->start ){
        err("fail to alloc: %ld\n", len);
        goto err_out_1;
    }
    
    if(mprotect (vr->start, len, PROT_NONE) != 0){
        err("unable to mprotect: %p, len=%ld\n", vr->start, len);
        goto err_out_2;
    }

    dbg("has alloc and mprotected:  vaddr=%p, len=%ld\n", vr->start, len);

    vr->length = len;
    vr->curr_vaddr = vr->start;
    vr->free_length = len;

    vr->type = allocator_pool;

    vr->pool.obj_size = pList.objsize[poolid]; //objsize;
    vr->pool.vaddr_unit_size = pList.vaddr_unitsize[poolid];

    // init the search tree node
    vr->treenode.address = (unsigned long)(vr->start);
    vr->treenode.len = len;

    /*if( objsize < PGSIZE )
        vr->pool.vaddr_unit_size = PGSIZE;
    else
        vr->pool.vaddr_unit_size = (objsize + PGSIZE-1) & (~(PGSIZE-1));
    */

    /// 2. prepare obj-table for this vaddr-range

    len = vr->length / vr->pool.vaddr_unit_size; // max num of objs in this range

    vr->pool.obj_table = malloc( len *sizeof(obj_item_t) );
    if( !vr->pool.obj_table ){
        err("fail to alloc obj-tbl: %ld objs, obj-size=%ld, unit-size=%ld\n",
            len, vr->pool.obj_size, vr->pool.vaddr_unit_size  );
        goto err_out_3;
    }

    dbg("has alloc obj-tbl: %ld objs, obj-size=%ld, unit-size=%ld\n",
        len, vr->pool.obj_size, vr->pool.vaddr_unit_size  );
    
    vr->pool.obj_table_size = len;
    for(i=0; i<len; i++){
        vr->pool.obj_table[i].state = STATE_FREE;
    }

    //// 3. prepare free-obj-list
    vr->pool.free_list = malloc(32*sizeof(void*));
    vr->pool.free_list_size = 32;
    vr->pool.num_free_objs = 0;

    //// 4.1  link this range to the pool
    add_range_to_pool(vr, &pList, poolid);
   
    /// 4.2   Insert this range to the BST. 
    insert( &range_tree, &vr->treenode );

    return vr;


////    err  out:

err_out_3:
err_out_2:
    free(vr->start);

err_out_1:
    free(vr);


    return NULL;
}


/*
release a pool allocator.
*/
int destroy_pool_allocator(vaddr_range_t *vr)
{
    printf("*** will destroy pool: \n");
    dump_allocator(vr);

    if( vr->type != allocator_pool ){
        err("alloc not a pool allocator\n");
        return -1;
    }
    
    /// take it out of the search tree...
    delete(&range_tree, &vr->treenode);     

    /// free the v-address
    if( vr->type == allocator_pool )
    {

        if( vr->pool.free_list )
            free(vr->pool.free_list);
        if( vr->pool.obj_table )
            free(vr->pool.obj_table);
    }
    else
    {
        err("Not supported right now!!!!\n");
    }

    if( vr->start )
        free(vr->start);
    
    /// TODO:: take it out of the avl-tree...


    ////////////
    free(vr);

    return 0;
}


/*
In a pool-allocator, find the offset of a given obj address (p) 
in its object table.
*/
inline unsigned long get_pool_obj_offset(vaddr_range_t *vr, void* p)
{
    unsigned long dist = (unsigned long)(p - vr->start);

    return dist/vr->pool.vaddr_unit_size;

}

inline obj_item_t* get_pool_obj_item(vaddr_range_t* vr, void* p)
{
    return &(vr->pool.obj_table[get_pool_obj_offset(vr, p)]);
}

inline int get_pool_obj_state(vaddr_range_t *vr, void *p)
{
    unsigned long off = get_pool_obj_offset(vr, p);
    return vr->pool.obj_table[off].state;
}

inline void set_pool_obj_state(vaddr_range_t *vr, void* p, int state)
{
    unsigned long off = get_pool_obj_offset(vr, p);
    vr->pool.obj_table[off].state = state;
}

inline unsigned long get_pool_obj_ssd_pos(vaddr_range_t *vr, void* p, size_t *retaddr)
{
    obj_item_t *om = get_pool_obj_item(vr, p);
    unsigned long t;

    t = (unsigned long)(om->ssd_pos_msb);
    t = (t<<32);
    t |= (unsigned long)(om->ssd_pos);
    
    *retaddr = t;

    return t;
}


inline void set_pool_obj_ssd_pos(vaddr_range_t *vr, void* p, unsigned long position)
{
    obj_item_t *om = get_pool_obj_item(vr, p);
    unsigned int t = (unsigned int)(position>>32);
    om->ssd_pos_msb = t;
    om->ssd_pos = (unsigned int)(position);
}

inline int get_obj_state(void* p)
{
    avl_node_t* node = find( &range_tree, (unsigned long)p );
    vaddr_range_t  *vr = container_of(node, vaddr_range_t, treenode);
    ////// now, only work with pool allocator...
    assert( vr->type == allocator_pool );

    return get_pool_obj_state(vr, p); 
}

inline void set_obj_state(void *p, int st)
{
    avl_node_t* node = find( &range_tree, (unsigned long)p );
    vaddr_range_t  *vr = container_of(node, vaddr_range_t, treenode);
    ////// now, only work with pool allocator...
    assert( vr->type == allocator_pool );

    set_pool_obj_state(vr, p, st);
}

/*
When a (void *) is needed to be returned, pass it in func parameters.
*/
inline void get_pool_obj_vaddr(vaddr_range_t *vr, void* p, void** retaddr)
{
    unsigned long off = get_pool_obj_offset(vr, p);

    *retaddr = (vr->start + (vr->pool.vaddr_unit_size)*off);
}


/*
grab a free vaddr-slot from the vaddr-range.
If returning a void* directly,  the result high 4 bytes of address are always wrong 
at caller side. So let caller pass the address of result buf, and fill in this result
at callee. 
*/
int pool_alloc(int datasize, vaddr_range_t *vr, void** retaddr)
{
    assert( vr->type == allocator_pool );

    void *p = NULL;
    size_t i;

    ///// 1. alloc a virt-addr unit
    if( vr->pool.num_free_objs > 0 ){
        p = vr->pool.free_list[--vr->pool.num_free_objs];
    }
    else if( vr->free_length >= vr->pool.vaddr_unit_size ){
        p = vr->curr_vaddr;
        vr->curr_vaddr += vr->pool.vaddr_unit_size;
        vr->free_length -= vr->pool.vaddr_unit_size;
    }
    else{
        dbg("no vaddr avail at vaddr-range: %ld...\n", vr->start);
        //dump_allocator(vr);
        return -1;
    }

    if(mprotect(p, vr->pool.vaddr_unit_size, PROT_NONE) < 0){
        err("fail to mprotect...\n");
        pool_free( vr, p );
        return -1;
    }
   

    //////// 2. alloc ram in slab-cache:  
    /// note::  this is not necessary, since at sigseg handler, an item is grabbed from slabcache.
    /*item *it=NULL;
    it = hb_item_alloc(p, sizeof(p), datasize);
    it->vaddr_range = vr;
    */
    //////  3. mark the state of this new obj in the obj-table    
    //set_pool_obj_state(vr, p, STATE_ALLOC_DIRTY);
    set_pool_obj_state(vr, p, STATE_ALLOC_EMPTY);

    *retaddr = p; 
    return 0;
}


/*
Return a slot of vaddr to the pool allocator.
*/
int pool_free(vaddr_range_t *vr, void* p)
{
    assert( vr->type == allocator_pool );

    /*
    unsigned long ap = (unsigned long)(p + vr->pool.vaddr_unit_size-1) & 
            (~(vr->pool.vaddr_unit_size-1));
    if( ap != (unsigned long)p ){
        err_exit("free addr=%p, not aligned to unit-size=%ld\n", 
            p, vr->pool.vaddr_unit_size);
    }
    */

    //// 1. madvise to release the physical ram
    madvise(p, vr->pool.vaddr_unit_size, MADV_DONTNEED);


    //// 2. return this addr to free-list
    if(vr->pool.num_free_objs == vr->pool.free_list_size)
    {
        int newsize = (vr->pool.free_list_size>10000) ? vr->pool.free_list_size+10000 :
            vr->pool.free_list_size * 2;
        void** newlist = realloc( vr->pool.free_list, newsize*sizeof(void*) );
        if( !newlist ){
            err("fail to resize free-list to %ld...\n", newsize);
            return -1;
        }
        vr->pool.free_list = newlist;
        vr->pool.free_list_size = newsize;
    }

    vr->pool.free_list[vr->pool.num_free_objs++] = p;

    //// 3.  release the slab-cache
    hb_item_delete_by_key(p, sizeof(p));

    /// 4. Mark the state of this obj as free in the obj-table    
    set_pool_obj_state(vr, p, STATE_FREE);
    return 0;
}





/*
Alloc objects from hybrid-mem. 
If numobj==1,  use pool-allocator.
Else, use coalescing allocator.

*/
int hbmem_alloc_object(size_t objsize, size_t numobj, void **retaddr)
{
    int i = 0;
    vaddr_range_list_t  *rlist;
    struct list_head *pos, *n;
    vaddr_range_t *vr;

    size_t  totalsize = objsize * numobj; // alloc this much vaddr in total

    if( objsize > max_obj_size ){
        objsize = max_obj_size;
    }

    //////////////////////////////////////////////////
    /////////  use coalesce-allocator
    if( numobj > 1 || totalsize>max_obj_size )
    {
        int row, col;
        if(get_coal_alloc_index(&coalList, totalsize, objsize, &row, &col)!=0 )
        {
            err("no coal-alloc fits with request: total=%ld, objsize=%ld\n",
                totalsize, objsize );
            *retaddr = NULL;
            return -1;
        }
        dbg("will use coal-array[%d][%d]\n", row, col);

        rlist = &coalList.coalesce_array[row][col];

        pthread_mutex_lock( &rlist->lock );

        list_for_each_safe(pos, n, &rlist->head )
        {
            vr = list_entry(pos, vaddr_range_t, range_list);
            if( coalesce_alloc(totalsize, vr, retaddr) == 0 )
            {
                pthread_mutex_unlock( &rlist->lock );
                return 0;
            }
        } 
        /// no vaddr-range, or all virt-ranges are full. create a new range.
        vr = create_coalesce_allocator( coalList.coalesce_sizes[row], 
           coalList.obj_sizes[col], rlist );

        dump_allocator(vr);

        i = coalesce_alloc(totalsize, vr, retaddr);
        pthread_mutex_unlock( &rlist->lock );
        return i;
    }

    //////////////////////////////////////////////////
    /////////  use pool-allocator

    int plid = find_pool_id( objsize, &pList );
    int nsize; // adjusted obj size, the obj-size of target pool-allocator
 
    rlist = &pList.pool_array[plid];
    nsize = pList.objsize[plid];

    dbg("\n\n\n******\n");
    dbg("Alloc obj:  usize=%d, obj-size=%d at pool-cls %d\n", 
            objsize, nsize, plid ); 
    pthread_mutex_lock( &rlist->lock );
    list_for_each_safe(pos, n, &rlist->head )
    {
        vr = list_entry(pos, vaddr_range_t, range_list);
        nsize = vr->pool.obj_size;
        dbg("will alloc from vr-%d...\n", i++);
        if( pool_alloc(nsize, vr, retaddr) == 0 ){
            pthread_mutex_unlock( &rlist->lock );
            return 0;
        }
    } 
    /// no vaddr-range, or all virt-ranges are full. create a new range.
    vr = create_pool_allocator(nsize);
    i = pool_alloc(nsize, vr, retaddr);

    pthread_mutex_unlock( &rlist->lock );
    return i;
}


/*
Release the virt-address and associated RAM to hybrid mem.
*/
int hbmem_free(void *p)
{
    avl_node_t* node = find( &range_tree, (unsigned long)p );
    vaddr_range_t  *vr = container_of(node, vaddr_range_t, treenode);

    pthread_mutex_lock( &vr->vrlist->lock );
    if( vr->type == allocator_coalesce )
    {
        coalesce_free(vr, p); 
    }
    else
    {
        //assert(vr->type ==allocator_pool );
        pool_free(vr, p);            

    }
    pthread_mutex_unlock( &vr->vrlist->lock );
    return 0;
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////



int init_vaddr(char* ssddir)
{
    unsigned long memlimit;
    unsigned long minsize, maxsize;
    double factor;
    
    int hsh_tbl_bits = 20; // initial hash-tbl size = 2^20 
    factor = 1.3;

    size_t  ramqSize = 1024UL*1024; //1024;//1024*20; //1024*5;  // ram-q (pg-buf) size)
    memlimit = 1024UL*1024*32; //1024*32; //1024*40; //1024*20; //1024*500;
    minsize = 128;
    maxsize = 1024UL*1024; //1024; //1024;
    settings.item_size_max = maxsize;  // size of a slab chunk.

    /*
    size_t  ramqSize = 1024UL*1024*20; //1024*5;  // ram-q (pg-buf) size)
    memlimit = 1024UL*1024*40; //1024*20; //1024*500;
    minsize = 128;
    maxsize = 1024UL*1024; //1024;
    settings.item_size_max = maxsize;  // size of a slab chunk.
    */

    ///////// init the hybrid-mem slab-allocator, use memcached's settings
    /*************
    pre-alloc mem for slabs and mlock the slab-RAM, so that later access 
    to the slab RAM won't be trapped into sigseg handler, 
    which is meant for ssd-alloc.
    **************/
    //sprintf(HB_DIR, "/tmp/fio/ouyangx"); 
    char    cmd[1024];
    sprintf(HB_DIR, ssddir); 
    sprintf(cmd, "mkdir -p %s", ssddir);    
    system(cmd); 
    sb_hbmem = slabs_init( memlimit, minsize, maxsize, "hybrid-mem", 
            factor, 1, 1, 0 );  // pre-alloc RAM, use hybrid-mem
    /// init the hash-lookup table
    assoc_init(hsh_tbl_bits); 
    sleep(1); // wait for hash maintenance thread to init.

    //////// prepare the search tree
    avl_init(&range_tree);


    max_obj_size = maxsize; // an in-ram obj cannot exceed this size

    // init the list of pool allocators
    init_pool_alloc_array(&pList, minsize, maxsize, factor);
    dump_pool_list(&pList);

    init_coal_alloc_array( &coalList );
    dump_coalesce_list(&coalList);   

    // init the sigsegv handler
    if( init_sigseg() != 0 ){
        err("fail to init sigseg\n");
        goto err_out_1;
    }

    /// init the RAM-usage queue
    start_ram_reclaim(ramqSize,4096UL);
    
    return 0;

err_out_1:
    return -1;

}


int destroy_vaddr()
{
    /// destroy hash table
    assoc_destroy();

    /// destroy slabcache
    slabs_destroy(sb_hbmem);
    
    /// TODO:: restore the sigsegv ? 
    stop_ram_reclaim();

    /// release all pool-allocators   
    printf("\n========= will destroy pool-list and coal-list:: \n");
    //dump_pool_list(&pList);
    destroy_pool_alloc_array(&pList);

    destroy_coal_alloc_array( &coalList );  

    /// release the search tree
    avl_destroy(&range_tree);

    return 0;
}







