/* slabs memory allocation */
#ifndef SLABS_H
#define SLABS_H


#include "myhmem.h"


/* powers-of-N allocation structures */

#define		MAX_HB_FILENAME	(256)

typedef struct {
	pthread_mutex_t lock; //  = PTHREAD_MUTEX_INITIALIZER;
	
    unsigned int size;      /* sizes of items */
    unsigned int perslab;   /* how many items per slab */

    void **slots;           /* list of item ptrs */
    unsigned int sl_total;  /* size of previous array */
    unsigned int sl_curr;   /* first free slot */

    void *end_page_ptr;         /* pointer to next free item at end of page, or 0 */
    unsigned int end_page_free; /* number of items remaining at end of last alloced page */

    unsigned int slabs;     /* how many slabs were allocated for this class */

    void **slab_list;       /* array of slab pointers */
    unsigned int list_size; /* size of prev array */

    unsigned int killing;  /* index+1 of dying slab, or zero if none */
    size_t requested; /* The number of requested bytes */
    
    //// evict some mem-items to SSD

    pthread_rwlock_t  rwlock;  // to sync access to the ssd-file
 
    char	hb_filename[MAX_HB_FILENAME]; 
    int 	hb_fd;	// file descriptor for the hybrid-mem of this slab-class
    long	hb_fdoffset;	// current offset into the fd to write
    
    unsigned char	*cbuf;	// coalesce mem-items into a buf to write to SSD in whole chunk. 
    	// cbuf must be page-aligned to perform direct-IO
    long	cbuf_size;	// size of the cbuf
    
    //long	inmem_items;  // items of this slabclass that sit in mem
    //long	indisk_items; // items of this slabclass that are stored in mem
    
} slabclass_t;



#define	MAX_SLABNAME (32)

typedef struct
{
	slabclass_t slabclass[MAX_NUMBER_OF_SLAB_CLASSES];
	int power_largest;  // how many valid slabs in the slab-class array
	
	//Access to the slab allocator is protected by this lock
	pthread_mutex_t slabs_lock; //  = PTHREAD_MUTEX_INITIALIZER;

	char	name[MAX_SLABNAME];   // name of this slab
	
	size_t mem_limit;   // max total mem-size can be used by this slabs-t
	size_t mem_malloced;  // actual mem-size used by this slabs-t

	///////////////////////////
	item*	inmem_item[MAX_NUMBER_OF_SLAB_CLASSES]; // the last in-mem item in the LRU list
	item*	heads[MAX_NUMBER_OF_SLAB_CLASSES];
	item*	tails[MAX_NUMBER_OF_SLAB_CLASSES];

	//////// if use pre-alloc-slabs,  then all mem is recorded below::	
	
	// in pre-alloc, the mem_based is acquired by mmap()
	int  use_mmap; 
	int  mmap_fd; 
	char mmap_filename[MAX_HB_FILENAME];
	
	void *mem_base;  
	void *mem_current;
	size_t mem_avail;

} slabs_t;



/** Init the subsystem. 1st argument is the limit on no. of bytes to allocate,
    0 if no limit. 2nd argument is the growth factor; each slab will use a chunk
    size equal to the previous slab's chunk size times this factor.
    3rd argument specifies if the slab allocator should allocate all memory
    up front (if true), or allocate memory in chunks as it is needed (if false)
*/
//slabs_t* slabs_init(const size_t limit, const double factor, const bool prealloc);

slabs_t* slabs_init(const size_t limit, size_t min_chunksize, size_t max_chunksize, 
       char* name, const double factor, bool prealloc, int use_hybrid, int use_mmap );
        
int	slabs_destroy(slabs_t* slab);

int slabs_msync(slabs_t* sb);

/**
 * Given object size, return id to use when allocating/freeing memory for object
 * 0 means error: can't store such a large object
 */

unsigned int slabs_clsid(const size_t size, slabs_t* sb);

void*  myslabs_alloc(size_t size, slabs_t* sb);
void    myslabs_free(void* ptr, size_t size, slabs_t* sb);

/** Allocate object of given length. 0 on error */ /*@null@*/
void *slabs_alloc(const size_t size, unsigned int id, slabs_t* sb);

/** Free previously allocated object */
void slabs_free(void *ptr, size_t size, unsigned int id, slabs_t* sb);

/** Return a datum for stats in binary protocol */
bool get_stats(const char *stat_type, int nkey, ADD_STAT add_stats, void *c);

/** Fill buffer with stats */ /*@null@*/
void slabs_stats(ADD_STAT add_stats, void *c);

extern char HB_DIR[1024];

#endif
