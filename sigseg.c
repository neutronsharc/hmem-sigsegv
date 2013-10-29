
#define _GNU_SOURCE // this will open __USE_GNU, needed for ucontext_t
//#define __USE_GNU


#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>


#include <signal.h>
#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/mman.h>
#include <assert.h>

#include <sys/ucontext.h>

#include "debug.h"
#include "vaddr.h"


static void test_sigsegv_handler(int sig, siginfo_t *si, void *unused)
{
    //unsigned long va = (unsigned long)si->si_addr;
    void *va = si->si_addr;
  
    ucontext_t *u = (ucontext_t *)unused;
 
    unsigned char *pc = (unsigned char *)u->uc_mcontext.gregs[REG_RIP];

    int rwerror = u->uc_mcontext.gregs[REG_ERR]&2; 

    // u->uc_mcontext.gregs[REG_ERR]&2 == 0::  read-protect error
    // u->uc_mcontext.gregs[REG_ERR]&2 == 2::  write-protect error
     
    dbg("Got SIGSEGV at address: 0x%lx, pc=%p, rw=%d\n",
        (long) si->si_addr, pc, rwerror );
    
    if( !va ){    // invalide address, shall exit now ...
        err("sigsegv:  invalid address=%p\n", va);
        assert(0);
        return;
    }

    /////////////////   

    va = (void*)((unsigned long)va & ~((1UL<<12)-1));
    if( rwerror == 0 ) // read-access
    {
        if( mprotect( va, 4096, PROT_READ)!=0 ) // open read access
        {
            err("in sigsegv: read mprotect %p failed...\n", va);
            perror("mprotect error::  ");
            exit(0);
        }
    }
    else if( rwerror==2 ) // write-access, give write-prot, which implies read-prot 
    {
        if( mprotect( va, 4096, PROT_WRITE)!=0 ){
            err("in sigsegv: write mprotect %p failed...\n", va);
            perror("mprotect error::  ");
            exit(0);
        }
    }
}


