
//#define _XOPEN_SOURCE 600

#include <stdlib.h>
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

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/mman.h>



#include "myhmem.h"
#include "vaddr.h"
#include "avl.h"


volatile rel_time_t current_time;


inline void    myprotect(void* p, int unitsize, int units)
{
    int i=0;
    unsigned long pos = 0;

    for(i=0; i<units; i++)
    {
        if( mprotect( p+pos, unitsize, PROT_NONE)!=0 ){
            err("fail to mprotect at: %p, sz=%d\n", p+pos, unitsize);
            return;
        }
        pos+=unitsize;
    }

}

void    test_protect()
{
    long objs = 1000*1000;
    int unitsize = 4096;
    void *p;
    double tus;
    struct timeval t1, t2;

    posix_memalign(&p, unitsize, objs*unitsize);

    
    gettimeofday(&t1, NULL);
    myprotect(p, 4096, objs);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld mprotect:  %f usec, %f usec/protect\n", objs, tus, tus/objs);

    free(p);
}

void    test_madvise()
{
    long objs = 1000*1000;
    int unitsize = 4096;
    long pos = 0;
    unsigned char *p;
    double tus;
    struct timeval t1, t2;
    int i;
    char ch;

    posix_memalign(&p, unitsize, objs*unitsize);

    //////////////
    pos = 0;    
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++)
    {
        *(p+pos) = 0x31; // write-access causes an on-demand pg
        //ch = *(p+pos); // read-access won't cause OS to alloc on-demand pg
        pos += unitsize;
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld on-demand pgs: %f usec, %f usec/pg\n", objs, tus, tus/objs);

    printf("pause...\n");
    sleep(10);

    //////////////////////
    printf("start madvise...\n");

    pos = 0;
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        madvise(p+pos, unitsize, MADV_DONTNEED);
        pos += unitsize;
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld madvise release RAM:  %f usec, %f usec/rel\n", objs, tus, tus/objs);

    ////////////////////
    free(p);
}

////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////
struct work_item{
    void* buf;
    size_t  bufsize;
    int thread_id;
    int num_thread_share_buf; // num of threads sharing this buf
    int num_access; 
};
unsigned long get_rand(unsigned long max_val)
{
    long v; // = rand();
    v = lrand48();
    return v%max_val;
}

void  hbmem_rw(void* arg)
{
    struct timeval t1, t2;
    double tus;
    struct work_item  *im = (struct work_item*)(arg);

    long *p = (long*)(long*)im->buf;
    long cnt = im->bufsize / sizeof(long); // num of long items
    int tid =  im->thread_id; //(int)(unsigned long)(arg); //im->thread_id; 
    int num_thr = im->num_thread_share_buf;

    long pos; //  = get_rand(xxx); // pos: index into the long[] array
    
    size_t i;

    gettimeofday(&t1, NULL);
    for(i=0; i<im->num_access; i++)
    {
        pos = get_rand(cnt/num_thr);
        pos += (cnt/num_thr)*tid;

        /// read access:
        if( *(p+pos) != pos ){
            printf("rd-test err: pos=%ld: val=%ld\n", pos, *(p+pos) );            
        }
        /// write access...
    }

    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("***** thread:   %ld access, %.3f usec, %.3f usec/read\n", 
        im->num_access, tus, tus/im->num_access );

}

