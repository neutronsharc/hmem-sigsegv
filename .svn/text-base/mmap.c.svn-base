


#define _GNU_SOURCE  // to use O_DIRECT
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <stdio.h>
#include <errno.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>




void*  map_file(char* file, size_t len, int *pfd)
{
	int fd;
	int ret;
	
	//fd = open(file, O_CREAT|O_RDWR|O_DIRECT, 0666);
	fd = open(file, O_CREAT|O_RDWR, 0600);
	//fd = open(file, O_CREAT|O_RDWR|O_TRUNC, 0666);
	if( fd <0 ){
		printf("fail to open map file\n");
        perror("open failed !\n");
		return NULL;
	}
	
	ret = ftruncate( fd, len );
	if( ret ){
		printf("trunc failed: file=%s, len=%ld:  %s\n", file, len, strerror(errno) );
		close(fd);
		return NULL;
	}

    /*if (unlink(file) != 0) {
       perror("unlink");
       return 0; 
    } */
	
	//void *m = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, (off_t)0 ); 
	void *m = mmap(NULL, len, PROT_READ|PROT_WRITE, MAP_SHARED, fd, (off_t)0 ); 
    //directIO cannot support "MAP_SHARED" mmap 
	if( m==(void*)-1 ){
		printf("mmap failed::  file=%s:  %s\n", file, strerror(errno) );
		close(fd);
		return NULL;
	}
	printf("map %s: len=%ld, ret=%p\n", file, len, m);
	*pfd = fd;
	return m;	
}


int main(int argc, char** argv)
{
	size_t	len;
	int fd;
	void*	m;
	
	len = 1024L*1024*1024*100; //*1024*1024*20; //1024; //*100;
	m = map_file("/tmp/pfs/myfile", len, &fd);
    if(!m)  return;

    char* ch = m;
    size_t  cnt;
    //for(cnt=0; cnt<len; cnt++)
    //    *(ch+cnt) = cnt%256;

    //strcpy(m+len-90, "hello");
    //strcpy(m+90, "hello");	
    long i;
    int k;
    for(i=0, k=0; i<len; i+=1024*1024*1024, k++)
    {
        *((char*)(m+i+10))=0;
	    printf("offset %d: = 0x%lx: \"%s\"\n", k, i, (char*)(m+i));       

    }

	//printf("map-content: \"%s\"\n", (char*)(m+len-90));
    /*if( msync(m, len, MS_SYNC)!=0 ){
        perror("msync failed!!\n");
    }*/
	
	if( m )	munmap(m, len);	
	close(fd);

	return 0;
}
