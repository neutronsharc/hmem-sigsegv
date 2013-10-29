/*
This header file contains definitions of constants/data structs to be used in the 
virtual-address access interface to be used with hybrid-mem.

Author: Xiangyong Ouyang (neutronsharc@gmail.com)

v0.1:	2012-04-30

*/

#ifndef __VADDR_H__
#define __VADDR_H__

#include <pthread.h>
#include <semaphore.h>

#include "list.h"

#include "slabs.h"
#include "myhmem.h" 
#include "avl.h"


/*
state of a vaddr-unit in the vaddr-range.
*/
#define STATE_FREE 0  // the vaddr-unit is free to be allocated
#define STATE_ALLOC_EMPTY 1 // the vaddr-unit is allocated, but no data in it. no copy in ssd.
#define STATE_ALLOC_DIRTY 2 // vaddr-unit allocated, has been written to, but not saved to ssd
#define STATE_ALLOC_CLEAN 3 // data in this vaddr-unit has been saved to ssd

#define VADDR_RANGE_SIZE    (1024UL*1024*64)
#define PGSIZE  (4096UL)

enum
{
    allocator_pool = 0,
    allocator_coalesce = 1,
};


/*

*/
typedef struct
{
    unsigned int state       : 3;

    unsigned int pgbuf_dirty : 1;  // pg-buf ram contains dirty data?

    unsigned int ssd_pos_msb : 12; //high bits of position in SSD
    unsigned int ssd_pos     : 32;
}__attribute__((__packed__)) obj_item_t; 

/*typedef union
{
    unsigned char   dummy[6]; // 40 bits
    typedef struct
    {
        unsigned int state       : 4;
        unsigned int ssd_pos_msb : 12;
        unsigned int ssd_pos     : 32;
    }__attribute__((__packed__)) item_state;

#define state  item_state.state
#define ssd_pos_msb  item_state.ssd_pos_msb
#define ssd_pos item_state.ssd_pos

} obj_item_t;
*/

struct vaddr_range_list_s;

typedef struct vaddr_range_s
{
    struct list_head  range_list; // link to next range with the same obj-size
    pthread_mutex_t  lock;

    void    *start;  // beginning of the vaddr-range
    unsigned long length;  // length of this range

    void    *curr_vaddr; // alloc from this pos in the [start, start + length) range
    unsigned long free_length; //

    int type;   // possibe type:  pool-allocator, coalescing allocator
    struct vaddr_range_list_s*  vrlist;  // which vr-list this range belongs to.

    union 
    {
    // for pool-allocator
    struct 
    { 
        size_t obj_size;  // in-ram obj data size, used by pool-allocator, can be any arbitrary size 
        size_t vaddr_unit_size; // alloc vaddr in this unit. Aligned to pg size(4kB)

        obj_item_t *obj_table;
        size_t obj_table_size; // max num of objs in the obj-table 

        void **free_list;  // an array containing all free obj addr
        size_t free_list_size; // size of the array
        size_t num_free_objs; //  num of free objs in the free_list
        
    }pool;

    // for coalescing allocator
    struct
    {
        size_t obj_size;  // in-ram obj unit size, 
        	// also size of one item in obj-table. Must be multiples of pgsize (4kB)

        size_t vaddr_unit_size;  // total size of a ssd-alloc(num-objs,  size-of one obj)
        	// == num_objs * size-of-one-obj.
        	// allocs this much vaddr range, <= vaddr-range-length
        
        obj_item_t *obj_table;
        size_t obj_table_size; // max num of obj-units in the obj-table,
		// == (vaddr-range-length / obj_unit_size)

        void **free_list;  // an array containing all free obj addr
        size_t free_list_size; // size of the array
        size_t num_free_objs; //  num of free vaddr-units in the free_list

    }coalesce;

    }; // end of union

    ///////////////////// link in the AVL search tree
    avl_node_t      treenode;

} vaddr_range_t;


/*
All vaddr-ranges of the same obj-size are linked in this list
*/
typedef struct vaddr_range_list_s
{
    pthread_mutex_t  lock; // sync access to this list
    
    int num_ranges; // how many vaddr-ranges in this pool
    
    struct list_head  head; // head pointer of the list
    
}vaddr_range_list_t; // 


/*
Gather all pool list into a struct
*/
typedef struct
{
    size_t  objsize[MAX_NUMBER_OF_SLAB_CLASSES];
    size_t  vaddr_unitsize[MAX_NUMBER_OF_SLAB_CLASSES];
    vaddr_range_list_t   pool_array[MAX_NUMBER_OF_SLAB_CLASSES];

    int  num_pools; // num of valid pool allocs in this array

}vaddr_pool_list_t; // 




