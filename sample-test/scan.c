#include <time.h>

struct row{
  int a[8192];
};
int main(int argc, char *argv[]){
  int length = atoi(argv[1]);
  int iters = atoi(argv[2]);

  struct row *rowArray = (struct row *) malloc(sizeof(struct row)*length);
  struct row tmp;
  
  int *array = rowArray[3].a;
  int i, j;
  array[2] = 8;

  struct timespec requestStart, requestEnd;
  clock_gettime(CLOCK_REALTIME, &requestStart);
  for (i=0; i<iters; i++){
    for (j=0; j<length; j++){
      tmp = rowArray[j];
    }
  }
  clock_gettime(CLOCK_REALTIME, &requestEnd);
  printf("hello %d\n", tmp.a);
  double accum = ( requestEnd.tv_sec - requestStart.tv_sec ) + ( requestEnd.tv_nsec - requestStart.tv_nsec ) / 1E6;
  printf( "%lf\n", accum );
  printf( "%lf\n", accum/iters );
  printf( "%lf\n", accum/(iters*length) );
  return 0;
}
