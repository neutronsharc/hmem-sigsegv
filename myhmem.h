#ifndef __MY_HMEM__
#define __MY_HMEM__




#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
//#include <event.h>
#include <netdb.h>
#include <pthread.h>
#include <unistd.h>

#include "debug.h"

#if HAVE_STDBOOL_H
#include <stdbool.h>
#else
#define bool char
#define false 0
#define true 1
#endif 



#define DONT_PREALLOC_SLABS   // don't prealloc mem for each slab


/* Slab sizing definitions. */
#define POWER_SMALLEST 1
#define POWER_LARGEST  200
#define CHUNK_ALIGN_BYTES 8
#define MAX_NUMBER_OF_SLAB_CLASSES (POWER_LARGEST + 1)

#define LARGEST_ID POWER_LARGEST

#define KEY_MAX_LENGTH 250 

#define ENDIAN_LITTLE 1

/* status of an item */
#define ITEM_LINKED 1
#define ITEM_CAS 2
#define ITEM_SLABBED 4



/**  debug out print */
/*#if 0
  #define dbg(fmt, ...) do{ \
    fprintf(stderr, "[ %s ] : " fmt, __func__, __VA_ARGS__); fflush(stderr); } while(0)
#else
  #define  dbg(fmt, ...)
#endif

#define err(fmt, ...) do{ \
    fprintf(stderr, "Error!! [%s] : " fmt, __func__, __VA_ARGS__); fflush(stderr); } while(0)
*/
typedef unsigned int rel_time_t;


#define STAT_KEY_LEN 128
#define STAT_VAL_LEN 128




/**
 * Structure for storing items within memcached.
 */
typedef struct _stritem {
    struct _stritem *next;  // bi-direction link in LRU
    struct _stritem *prev;

    struct _stritem *h_next;  /// hash chain link

    void *vaddr_range; // which addr-range this key (address) belongs to.
    void *address; // virt-address is the key
    //uint8_t         nkey;       /// key length, w/terminating null and padding
    int  nbytes;     ///// size of data
    unsigned short  refcount;  // refcount of the data in this item

    ////////////////
    //rel_time_t      time;       //* least recent access
    //rel_time_t      exptime;    ///* expire time
    //uint8_t         nsuffix;    ////* length of flags-and-length string
    uint8_t     it_flags;   /// flags of the item hdr 
    uint8_t     slabs_clsid;//// which slab class we're in 

    uint8_t     is_dirty;   // the (*data) in this item is dirty 
    	            // and shall be flushed to SSD when being evicted

    void *end[]; // mem-addr where the real-data starts. 
    
} item;
//} __attribute__((__packed__)) item;


/* When adding a setting, be sure to update process_stat_settings */
/**
 * Globally accessible settings as derived from the commandline.
 */
struct settings {
    size_t maxbytes;
    int maxconns;
    int port;
    int udpport;
    char *inter;
    int verbose;
    rel_time_t oldest_live; /* ignore existing items older than this */
    int evict_to_free;
    char *socketpath;   /* path to unix socket if using local socket */
    int access;  /* access mask (a la chmod) for unix domain socket */
    double factor;          /* chunk size growth factor */
    int chunk_size;
    int num_threads;        /* number of worker (without dispatcher) libevent threads to run */
    char prefix_delimiter;  /* character that marks a key prefix (for stats) */
    int detail_enabled;     /* nonzero if we're collecting detailed stats */
    int reqs_per_event;     /* Maximum number of io to process on each
                               io-event. */
    bool use_cas;
    //enum protocol binding_protocol;
    int backlog;
    int item_size_max;        /* Maximum item size, and upper end for slabs */
    bool sasl;              /* SASL on/off */
};


/**
 * Global stats.
 */
struct stats {
    pthread_mutex_t mutex;
    unsigned int  curr_items;
    unsigned int  total_items;
    uint64_t      curr_bytes;
    unsigned int  curr_conns;
    unsigned int  total_conns;
    unsigned int  conn_structs;
    uint64_t      get_cmds;
    uint64_t      set_cmds;
    uint64_t      get_hits;
    uint64_t      get_misses;
    uint64_t      evictions;
    uint64_t      reclaimed;
    time_t        started;          /* when the process was started */
    bool          accepting_conns;  /* whether we are currently accepting */
    uint64_t      listen_disabled_num;
};



/**
 * Callback for any function producing stats.
 *
 * @param key the stat's key
 * @param klen length of the key
 * @param val the stat's value in an ascii form (e.g. text form of a number)
 * @param vlen length of the value
 * @parm cookie magic callback cookie
 */
typedef void (*ADD_STAT)(const char *key, const uint16_t klen,
                         const char *val, const uint32_t vlen,
                         const void *cookie);
                         


/* warning: don't use these macros with a function, as it evals its arg twice */
#define ITEM_get_cas(i) ((uint64_t)(((i)->it_flags & ITEM_CAS) ? \
                                    *(uint64_t*)&((i)->end[0]) : 0x0))
#define ITEM_set_cas(i,v) { if ((i)->it_flags & ITEM_CAS) { \
                          *(uint64_t*)&((i)->end[0]) = v; } }

//#define ITEM_key(item) ((unsigned long)((item)->address))
#define ITEM_key(item) ((item)->address)

//#define ITEM_key(item) (((char*)&((item)->end[0])) \
//         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_suffix(item) ((char*) &((item)->end[0]))

//#define ITEM_suffix(item) ((char*) &((item)->end[0]) \
//         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0) \
//         + (item)->nkey + 1 )


#define ITEM_data(item) ((char*) &((item)->end[0]))
//#define ITEM_data(item) ((char*)(item->data))

#define ITEM_ntotal(item) (sizeof(struct _stritem) + (item)->nbytes)

//#define ITEM_ntotal(item) (sizeof(struct _stritem) + (item)->nkey + 1 \
//         + (item)->nsuffix + (item)->nbytes \
//         + (((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0))

#define ITEM_hdrsize(item)  ( sizeof(struct _stritem) ) 

/*#define ITEM_hdrsize(item)  ( sizeof(struct _stritem)   \
         + ( ((item)->it_flags & ITEM_CAS) ? sizeof(uint64_t) : 0 ) \
         + (item)->nkey + 1 \
         + (item)->nsuffix )
  */       

/////////////    extern global vars

extern struct settings settings;
extern volatile rel_time_t current_time;
extern struct stats stats;  
extern pthread_mutex_t cache_lock;

#endif  // end of __MY_HMEM__