void    multi_thread_test()
{ 
    long objsizes[16] = { 1024UL*4, 1024UL*4, 1024UL*4, 1024UL*4, 
            1024UL*4, 1024UL*4, 1024UL*4, 1024UL*4,
            1024UL*4, 1024UL*4, 1024UL*4, 1024UL*4,
            1024UL*4, 1024UL*4, 1024UL*4, 1024UL*4 };

    size_t  bufsize = 1024UL*1024*500; //1000; //1024; //*100;
    size_t  num_access = 200UL; //1000UL*10; //*1000;
    int   num_thread = 12;

    struct work_item  witem[16];
    pthread_t wthread[16];

    struct timeval t1, t2;
    double tus;
    int i;

    size_t  cnt; // = bufsize/sizeof(long);
    long *p1; 
    long t;

    ///////// init ssd-hybrid mem
    init_vaddr("/tmp/fio/ouyangx");

    hbmem_alloc_object(objsizes[0], bufsize/objsizes[0], &witem[0].buf);
    //////// alloc objs
    for(i=0; i<num_thread; i++){
        //hbmem_alloc_object(objsizes[i], bufsize/objsizes[i], &witem[i].buf);
        witem[i].buf = witem[0].buf;
        witem[i].bufsize = bufsize;
        witem[i].num_access = num_access;
    }

    /// pre-fault all objs
    cnt = witem[0].bufsize / sizeof(long);
    //cnt = 5; 
    for( t=0; t<cnt; t++ ){
        for(i=0; i<num_thread; i++){
            p1 = witem[i].buf; //ssdbuf;
            *(p1+t) = t;
        }
    }
    //clear_ram_queue( &memQ ); 
    //evict_all_slabs( sb_hbmem );
    dump_slabs( sb_hbmem );
    show_evict_stats();
    show_load_stats();
    /////// create worker-threads

    for(i=0; i<num_thread; i++){
        //pthread_create( wthread+i, NULL, hbmem_rw, (unsigned long)i);
        witem[i].thread_id = 0;
        witem[i].num_thread_share_buf = 1;
        pthread_create( wthread+i, NULL, hbmem_rw, &(witem[i]) );
    }
    
    gettimeofday(&t1, NULL);
    ////// wait for worker-thread to exit
    for(i=0; i<num_thread; i++){
        pthread_join( wthread[i], NULL );
    }  

    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("\n %d threads, total %ld access: %.1f usec, throughput= %.3f access/sec\n", 
        num_thread,  num_thread*num_access, tus,  (num_thread*num_access)/(tus/1000/1000) );
    show_evict_stats();
    show_load_stats();

    /////////  free 
    hbmem_free(witem[0].buf);
    gettimeofday(&t1, NULL);
    for(i=0; i<num_thread; i++)
    {
        //hbmem_free(witem[i].buf);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("\n**** %ld free, %f usec\n", bufsize, tus );


    ////////  finalize hybrid-mem
    destroy_vaddr();

}

////////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////
void    compare_coal()
{
    long i; 
    long totalsize = 1024UL*1024*50; //1024*10; //1024; //*512; //1024; //1024*16; //

    long objsize1 = 1024UL*4;
    long objsize2 = 1024UL*16;

    long objs = totalsize/4096; //objsize1;

    int pos = 1005;

    struct timeval t1, t2;
    double tus;
    unsigned long tl; 
    long t;
    
    init_vaddr("");

    unsigned char *p1; // = malloc(sizeof(char*)*objs);
    unsigned char *p2;

    //////// alloc objs
    gettimeofday(&t1, NULL);
    hbmem_alloc_object(objsize1, totalsize/objsize1, &p1);
    hbmem_alloc_object(objsize2, totalsize/objsize2, &p2);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    show_evict_stats();
    show_load_stats();

    /// pre-fault all objs
    printf("\n***** prefault obj size  = %ld, %ld:: \n", objsize1, objsize2 );
    for(tl=0, t=0; tl<totalsize; tl+=4096){
	    memcpy(p1+tl+pos, &t, sizeof(long));
	    memcpy(p2+tl+pos, &t, sizeof(long));
        t++;
    }
    show_evict_stats();
    show_load_stats();
            
    /// write fault    
	printf("\n\n****  Write-fault obj size 1=%ld::\n", objsize1);
    gettimeofday(&t1, NULL);
    for(tl=0, t=1; tl<totalsize; tl+=4096, t++){
        memcpy(p1 + tl + pos,  &t, sizeof(long));
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld write, %f usec, %f usec/write\n", totalsize/4096, tus, tus/objs);
    show_evict_stats();
    show_load_stats();
    

    /// read fault
	printf("\n\n****  Read-fault obj size 1=%ld::\n", objsize1);
    gettimeofday(&t1, NULL);
    for(tl=0, i=1; tl<totalsize; tl+=4096, i++){
        memcpy(&t, p1 + tl + pos, sizeof(long));
        if( t != i )
            printf("err at pos[%d] = %ld\n", i, t);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("**** %ld read, %f usec, %f usec/read\n", objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();

    /// write fault    
	printf("\n\n****  Write-fault obj size 2=%ld::\n", objsize2);
    gettimeofday(&t1, NULL);
    for(tl=0, t=1; tl<totalsize; tl+=4096, t++){
        memcpy(p2 + tl + pos,  &t, sizeof(long));
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld write, %f usec, %f usec/write\n", objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();
    

    /// read fault
	printf("\n\n****  Read-fault obj size 2=%ld::\n", objsize2);
    gettimeofday(&t1, NULL);
    for(tl=0, i=1; tl<totalsize; tl+=4096, i++){
        memcpy(&t, p2 + tl + pos, sizeof(long));
        if( t != i )
            printf("err at pos[%d] = %ld\n", i, t);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("**** %ld read, %f usec, %f usec/read\n", objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();

    /////////  free 
    gettimeofday(&t1, NULL);
    hbmem_free(p1);
    hbmem_free(p2);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("\n**** %ld free, %f usec\n", objs, tus );

    //////////////

    destroy_vaddr();

}


void test_coal()
{
    long i; 
    long objs = 1000*40; //1000; //1000*10; //1000*100; //1000*500; //1000*500; //1000*400; //1000*10; //1000*1000; 
    long obj_pos = 3005; //2001; //8094; //2001; //8094;
    long objsize = 1024UL*4;

    /**************
    NOTE::  if an obj is > a pg (4kB), and an access crosses the 2 pg boundary, 
    in this case if vaddr is unprotected in pg granularity, then two sigseg faults 
    are raised, one for each page.
    
    If vaddr is unprotected in obj granularity, then only one sigseg is generated.
    So in obj-pool mode, the sigseg handler shall unprotect vaddr in obj unit sizes.
    *******************/

    char ch;
    struct timeval t1, t2;
    double tus;
    char buf[16384];
    
    long t;
    
    init_vaddr("");

    unsigned char *p; // = malloc(sizeof(char*)*objs);
    unsigned char *p2; 


    //////// alloc objs
    gettimeofday(&t1, NULL);
    hbmem_alloc_object(objsize, objs, &p);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("***** addr=%p, alloc-size=%ld, objsize=%ld, %ld objs, %f usec, %f usec/alloc\n", 
       p, objsize*objs, objsize, objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();

    /// pre-fault all objs
    printf("\n***** prefault:: \n");
    for(i=0; i<objs; i++){
	    memcpy(p+i*objsize+obj_pos, &t, sizeof(long));        
    }
    printf("prefault::\n");
    show_evict_stats();
    show_load_stats();
            
    /// write fault    
	printf("\n\n****  Write-fault::");
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        t = i;
        memcpy(p+i*objsize+obj_pos, &t, sizeof(long));
        //sprintf(p[i]+obj_pos, "string %d = %d", i, i);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld write, %f usec, %f usec/write\n", objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();
    

    /// read fault
    printf("\n\n****** Read fault:: ");
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        memcpy(&t, p+i*objsize+obj_pos, sizeof(long));
        if( t != i )
            printf("err at pos[%d] = %ld\n", i, t);
        //printf("p[%d] = '%s'\n", i, p[i]+obj_pos);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("**** %ld read, %f usec, %f usec/read\n", objs, tus, tus/objs);
    
    show_evict_stats();
    show_load_stats();

   
    /////////  free 
    gettimeofday(&t1, NULL);
    hbmem_free(p);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("\n**** %ld free, %f usec\n", objs, tus );

    //////////////

    destroy_vaddr();
}

void test_hb()
{
    printf("\n\n======== test pool-allocator..., sizeof(item)=%d\n", sizeof(item) );
    //return;

    long i; 
    long objs = 1000*5; //1000*10; //1000*100; //1000*500; //1000*500; //1000*400; //1000*10; //1000*1000; 
    long size = 12305; //3015;  //3015; //3015; //12305; //24*16; //obj size ;
    long obj_pos = 8190; //2001; //8094; //2001; //8094;

    /**************
    NOTE::  if an obj is > a pg (4kB), and an access crosses the 2 pg boundary, 
    in this case if vaddr is unprotected in pg granularity, then two sigseg faults 
    are raised, one for each page.
    
    If vaddr is unprotected in obj granularity, then only one sigseg is generated.
    So in obj-pool mode, the sigseg handler shall unprotect vaddr in obj unit sizes.
    *******************/

    char ch;
    struct timeval t1, t2;
    double tus;
    char buf[16384];
    
    long t;
    
    init_vaddr("");

    unsigned char **p = malloc(sizeof(char*)*objs);


    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++)
        hbmem_alloc_object(size, 1, &p[i]);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("***** %ld alloc, %f usec, %f usec/alloc\n", objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();
    
    /// pre-fault all objs
    t = 10; 
    printf("\n***** prefault:: \n");
    for(i=0; i<objs; i++){
        //sprintf(p[i]+obj_pos, "string %d = %d", i, i);
        //p[i][obj_pos] = (char)((0x30+i));
	    memcpy(p[i]+obj_pos, &t, sizeof(long));        
    }
    printf("prefault::\n");
    show_evict_stats();
    show_load_stats();
            
    /// write fault    
	printf("\n\n****  Write-fault::");
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        t = i;
        memcpy(p[i]+obj_pos, &t, sizeof(long));
        //sprintf(p[i]+obj_pos, "string %d = %d", i, i);
        //p[i][obj_pos] = (char)((0x31+i));
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld write, %f usec, %f usec/write\n", objs, tus, tus/objs);
    show_evict_stats();
    show_load_stats();
    

    /// read fault
    printf("\n\n****** Read fault:: ");
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        //ch = p[i][obj_pos];
        //if( ch != (char)(0x31+i) ){
        //    printf("read err at: p[%d]\n", i);
        //}
        //ch = p[i][8094];
        //ch = p[i][9094];
        //if( i%500==0 )        
        memcpy(&t, p[i]+obj_pos, sizeof(long));
        if( t != i )
            printf("err at pos[%d] = %ld\n", i, t);
        //printf("p[%d] = '%s'\n", i, p[i]+obj_pos);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("**** %ld read, %f usec, %f usec/read\n", objs, tus, tus/objs);
    
    show_evict_stats();
    show_load_stats();

    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        hbmem_free(p[i]);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("\n**** %ld free, %f usec, %f usec/free\n", objs, tus, tus/objs);
    
    free(p);
    destroy_vaddr();
}

void test_individual()
{
    int i;

    struct timeval t1, t2;
    double tus;
    char ch;
    int objs = 5; //1000*300; //1000*100; //1000*1100; //1000*1000; //1000*1000;
    int size = 3000; //24*16; //obj size ;
    int obj_pos = 100;

    init_vaddr("");

    ////////// alloc objs
    unsigned char **p = malloc(sizeof(char*)*objs);
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++)
    {
        if( hbmem_alloc_object(size, 1, &p[i]) != 0 ){
           err_exit("fail to alloc obj-%d\n", i); 
        }
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld alloc, %f usec, %f usec/alloc\n", objs, tus, tus/objs);
    //dump_pool_list(&pList);
    //dump_slabs(sb_hbmem);
    printf("\n\n"); 
    
    /////////// test slab-cache lookup latency::
    item *temp;
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        //hb_item_get(p[i], NULL, &temp, 1);
        //hb_item_remove(temp); // dec the ref to this item
        //temp->refcount--;
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld slab-lookup, %f usec, %f usec/lookup\n", objs, tus, tus/objs);
        
    ////////////////////// test page write fault latency::
    
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        p[i][obj_pos] = 0x31; // write fault:: init obj to be dirty
        //sprintf(p[i]+obj_pos, "p-%04d = %d\n", i, i);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    //printf("=== %ld write sigsegv, %f usec, %f usec/fault\n", objs, tus, tus/objs);
    
   
        ///////  read fault  
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        ch = p[i][obj_pos];         // read fault
        //memcpy(p[(i+objs/2)%objs], p[i], size);
        //ch = p[(i+1)%objs][0] = p[i][0];         // read fault
        if( ch != 0x31 ){
            err("read err!!\n");
        }
        //if( i%10000 == 0 )
        //printf("%s\n", p[i]+obj_pos);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld read sigsegv, %f usec, %f usec/fault\n", objs, tus, tus/objs);

        //////// write fault 
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++){
        p[i][obj_pos] = 0x32;          // write fault
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld write sigsegv, %f usec, %f usec/fault\n", objs, tus, tus/objs);

    /////////////////
    printf("\n\npause...\n");
    //sleep(10);

    ////////////// free objs
    printf("\n\n\n Release %ld objs:: \n", objs);
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++)
        hbmem_free(p[i]);
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("=== %ld free, %f usec, %f usec/free\n", objs, tus, tus/objs);

    //dump_pool_list(&pList);
    //dump_slabs(sb_hbmem);

    //////////// free
    free(p);
    destroy_vaddr();
}



int test()
{
    ///    
    init_vaddr("");
    
    /////////
    int size = 1020;
    //dump_allocator(vr);
    //dump_pool_list(&pList);
    struct timeval t1, t2;
    double tus;
    char ch;

    int objs = 1000*500;
    unsigned char **p;
    p = malloc(objs*sizeof(char*));

    ///////////////////
    int i;
    gettimeofday(&t1, NULL);
    for(i=0; i<objs; i++)
    {
        if( hbmem_alloc_object(size, 1, &p[i]) != 0 ){
           err_exit("fail to alloc obj-%d\n", i); 
        }
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld alloc, %f usec, %f usec/write\n", objs, tus, tus/objs);

    dump_pool_list(&pList);
    dump_slabs(sb_hbmem);
    dump_hash_stats();
    clear_hash_stats();
    printf("\n\n"); 
    ///////////////////////

    gettimeofday(&t1, NULL);    
    for(i=0; i<objs; i++){
        //sprintf(p[i], "test %d...", i);
        p[i][0] = 0x31;
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld write-access, %f usec, %f usec/write\n", objs, tus, tus/objs);

    gettimeofday(&t1, NULL);    
    for(i=0; i<objs; i++){
        ch = p[i][0];
        //if( i%50000==0 )
        //fprintf(stderr, "====  get addr = %p, p[%d]='%s'\n", p[i], i, p[i]);
    }
    gettimeofday(&t2, NULL);
    tus = (t2.tv_sec - t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
    printf("%ld read-access, %f usec, %f usec/read\n", objs, tus, tus/objs);

    dump_hash_stats();
    dump_ram_queue(&memQ);

    for(i=0; i<objs; i++)
        hbmem_free(p[i]);

    dump_pool_list(&pList);
/*
    for(i=0; i<0; i++){
        hbmem_alloc_object(size, 1, p+i);
    }
        //pool_alloc(vr, p+i);
        
    dump_pool_list(&pList);

    for(i=0; i<0; i++)
        hbmem_free(p[i]);
        //pool_free(vr, p[i]);

    dump_pool_list(&pList);
*/
    //////////
    free(p);
    printf("pause...\n");
    sleep(1);
    destroy_vaddr();


}


avl_node_t* new_node(unsigned long addr, unsigned long len)
{
    avl_node_t *node;
    node = malloc(sizeof(avl_node_t));
    node->address = addr;
    node->len =  len;
    return node; 
}

void    test_avl()
{
    avl_tree_t tree;
    avl_node_t *node;

    avl_init( &tree );

    int i;
    for(i=1; i<=8; i++){
        node = new_node(i*1000, 200);
        insert(&tree, node);
    }

    dump_avl_tree(&tree);
    ///
    get_all_avl_nodes(&tree);

    ////
    for(i=1; i<=8; i++){
        node = find(&tree, i*1000);
        dump_avl_node(node);
        printf("\n");

        delete(&tree, node); 
        dump_avl_tree(&tree);
        free(node);
    }

    ////////
}


int main(int argc, char **argv)
{
    /////////////
    //test_avl();
    //test();

    //test_protect();
    //test_madvise();
    //test_individual();
    //test_coal();

    //test_hb();
    //compare_coal();

    multi_thread_test();

    ////////////


    obj_item_t  ot;

    ot.state = 3;
    ot.ssd_pos = 100;
    ot.ssd_pos_msb = 15; 

    printf("sizeof(obj_item_t) = %d\n", sizeof(obj_item_t));

    vaddr_range_t   t;

    t.start = 1;
    t.length = 2;
    t.pool.obj_size = 10;
    t.coalesce.obj_size = 20;




    return 0;

}