/****************
Synchronous signals are a result of a program action.

SIGSEGV, segmentation violation, is a synchronous signal.
It is returned when the program tries to 
access an area of memory outside the area it can legally access.


Note:: 
sometime, sigseg fault happens at the vaddr of the slab-cache.
*****************/
static void sigsegv_handler(int sig, siginfo_t *si, void *unused)
{
    //unsigned long va = (unsigned long)si->si_addr;
    void *va = si->si_addr;
  
    ucontext_t *u = (ucontext_t *)unused;
 
    unsigned char *pc = (unsigned char *)u->uc_mcontext.gregs[REG_RIP];

    int rwerror = u->uc_mcontext.gregs[REG_ERR]&2; 

    // u->uc_mcontext.gregs[REG_ERR]&2 == 0::  read-protect error
    // u->uc_mcontext.gregs[REG_ERR]&2 == 2::  write-protect error
     
    dbg("\n\nGot SIGSEGV at address: 0x%lx, pc=%p, rw=%d\n",
        (long) si->si_addr, pc, rwerror );
    
    if( !va ){    // invalide address, shall exit now ...
        //signal(SIGSEGV, SIG_DFL);
        err("sigsegv:  invalid address=%p\n", va);
        assert(0);
        return;
    }

    /////////////////   
    avl_node_t* node = find( &range_tree, (unsigned long)va ); 
    if( !node ){          
        err("fault addr=%p:  no avl-node found!!\n", va);
        sleep(1000000);
        assert(0);
        return;
    }

    vaddr_range_t  *vr = container_of(node, vaddr_range_t, treenode);
    //assert(vr->type ==allocator_pool );

	unsigned long offset;
	void *objvaddr;
	int state;
    item *it=NULL;
    int alloctype = vr->type; 

    /////  acquire lock on the enclosing vaddr-range 
    //pthread_mutex_lock( &vr->vrlist->lock );

    size_t  datasize;
    int clsid; // = slabs_clsid( datasize, sb_hbmem );
    slabclass_t* p;// = &sb_hbmem->slabclass[clsid];

    //pthread_mutex_lock( &vr->lock );
    if( alloctype == allocator_pool )
    {
        datasize = vr->pool.obj_size;
        clsid = slabs_clsid( datasize, sb_hbmem );
        p = &sb_hbmem->slabclass[clsid];

        pthread_rwlock_rdlock( &p->rwlock ); //// will read objs from ssd: put read lock
        pthread_mutex_lock( &vr->lock );
        //pthread_mutex_lock( &cache_lock );
    	offset = get_pool_obj_offset(vr, va); 
	    state = get_pool_obj_state(vr, va);
	    get_pool_obj_vaddr(vr, va, &objvaddr);
        //pthread_mutex_unlock( &cache_lock );
        pthread_mutex_unlock( &vr->lock );
        pthread_rwlock_unlock( &p->rwlock ); //// will read objs from ssd: put read lock
	}
	else
	{
        datasize = vr->coalesce.obj_size;
        clsid = slabs_clsid( datasize, sb_hbmem );
        p = &sb_hbmem->slabclass[clsid];

        pthread_rwlock_rdlock( &p->rwlock ); //// will read objs from ssd: put read lock
        pthread_mutex_lock( &vr->lock );
        //pthread_mutex_lock( &cache_lock );
		offset = get_coalesce_obj_offset(vr, va);
		state = get_coalesce_obj_state( vr, va );
		get_coalesce_obj_vaddr(vr, va, &objvaddr);		
        //pthread_mutex_unlock( &cache_lock );
        pthread_mutex_unlock( &vr->lock );
        pthread_rwlock_unlock( &p->rwlock ); //// will read objs from ssd: put read lock
	}

    //relax_ram_queue(&memQ); // release some item in pg-buf, st. its slabcache can be evicted.

    hb_item_get(objvaddr, vr, p, &it, 1); // either in slab-cache, or load from SSD

    //pthread_mutex_unlock( &vr->vrlist->lock );
    
    dbg("addr=%p at obj-off=%ld, obj-state='%s', objvaddr=%p, objsz=%ld, vaddr-unit=%ld, item=%p\n", 
        va, offset, state2string(state), objvaddr, vr->pool.obj_size, vr->pool.vaddr_unit_size, it );

    if( it ) { // (it) is in-ram data +header, (obj_size) is pure data-size
        if( alloctype == allocator_pool )
            assert( it->nbytes <= vr->pool.obj_size );        
        else
            assert( it->nbytes <= vr->coalesce.obj_size );        
    }
    else{
        err("vaddr=%p: no item found...\n", va);
        sleep(1000000);
    }
    ////////////////////////////////

    if( va - objvaddr >= it->nbytes ){
        dump_avl_node(node);
        dump_allocator(vr);
        err_exit("Boundary violation!!! item datasize=%d, va=%p, objvaddr=%p\n",
            it->nbytes, va, objvaddr );
    }


    //va = (void*)((unsigned long)va & ~((1UL<<12)-1));
    void *pd;
    size_t  protsize;
    size_t  sz;
    obj_item_t *om;

    // TODO(ouyangx):  protsize shall be within a page boundary.
    if( alloctype == allocator_pool ){

        protsize = vr->pool.vaddr_unit_size; // obj vaddr size, rounded to pg
        sz = vr->pool.obj_size;  // actual obj size rounded to 8 bytes
    } else {
        protsize = vr->coalesce.obj_size;
        sz = vr->coalesce.obj_size;
    }

  /**  if use write-lock: concurrent read are stable, but with less throughput (11.5K reads/sec), 
       since it blocks reader threads. 
       If use read-lock:  can produce higher concurrent read throughput(16K reads/sec), but 
       occasionally read results are incorrect...
  **/
    //pthread_rwlock_wrlock( &p->rwlock ); //// will read objs from ssd: put r/w lock
    //pthread_rwlock_rdlock( &p->rwlock ); ////
    pthread_mutex_lock( &vr->lock );
    if( rwerror == 0 ) // read-access
    {
        dbg("*** Read-fault...\n");
        
        if( it && state!=STATE_ALLOC_EMPTY && state != STATE_FREE ) 
        {   // copy data into this object
            //mprotect( va, 4096, PROT_WRITE);
            //mprotect(objvaddr, vr->pool.vaddr_unit_size, PROT_WRITE);
            //sz = vr->pool.obj_size; //it->nbytes - (va-objvaddr);
            mprotect(objvaddr, protsize, PROT_WRITE);
            pd = ITEM_data(it); // + (va-objvaddr); // start access data here...
            //if(sz > 4096 )  sz = 4096;
            //memcpy(va, pd, sz); 

            // TODO(ouyangx): copy size shall be within the current faulting page.
            memcpy(objvaddr, pd, sz);
        }
        
        //if( mprotect( va, 4096, PROT_READ)!=0 ) // open read access
        if(mprotect(objvaddr, protsize, PROT_READ)!=0)
        {
            err("in sigsegv: read mprotect %p failed...\n", va);
            perror("mprotect error::  ");
            exit(0);
        }
    }
    else if( rwerror==2 ) // write-access, give write-prot, which implies read-prot 
    {
        dbg("*** Write-fault...\n");
        if( mprotect(objvaddr, protsize, PROT_WRITE)!=0 ){
            //// for obj-pool, unprotect vaddr in obj size. 
            err("in sigsegv: write mprotect %p failed...\n", va);
            perror("mprotect error::  ");
            exit(0);
        }
        
        if( it && state!=STATE_ALLOC_EMPTY && state != STATE_FREE ) 
        {   // copy original data into this obj, so that user can write new data 
            // on top of the original version.
            pd = ITEM_data(it); // + (va-objvaddr); // start access data here...
            //sz = vr->pool.obj_size; // it->nbytes - (va-objvaddr);
            //if(sz > 4096 )  sz = 4096;
            //memcpy(va, pd, sz); 
            memcpy(objvaddr, pd, sz);  // for obj-pool, unprotect vaddr in obj size.
        }
        
        if( alloctype == allocator_pool ){
            om = get_pool_obj_item(vr, va); 
            om->pgbuf_dirty = 1;
        } 
        else {
            get_coalesce_obj_item(vr, va, &om); 
            om->pgbuf_dirty = 1;
        }
    }

    pthread_mutex_unlock( &vr->lock ); // need this lock, s.t. no tow concurrent thread
        // will copy the same contents
    //pthread_rwlock_unlock( &p->rwlock ); //// will read objs from ssd: put read lock

    //hb_item_remove(it); // decrease the refcount
    //it->refcount--;
    //it->refcount = 1;

    ///// put this ram-unit to queue
    //enqueue_ram_queue(va, 4096, it, vr,  &memQ);
    //enqueue_ram_queue(objvaddr, vr->pool.vaddr_unit_size, it, vr, &memQ);
    //pthread_mutex_lock( &cache_lock ); //// will read objs from ssd: put read lock

    enqueue_ram_queue(objvaddr, protsize, it, vr, &memQ);

    //pthread_mutex_unlock( &cache_lock ); //// will read objs from ssd: put read lock

    //pthread_mutex_unlock( &vr->vrlist->lock );
}



