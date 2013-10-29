#include "stdio.h"

#include "stdlib.h"
#include "string.h"

#include <sys/types.h>
#include <attr/xattr.h>

#include <semaphore.h>
#include <pthread.h>

#include "myhmem.h"

#include "slabs.h"
#include "items.h"

extern slabs_t*  sb_hbmem;
extern slabs_t*	 sb_htable;
extern struct settings settings;


volatile rel_time_t current_time;


struct stats stats;  



typedef struct work_info_s
{
	pthread_t	thr;
	int myid;
	int	numthreads;
	
	sem_t	sem_begin;
	sem_t	sem_end;

	float	updaterate;
		
	unsigned char	*buf;  // dynamically alloc a buf to read/write
	int		bufsize; // size of the buf
	int 	check_buf; // whether should init the buf before writing to file
	
	long	total_ops;
	long	opset;  // num of set ops in current round
	long	opget;  // num of get ops in current round
	
} work_info_t;





void	slab_test(slabs_t* sb)
{
	//// work
	int i, j;
	int		numitems = 15;
	int itemsize = 208;
	void** array;
	
	array = (void**)malloc( numitems*sizeof(void*));
	
	dump_slabs(sb);

	for(i=0; i<numitems; i++){
		
		array[i] = myslabs_alloc( itemsize, sb );		
		if( array[i] == NULL ){
			printf(" at item-%d: run out of mem\n", i);
			break;
		}
		sprintf(array[i], "item %d", i);
	}
	
	dump_slabs(sb);
	
	for(j=0; j<i; j++)
		myslabs_free(array[j], itemsize, sb);
		
	dump_slabs(sb);
	
	for(i=0; i<numitems; i++){
		
		array[i] = myslabs_alloc( itemsize, sb );
		if( array[i] == NULL ){
			printf(" at item-%d: run out of mem\n", i);
			break;
		}
		sprintf(array[i], "item %d", i+100);
	}	
	
	dump_slabs(sb);
	
	
	for(j=0; j<i; j++)
		myslabs_free(array[j], itemsize, sb);
	
	dump_slabs(sb);
		
	free(array);


}


///// alloc an item
item *item_alloc(char *key, size_t nkey, int flags, rel_time_t exptime, int nbytes) 
{
    item *it;
    dbg("%s: begin::  key=%s (len=%ld), nbytes=%d\n", __func__, key, nkey, nbytes);
    pthread_mutex_lock(&cache_lock);
    it = do_item_alloc(key, nkey, flags, exptime, nbytes);
    pthread_mutex_unlock(&cache_lock);
    /*if( it )
    printf("%s: return:   key=%s (len=%ld), nbytes=%d val=\"%s\"\n", 
            __func__, key, nkey, nbytes, ITEM_data(it) );   */
    return it;
}



///// have filled in an item,  need to store this item into the hashtable
int store_item(item *it)
{
    dbg("%s: begin::   %s (len=%d), nbytes=%d, ref=%d, slab-clsid=%d\n", 
            __func__, ITEM_key(it), it->nkey, it->nbytes, 
            it->refcount, it->slabs_clsid );
            
    char* key = ITEM_key(it);
    
    pthread_mutex_lock(&cache_lock);
    
    // ret = do_store_item(item, comm, c); 
    {
		item *old_it = do_item_get(key, it->nkey);
		
		if(old_it)
			item_replace(old_it, it);
		else
			do_item_link(it);
			
		if(old_it)
			do_item_remove(old_it);     
    }    
    /////////////////////////////    
    
    
    pthread_mutex_unlock(&cache_lock);

}

/*
Given a key, search the hashtable for a match.
The matched item's refcount is inc by 1 in "do_item_get()",
and replace this item to head of LRU list.
*/
item *item_get(const char *key, const size_t nkey) 
{
    item *it;
    dbg("%s: begin::   key=%s (len=%ld)\n", __func__, key, nkey  );
    
    pthread_mutex_lock(&cache_lock);
    
    it = do_item_get(key, nkey); // search hashtable to find item
    
    if( it )
        do_item_update(it);   // move the item to LRU head
    
    pthread_mutex_unlock(&cache_lock);
    
    if( it )
    dbg("%s: key=%s (len=%ld), nbytes=%d, val=\"%s\", ref=%d, slab-clsid=%d\n",
            __func__, key, nkey, it->nbytes, ITEM_data(it), it->refcount, it->slabs_clsid);
    else{
        //printf("%s: return it NULL\n", __func__);
    }
    return it;
}


