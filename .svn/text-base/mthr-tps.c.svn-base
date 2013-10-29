// #define __USE_GNU
#define _GNU_SOURCE

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <pthread.h>

/// for open/read/write/close
#include <unistd.h>
#include <fcntl.h>
#include <sys/time.h>

#include <sys/types.h>
#include <attr/xattr.h>

#include <semaphore.h>
#include <pthread.h>


//#include </usr/include/bits/fcntl.h>

#if 1
#define dbg(fmt, args... )  printf( "%s: "fmt, __func__,  ##args  )
#else
#define dbg(fmt, args... )
#endif

///////  multi-thread ram-SSD hybrid mem throughput test


typedef struct work_info_s
{
	pthread_t	thr;
	int myid;
		
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


unsigned long get_rand(unsigned long max_val)
{
    long v; // = rand();
    v = lrand48();
    return v%max_val;
}


void*	thread_tps(void* arg)
{
	work_info_t	*winfo = (work_info_t*)arg;
	
	dbg("thr_%d started...\n", winfo->myid);

	int num_sizes = 1;
	int sizes[6] = {1020}; //{512, 1000, 2000, 3000, 4000};  // value sizes
	
#if 1
	long total_numitems = 1000*1000*20; //1000*1000*10; 
	long total_ops = 400000; 
#else
	long total_numitems = 100000;
	long total_ops = 40000; 
#endif

	long i, j;
	long numitems = total_numitems / numprocs;  // each procs inserts this many items
	long myops = total_ops / numprocs;   //then, each proc performs this many trans	
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
        	printf("[thr_%d]: create finished items: %d\n", winfo->myid);
        }
	}
		
		
	int opselect=0;
	int thresh = 0;
		
	while(1)
	{
		/////  wait to be woken up
		sem_wait( &winfo->sem_begin );
		dbg("thr_%d: uprate=%f\n", winfo->myid, winfo->updaterate );

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
					if( tn != cnt ){
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
	long	allops;
	
	work_info_t	*winfo;
	winfo = (work_info_t*)malloc( numthr * sizeof(work_info_t) );

    /// init the random-gen
    gettimeofday(&tstart, NULL);
    srand48( tstart.tv_usec );


	//// 1. prep threads
	for(i=0; i<numthr; i++){
		winfo[i].myid = i;
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
			winfo[i].total_ops = 1;
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
		
		for(i=0; i<numthr; i++)
			allops += (winfo[i].opget+winfo[i].opset);

		printf("uprate=%f: allops=%ld in %f sec: tps = %ld op/s\n", uprate, allops, tus, 
			(long)(allops/tus) );
	}

	/// wait for all thr to exit
	for(i=0; i<numthr; i++ )
		pthread_join( winfo[i].thr, NULL );
		
	free( winfo );	
    ////////
	return 0;
	
	
}




int main(int argc, char** argv)
{
	if( argc < 3 ){
		//printf("Usage: %s [ 0: src / 1: tgt ] [ num-threads]\n",argv[0]);
		//return -1;
	}

	test_tps(4);
	
	return 0;	
	

}