void    init_sigsegv_sigaction(struct sigaction* action)
{
  /* Block most signals while SIGSEGV is being handled.  */
  /* Signals SIGKILL, SIGSTOP cannot be blocked.  */
  /* Signals SIGCONT, SIGTSTP, SIGTTIN, SIGTTOU are not blocked because
     dealing with these signals seems dangerous.  */
  /* Signals SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTRAP, SIGIOT, SIGEMT, SIGBUS,
     SIGSYS, SIGSTKFLT are not blocked because these are synchronous signals,
     which may require immediate intervention, otherwise the process may
     starve.  */

    // sigs in the sa_mask will be blocked

    sigemptyset (&action->sa_mask);

#ifdef SIGHUP
    sigaddset (&action->sa_mask,SIGHUP);
#endif
#ifdef SIGINT
    sigaddset (&action->sa_mask,SIGINT);
#endif
#ifdef SIGQUIT
    sigaddset (&action->sa_mask,SIGQUIT);
#endif
#ifdef SIGPIPE
    sigaddset (&action->sa_mask,SIGPIPE);
#endif
#ifdef SIGALRM
    sigaddset (&action->sa_mask,SIGALRM);
#endif
#ifdef SIGTERM
    sigaddset (&action->sa_mask,SIGTERM);
#endif
#ifdef SIGUSR1
    sigaddset (&action->sa_mask,SIGUSR1);
#endif
#ifdef SIGUSR2
    sigaddset (&action->sa_mask,SIGUSR2);
#endif
#ifdef SIGCHLD
    sigaddset (&action->sa_mask,SIGCHLD);
#endif
#ifdef SIGCLD
    sigaddset (&action->sa_mask,SIGCLD);
#endif
#ifdef SIGURG
    sigaddset (&action->sa_mask,SIGURG);
#endif
#ifdef SIGIO
    sigaddset (&action->sa_mask,SIGIO);
#endif
#ifdef SIGPOLL
    sigaddset (&action->sa_mask,SIGPOLL);
#endif
#ifdef SIGXCPU
    sigaddset (&action->sa_mask,SIGXCPU);
#endif
#ifdef SIGXFSZ
    sigaddset (&action->sa_mask,SIGXFSZ);
#endif
#ifdef SIGVTALRM
    sigaddset (&action->sa_mask,SIGVTALRM);
#endif
#ifdef SIGPROF
    sigaddset (&action->sa_mask,SIGPROF);
#endif
#ifdef SIGPWR
    sigaddset (&action->sa_mask,SIGPWR);
#endif
#ifdef SIGLOST
    sigaddset (&action->sa_mask,SIGLOST);
#endif
#ifdef SIGWINCH
    sigaddset (&action->sa_mask,SIGWINCH);
#endif

}