/*
replace an old-item with a new one, and unlink the olditem from hashtable & LRU list.
If olditem's refcount=0, it's reclaimed to freelist.
*/
int item_replace(item *old_it, item *new_it) 
{
    dbg("%s: begin::   old-item: %s (len=%d), nbytes=%d, ref=%d, slab-clsid=%d\n",
            __func__, ITEM_key(old_it), old_it->nkey,  
            old_it->nbytes, old_it->refcount, old_it->slabs_clsid);
    //dump_item(old_it);
    //dump_item(new_it);
    return do_item_replace(old_it, new_it);
}


///// after usage, release an item
/* 
Decrements the reference count on an item.
If its refcount is 0 and it's not linked in the LRU lis (it->flags has no ITEM_LINKED bit),
this item is reclaimed to the freelist.
*/
void item_remove(item *item) 
{
    dbg("%s: begin::   %s (len=%d), nbytes=%d, ref=%d, slab-clsid=%d\n",
            __func__, ITEM_key(item), item->nkey,  
            item->nbytes, item->refcount, item->slabs_clsid);
    pthread_mutex_lock(&cache_lock); 
    do_item_remove(item);
    pthread_mutex_unlock(&cache_lock);
}




/////////////////////////// delete an item
int delete_by_key(char* key, int nkey)
{	
	dbg("will del: key=%s(%d)\n", key, nkey);	
	item* it = item_get(key, nkey);
	if(it)
	{
		//dump_item(it);		
		pthread_mutex_lock(&cache_lock); 
		
		do_item_unlink(it); // rm the item from LUR list
		//dump_item(it);
		
		do_item_remove(it);
		//dump_item(it);		
	
		pthread_mutex_unlock(&cache_lock); 
        return 1;
	}
    return 0;
}


void	delete_by_item(item* it)
{
	dump_item(it);
		
	pthread_mutex_lock(&cache_lock); 

	dump_item(it);
		
	if( it->refcount>0 )
	{
		do_item_unlink(it); // rm the item from LUR list
		do_item_remove(it); // free the item
	}
	else // refcount==0, "unlink" will free it automatically
	{
		do_item_unlink(it); // rm the item from LUR list
	}
	//dump_item(it);
	
	pthread_mutex_unlock(&cache_lock); 

}

//////////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////////

	//////////// alloc, then delete...
/*	for(i=0; i<numitems/2; i++)
	{
		clsid = slabs_clsid(sizes[i%num_sizes], sb_hbmem);
		sprintf(key, "%d", i);	
		dbg("alloc for key:\"%s\"(%d), nbytes=%d\n", key, strlen(key), sizes[i%num_sizes] );
		
		it = do_item_alloc( key, strlen(key), 0, 0, sizes[i%num_sizes] );
		
		if( !it || !it->data ){
			printf("fail to alloc item %d\n", i);
			return;
		}	
		sprintf((char*)it->data, "%d\r\n", i); // // nbytes);	

		store_item(it); // link the item into LUR list
		item_remove(it);
		//dump_LRU_list(clsid);
	}	
	for(i=0; i<numitems/2; i++)
	{
		sprintf(key, "%d", i);		
		//item_delete(key, strlen(key));
		delete_by_key(key, strlen(key));
		//memcpy(it->data, "aaaaaaaaa\r\n", nbytes);	
	}
	dump_LRU_list(clsid);
	printf("will start run now...\n");	
	sleep(10);	*/
	
unsigned long get_rand(unsigned long max_val)
{
    long v; // = rand();
    v = lrand48();
    return v%max_val;
}