#define MAX_IN_RAM_UNIT_SIZE	 (1024UL*64) // in-ram unit <= this size, also read/write unit size to file
#define MAX_COALESCE_ALLOC_SIZE  ( 1024UL*1024*1024*1024*2 ) // a malloc must be <= this size

/*
coalescing allocator:
each coalescing alloc handles a combination of:  (total-alloc-size, in-mem obj-unit size).
All coalesce-alloc are put in a 2-d array:

Row:  vaddr-alloc unit size, from (pgsize) to MAX_COALESCE_ALLOC_SIZE
Col:  in-mem unit size, from (pgsize) to MAX_IN_RAM_UNIT_SIZE
*/
typedef struct
{
	int num_coalesce;  // row
	int num_objsize;   // column
	
	size_t *coalesce_sizes; // array, each elem is coalesce-vaddr size
	size_t *obj_sizes;  // array, each elem is a basic in-slabcache obj size
	
	vaddr_range_list_t  **coalesce_array; // 2-d array of coal-allocs
	
}vaddr_coalesce_list_t; //


//////////////////////////// release RAM instantiated by sigsegv.

//#define MAX_RAM_USAGE_SIGSEGV   (1024UL*1024) //1024*8)
// allow this much RAM used by vaddr-units materialized by sigsegv.

//#define RAM_LIST_SIZE_SIGSEGV   (MAX_RAM_USAGE_SIGSEGV/PGSIZE * 1)
//#define RAM_LIST_SIZE_THRESH    ((unsigned long)(RAM_LIST_SIZE_SIGSEGV * 1))
/*
Sigsegv handler keeps materializing vaddr-units into RAM.  
At huge num of vaddr-units, the RAM usage will eventually exceed avail mem size.
When the materialized units hit a certain limit, we must release those RAM units
to work with huge number of vaddr-units.
*/
typedef struct
{
    void *address;
    size_t  length; 
    vaddr_range_t   *vrange; // the vaddr-range this address belong to
    item  *slabitem;  // the item in slabcache
    
}mem_unit_t;

typedef struct 
{
    pthread_mutex_t ram_lock;

    mem_unit_t *ram_list;   // array of materialized ram-units (pg-size)
    size_t      list_size; // size of this array
    unsigned long  num_units; // num of elems in this array

    unsigned long  head;  // head ptr
    unsigned long  tail;  // tail ptr

    /// stats
    unsigned long  num_release;  // count how many RAM-objs has been released
    unsigned long  size_release; // cumulative RAM-unit size been released

    sem_t   ram_sem;
    
}mem_queue_t;


//////////////
extern vaddr_pool_list_t   pList;
extern avl_tree_t  range_tree;
extern mem_queue_t     memQ;

extern slabs_t*  sb_hbmem;

/////////////////////////

void    dump_ram_queue(mem_queue_t *q);
//inline void enqueue_ram_queue(void *addr, size_t sz, vaddr_range_t* vrange, mem_queue_t *q);
inline void enqueue_ram_queue(void *addr, size_t sz, item* slabitem, vaddr_range_t* vrange, mem_queue_t *q);

int start_ram_reclaim(size_t ramq_size, size_t unitsize);
int stop_ram_reclaim();


char* state2string(int obj_state);

int find_pool_id(int objsize, vaddr_pool_list_t* pools);

inline void get_pool_obj_vaddr(vaddr_range_t *vr, void* p, void** retaddr);
inline unsigned long get_pool_obj_offset(vaddr_range_t *vr, void* p);

inline int get_pool_obj_state(vaddr_range_t *vr, void *p);
inline void set_pool_obj_state(vaddr_range_t *vr, void* p, int state);
inline int get_obj_state(void* p);
inline void set_obj_state(void *p, int state);
inline obj_item_t* get_pool_obj_item(vaddr_range_t* vr, void* p);

inline unsigned long get_pool_obj_ssd_pos(vaddr_range_t *vr, void* p, size_t *retaddr);
inline void set_pool_obj_ssd_pos(vaddr_range_t *vr, void* p, unsigned long position);


//////////////////////
int init_vaddr(char *ssddir);
int destory_vaddr();
int hbmem_alloc_object(size_t objsize, size_t  numobj, void **retaddr);
int hbmem_free(void *p);
//////////////////////////////////


#endif //  __VADDR_H__ 
