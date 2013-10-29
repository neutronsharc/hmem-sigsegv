


#define _GNU_SOURCE  // to use O_DIRECT
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define _XOPEN_SOURCE 600
#include <stdlib.h>


#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>

#include <sys/time.h>


unsigned long get_rand(unsigned long max_val)
{
    long v; // = rand();
    v = lrand48();
    return v%max_val;
}



int  rand_read(char* file, int unitsize)
{
	int fd;
	int ret;
	char *buf;
	int i, k;
	struct timeval t1, t2;
	double tus;
	long maxblk;
	
	fd = open(file, O_RDWR|O_DIRECT, 0666);
	if( fd <0 ){
		printf("fail to open file\n");
        perror("open failed !\n");
		return NULL;
	}
	
	buf = 0;
	ret = posix_memalign(&buf, 4096, 4096*4);
	//posix_memalign(void **memptr, size_t alignment, size_t size);
	if( ret!=0 || !buf){
		printf("fail to alloc mem\n");
		return 0;
	}
	
	
    /// init the random-gen
    gettimeofday(&t1, NULL);
    srand48( t1.tv_usec );
    
    int numblks = 10000;
    int blksize = unitsize;
    if( blksize < 512 )	blksize = 512;
    if( blksize%512 )
    	blksize += (512 - blksize%512);
    /// 
    maxblk = 1024L*1024*2000 / 512;
    
    long offset=0;
    
    gettimeofday(&t1, NULL);
    for(i=0; i<numblks; i++)
    {
    	k = get_rand(maxblk);
    	offset = k<<9;
    	
    	k = pread(fd, buf, blksize, offset);
    	if( k!=blksize ){
    		printf("error: %ld@%ld: ret=%d\n", blksize, offset, k);
    		perror("pread err...\n");
    		return 0;
    	}    
    }    
    gettimeofday(&t2, NULL);
	tus = (t2.tv_sec-t1.tv_sec)*1000000 + (t2.tv_usec-t1.tv_usec);
	printf("rand-read %d: unit-size=%d, total-time= %.f us,  avg= %.f us / op\n", 
		numblks, unitsize, tus, tus/numblks );
	
    
    free(buf);
	close(fd);
	return 0;		
}


int main(int argc, char** argv)
{
	//char	*file = "/tmp/randfile";
	char* file="/tmp/fio/ff1";
	
	int unit;
	
	for(unit=128; unit<=4096; unit*=2)
	{
		rand_read(file, unit);
	}
	
	return 0;
}