//// timing test
void	test1()
{
#if 1
	long numitems = 1000*1000*15; //1000*1000*10; 
	//1000*10; //1000*1000*10; // 1000*64; // 1500; // *1500; // 1024L*1024;	
	long toadd = 1000*200; //1000*200; //500;  //1000000;
    long lookups = 1000*200; //500; //100000;	
    long todel = 1000*100; //500; //50000;
#else
	long numitems = 1000*100; //*20; //1000*1000*10; 
	//1000*10; //1000*1000*10; // 1000*64; // 1500; // *1500; // 1024L*1024;	
	long toadd = 0; //500;  //1000000;
    long lookups = 1000; //500; //100000;	
    long todel = 1000; //500; //50000;
#endif

    long printpt = 1000000L;

	long i;
	item** itarray;
	struct timeval t1, t2;
	item *it, *newit;
	int clsid;
	int num_sizes = 1;
	int sizes[6] = {4080, 2040, 250, 510, 1020}; //{512, 1000, 2000, 3000, 4000};  // value sizes
	char	key[512];
	double tus;

    /// init the random-gen
    gettimeofday(&t1, NULL);
    srand48( t1.tv_usec );


	///////////////////// 0.  alloc certain num of items as base for all testing
	clsid = slabs_clsid(sizes[i%num_sizes], sb_hbmem);
	printf("***** will create %ld items as base, item-size=%d\n", numitems, sizes[0] );
	gettimeofday(&t1, NULL);
	for(i=0; i<numitems; i++)
	{
		sprintf(key, "%ld", i);	
		dbg("alloc for key:\"%s\"(%d), nbytes=%d\n", key, strlen(key), sizes[i%num_sizes] );
		
		it = do_item_alloc( key, strlen(key), 0, 0, sizes[i%num_sizes] );
				
		if( !it || !it->data ){
			printf("fail to alloc item %d\n", i);
			return;
		}
		sprintf((char*)it->data, "value-of-%d\r\n", i);
		store_item(it); // link the item into LUR list
		item_remove(it); // dec its refcount	
		
        if( i%printpt==0 ){
        	printf("create finished items: %d\n", i);
        }
	}
	gettimeofday(&t2, NULL);
	tus = (t2.tv_sec-t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
	printf("create %d items: total-time= %f us,  avg= %.3f us / item\n", numitems, 
		tus, tus/numitems );
	

    if( toadd > 0 )
    {	
	///////////////////// 1.   add testing...
	printf("\n******  Now will create %ld new items\n", toadd);
    
	clsid = slabs_clsid(sizes[i%num_sizes], sb_hbmem);
    i = numitems;
	gettimeofday(&t1, NULL);
	for(; i<numitems+toadd; i++)
	{
		sprintf(key, "%ld", i);	
		dbg("alloc for key:\"%s\"(%d), nbytes=%d\n", key, strlen(key), sizes[i%num_sizes] );
		
		it = do_item_alloc( key, strlen(key), 0, 0, sizes[i%num_sizes] );
				
		if( !it || !it->data ){
			printf("fail to alloc item %d\n", i);
			return;
		}
		sprintf((char*)it->data, "value-of-%d\r\n", i);
		store_item(it); // link the item into LUR list
		item_remove(it); // dec its refcount	
		
	    /*	
		newit = do_item_alloc( key, strlen(key), 0, 0, sizes[i%num_sizes] );
		sprintf((char*)newit->data, "new-value-of-%d\r\n", i);
		item_replace(it, newit); // since oldit has no refcount, it will be reclaimed 
		item_remove(newit); // dec its refcount  
        */
        
        if( i%printpt==0 ){
        	//printf("add finished items: %d\n", i);
        }
	}
	gettimeofday(&t2, NULL);
	tus = (t2.tv_sec-t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
	printf("add %d items: total-time= %f us,  avg= %.3f us / item\n", toadd, 
		tus, tus/toadd );
    }

	//dump_LRU_list(clsid);
	dump_slabs(sb_hbmem);	
	dump_slabs(sb_htable);
	
	numitems += toadd;	
    printf("******** msync virtmem ret=%d\n", slabs_msync(sb_hbmem) );


	long tn, cnt;
	///////////////////////// 2. search items
	printf("***** will search %ld items\n", lookups );
	gettimeofday(&t1, NULL);
	for(i=0; i<lookups; i++)
	//for(i=numitems-1; i>=0; i--)
	{
        cnt = get_rand(numitems);	
		sprintf(key, "%ld", cnt);
		it = item_get( key, strlen(key) );
		if( !it ){
			printf("Error!!  key \"%s\"(%d) has no value!!\n", key, strlen(key) );
		}
		else {
			item_remove(it); // release item
			sscanf((char*)it->data, "value-of-%ld\r\n", &tn);
			if( tn != cnt ){
				printf("Error!!  item-get (%s): value not match!!\n", key );
			}
			//dump_item(it);
		}
		
        if( i%printpt==0 ){
        	//printf("search finished items: %ld\n", i);
        }
		// use the item
		//dump_LRU_list(clsid);
	}   
	gettimeofday(&t2, NULL);
	tus = (t2.tv_sec-t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
	printf("search %d items: total-time= %f us,  avg= %.3f us / item\n", lookups, 
		tus, tus/lookups );	
	
	//dump_LRU_list(clsid);
	dump_slabs(sb_hbmem);	
	dump_slabs(sb_htable); 
    
    
	
	////////////////////// 3. delete an item
/*    long hasdel = 0;    
	printf("***** will delete %ld items\n", todel );
	
	gettimeofday(&t1, NULL);
	//for(i=numitems-1; i>0; i--)
	for(i=0; i<todel; i++)
	{
        cnt = get_rand(numitems/2);
        //cnt = i;
		sprintf(key, "%ld", cnt);	
		hasdel += delete_by_key(key, strlen(key));

		//dump_LRU_list(clsid);
        if( hasdel % printpt==0 ){
        	printf("del finished items: %d\n", i);
        }
	}
	gettimeofday(&t2, NULL);
	tus = (t2.tv_sec-t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
	printf("delete %d out of %d items: total-time= %f us,  avg= %.3f us / item\n", 
        hasdel, todel, tus, tus/hasdel);

	//dump_LRU_list(clsid);
	dump_slabs(sb_hbmem);	
	dump_slabs(sb_htable);
*/
	
}


/////////////////////////////////////////////////
///////////////			multi-thr transaction throughput test::


void*	thread_tps(void* arg)
{
	work_info_t	*winfo = (work_info_t*)arg;
	
	dbg("thr_%d started...\n", winfo->myid);

	int num_sizes = 1;
	int sizes[6] = {1020}; //{512, 1000, 2000, 3000, 4000};  // value sizes
	
#if 1
	long total_numitems = 1000*1000*19; //1000*1000*10; 
	long total_ops = 400000; 
#else
	long total_numitems = 1000000;
	long total_ops = 4000; 
#endif

	long i, j;
	long numitems = total_numitems / winfo->numthreads;  // each procs inserts this many items
	long myops = total_ops / winfo->numthreads;   //then, each proc performs this many trans	
	char	key[512];
	item *it;
	
	///////// ins certain amount of records into the public pool
	for(i=0; i<numitems; i++)
	{
		sprintf(key, "p%d-key-%ld", winfo->myid, i);
		
		dbg("alloc for key:\"%s\"(%d), nbytes=%d\n", key, strlen(key), sizes[i%num_sizes] );
		
		it = do_item_alloc( key, strlen(key), 0, 0, sizes[i%num_sizes] );
				
		if( !it || !it->data ){
			printf("fail to alloc item %d\n", i);
			return;
		}
		sprintf((char*)it->data, "value-of-%d\r\n", i);
		store_item(it); // link the item into LUR list
		item_remove(it); // dec its refcount	
		
        if( i%1000000==0 ){
        	printf("[thr_%d]: create finished items: %ld\n", winfo->myid, i);
        }
	}
		
		
	int opselect=0;
	int thresh = 0;
	long tn;
	while(1)
	{
		/////  wait to be woken up
		sem_wait( &winfo->sem_begin );
		dbg("[thr_%d]: uprate=%f\n", winfo->myid, winfo->updaterate );

		winfo->opset=0;
		winfo->opget=0;

		thresh = (int)((winfo->updaterate * 1000));
		
		//////// start random get/set ops
		for(j=0; j<myops; j++)
		{
			opselect = get_rand(1000);
			
			if( opselect < thresh )	// set-ops
			{
				i = numitems;
				sprintf(key, "p%d-key-%ld", winfo->myid, i);				
				dbg("alloc for key:\"%s\"(%d), nbytes=%d\n", key, strlen(key), sizes[i%num_sizes] );
				
				it = do_item_alloc( key, strlen(key), 0, 0, sizes[i%num_sizes] );
				if( !it || !it->data ){
					printf("[thr_%d]: fail to alloc item %d\n", winfo->myid, i);
					return;
				}
				
				sprintf((char*)it->data, "value-of-%d\r\n", i);
				store_item(it); // link the item into LUR list
				item_remove(it); // dec its refcount
				
				numitems++;
				winfo->opset++;
			
			}
			else  // get ops
			{
				i = get_rand(numitems);
				sprintf(key, "p%d-key-%ld", winfo->myid, i);
				
				it = item_get( key, strlen(key) );
				if( !it ){
					printf("Error!!  key \"%s\"(%d) has no value!!\n", key, strlen(key) );
				}
				else {
					item_remove(it); // release item
					sscanf((char*)it->data, "value-of-%ld\r\n", &tn);
					if( tn != i ){
						printf("Error!!  item-get (%s): value not match!!\n", key );
					}
				}		
				
				winfo->opget++;
			}

		
		}
		/////////  end of current round of work
		
		////// tell parent: I'm finished
		sem_post( &winfo->sem_end );
		
		if( winfo->updaterate>=1.0 )
			break;
	
	}
	
	sleep(1);
	dbg("thr_%d exit now\n", winfo->myid);
	pthread_exit(NULL);	
}





int	test_tps(int numthr)
{
	int i, j, rv;
	struct timeval tstart, tend;
	float	uprate = 0;
	double tus;
	long	allops, allget, allset;
	
	work_info_t	*winfo;
	winfo = (work_info_t*)malloc( numthr * sizeof(work_info_t) );

    /// init the random-gen
    gettimeofday(&tstart, NULL);
    srand48( tstart.tv_usec );


	//// 1. prep threads
	for(i=0; i<numthr; i++){
		winfo[i].myid = i;
		winfo[i].numthreads = numthr;
		sem_init( &winfo[i].sem_begin, 0, 0 );
		sem_init( &winfo[i].sem_end, 0, 0 );
		pthread_create( &winfo[i].thr, NULL, thread_tps, (void*)(winfo+i) );
	}
	
	sleep(1);

	for(uprate =0; uprate<=1.01; uprate+=0.1)
	{
		/// 1. tell each worker:  update ratio
		for(i=0; i<numthr; i++)
		{
			winfo[i].updaterate = uprate;
		}
		
		printf("\n\n=========== begin new round:  update-rate = %f\n", uprate );	
		allops = 0;

		gettimeofday( &tstart, NULL );
				
		/// 2. start all child threads
		for(i=0; i<numthr; i++){
			sem_post( &winfo[i].sem_begin );
		}
		
		
		//// 3. wait for all children to complete current round of work
		for(i=0; i<numthr; i++){
			sem_wait( &winfo[i].sem_end );
		}
		gettimeofday( &tend, NULL );
		
		//// 4. calculate tps
		tus = (tend.tv_sec - tstart.tv_sec) + (tend.tv_usec - tstart.tv_usec)/1000000.0;
		
		allops = allset = allget = 0;
		for(i=0; i<numthr; i++)
		{
			//allops += (winfo[i].opget+winfo[i].opset);
			allset += winfo[i].opset;
			allget += winfo[i].opget;			
		}
		allops = allset+allget;
		
		printf("uprate=%f: allops=%ld (set=%ld, get=%ld) in %f sec: tps = %ld op/s\n", 
			uprate, allops, allset, allget, tus, (long)(allops/tus) );
	}

	/// wait for all thr to exit
	for(i=0; i<numthr; i++ )
		pthread_join( winfo[i].thr, NULL );
		
	free( winfo );	
    ////////
	return 0;
	
	
}



////////////////////////////////////////////////////////////////////////




int main(int argc, char** argv)
{
	//// lookup slab-allocators	
	long lookup_mem_limit = 1024L*1024*4000;
	long lookup_min_chunksize=80;
	long lookup_max_chunksize=250;
	double lookup_factor = 1.1;
	
	/// hybrid-mem slab allocators
	long mem_limit = 1024L*1024 * 1024*65; //1024*70;
		//1024L*1024*1024*70; //1024*70; //1024*20; //1024*20; 
        //1800; //4500;//1024*256; // 1024*1024*8; // 1024L*1024*1024*4;
	long min_chunksize = 128; 
	long max_chunksize = 1024*1024; // *1024; // 1024; // max-key size
	double factor = 2;
	bool  prealloc = true;
	int use_mmap = 1;
	
	settings.chunk_size = 80;
	settings.item_size_max = 1024*1024; //*1024; //1024*1024; // 1024*1024; // 1024*1024;// size of one slab
	settings.verbose = 2;

	
	///////	
	printf("slabs_t size=%d, slabclass size=%d, item size=%d\n", sizeof(slabs_t), sizeof(slabclass_t), sizeof(item) );
	
    sprintf(HB_DIR, "/tmp/fio"); // HDD: dir to store the hybrid-mem files
    //sprintf(HB_DIR, "/tmp/fio/d1"); // dir to store the hybrid-mem files

	/// init item-header's slab-allocators	
	sb_htable = slabs_init( lookup_mem_limit, lookup_min_chunksize, lookup_max_chunksize, "lookup-table", 
		lookup_factor, false, 0, 0); 
		// const double factor, bool prealloc, int use_hybrid, int use_mmap ) 
		
	/// init the hybrid-mem slab-allocator, use memcached's settings
	sb_hbmem = slabs_init( mem_limit, min_chunksize, max_chunksize, "hybrid-mem", 
			factor, prealloc, 1, use_mmap ); 
	
	//// init hash-table
	assoc_init();
	

	//////////////
	if( argc > 1)
	{
		printf("multi-thr throughput test: use %d thr...\n", atoi(argv[1]));
		test_tps(atoi(argv[1]));
	}
	else
	{
		printf("single thread latency test...\n");
		test1();
	}
	////////////////////



	//// destroy the hash-table
	assoc_destroy();

	/// destroy the slab-allocators
	slabs_destroy(sb_hbmem);
	slabs_destroy(sb_htable);

    return 0;
}