int init_sigseg()
{
    struct sigaction sa; 

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    init_sigsegv_sigaction(&sa);

    sa.sa_sigaction = sigsegv_handler;
    //sa.sa_sigaction = test_sigsegv_handler;
    //sa.sa_handler = term_handler;
    sa.sa_flags = SA_SIGINFO; // need a siginfo_t in callback func

    if( sigaction(SIGSEGV, &sa, NULL)!= 0 )
    {
        printf("fail to reg sig SIGSEGV...\n");
        return -1;
    }

    return 0;
}

int try_sigsegv()
{
    unsigned char *p1;
    unsigned long i;
    /////////////// install sigsegv handler
    //// term handler
    struct sigaction sa; 

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    init_sigsegv_sigaction(&sa);

    //sa.sa_sigaction = sigsegv_handler;
    //sa.sa_sigaction = test_sigsegv_handler;
    //sa.sa_handler = term_handler;
    sa.sa_flags = SA_SIGINFO; // need a siginfo_t in callback func

    if( sigaction(SIGSEGV, &sa, NULL)!= 0 )
    {
        printf("fail to reg sig SIGSEGV...\n");
        return -1;
    }

    ///////////////////// alloc mem, set read/write protect

    //// NOTE::  in order to malloc huge size, shall let linux kernel use:  sysctl -w vm.overcommit_memory=1

    size_t size = 1024UL*1024*1024*1024;// *1024*200; //sizeof(int);
    i = size/2; //4096; //size/2; //1024*1024*50;

    p1 = memalign(4096, size);
    if( !p1 ){
        printf("mem alloc failed..\n");
        return -1;
    }
    
    printf("now mprotect: %p, : size=%ld ...\n", p1, size);

    //madvise(p1, size, MADV_DONTNEED);
    if(mprotect ((void *)(p1), size, PROT_NONE) != 0)
    {
        printf("mprotect 1 failed..\n");
        return -1;
    }
    printf("begin test...\n");

    //////////////////////////  trigger sigsegv
    printf("1::  p[%ld] = %d\n\n\n", i+2, p1[i+2]);  // read
    p1[i+2] = 101;  // write   
    printf("1.5::  p[%ld] = %d\n\n\n", i+2, p1[i+2]);  // read


    ////////////////
    madvise(p1+i, 4096, MADV_DONTNEED);
    if(mprotect ((void *)(p1+i), 4096, PROT_NONE) < 0)
    {
        printf("mprotect 2 failed..\n");
        return -1;
    }
    //sleep(2);
    p1[i+3] = 112;  // write   
    printf("2:: p[%ld] = %d\n\n\n", i+3, p1[i+3]);  // if not write in the previous line,
        // read p1[i]= 0

    //////

    i = size-1;    
    p1[i] = 35;  // write   
    printf("p[%ld] = %d\n", i, p1[i]);  // read

    /////////////////////////////////
    free(p1);



}

/*
int main(int argc, char** argv)
{

    int i;
    
    void *p;

    try_sigsegv();
 

    return 0;

}   */
