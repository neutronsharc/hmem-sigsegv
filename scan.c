#include <time.h>
#include "myhmem.h"
#include "vaddr.h"
#include "avl.h"

struct row{
  int a[8192];
};

int main(int argc, char *argv[]){
  char *path = argv[1];
  int length = atoi(argv[2]);
  int iters = atoi(argv[3]);

  struct row *rowArray;

  // init ssd-hybrid-mem
  init_vaddr(path); //"/tmp/fio/ouyangx");
  hbmem_alloc_object(8192 * sizeof(int), length, (void**)&rowArray);
  // alloc "length" objs, each objsize=32k

  //struct row *rowArray = (struct row *) malloc(sizeof(struct row)*length);
  struct row tmp;
  struct timespec requestStart, requestEnd;
  
  int *array = rowArray[3].a;
  int i, j;
  array[2] = 8;


  /////////////
  clock_gettime(CLOCK_REALTIME, &requestStart);
  for (j=0; j<length; j++){
    rowArray[j].a[2] = j;
  }
  clock_gettime(CLOCK_REALTIME, &requestEnd);
  double wtime = (requestEnd.tv_sec - requestStart.tv_sec) +
    (requestEnd.tv_nsec - requestStart.tv_nsec) / 1E9;
  printf("fault %d objects costs time = %lf sec\n", length, wtime);
  dump_slabs( sb_hbmem );
  show_evict_stats();
  show_load_stats();
  /////////////

  clock_gettime(CLOCK_REALTIME, &requestStart);
  for (i=0; i<iters; i++){
    for (j=0; j<length; j++){
      tmp = rowArray[j];
      if (tmp.a[2] != j) {
        printf("item %d: tmp.a[2] = %d, should be %d\n", j, tmp.a[2], j);
        return -1;
      }
    }
  }
  clock_gettime(CLOCK_REALTIME, &requestEnd);

  ////////////////
  dump_slabs( sb_hbmem );
  show_evict_stats();
  show_load_stats();
  ////////////////

  printf("hello hybrid-mem addr: %p\n", tmp.a);
  double accum = ( requestEnd.tv_sec - requestStart.tv_sec ) + ( requestEnd.tv_nsec - requestStart.tv_nsec ) / 1E9;
  printf( "cumu-time(sec): %lf\n", accum );
  printf( "per-iter time(sec): %lf\n", accum/iters );
  printf( "avg access latency(sec): %lf\n", accum/(iters*length) );
  printf( "access  throughput per sec: %lf\n", (iters * length) / accum);

  //////////////
  hbmem_free(rowArray);
  destroy_vaddr();
  /////////////
  
  return 0;
}
