#define _FILE_OFFSET_BITS 64
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  printf("size of pthread_mutex = %ld\n", sizeof(pthread_mutex_t));

  const char *hdd_filename = "/tmp/hybridmemory/hddfile";
  int fd = open(hdd_filename, O_CREAT | O_LARGEFILE |O_RDWR, 0666);
  uint8_t buffer[4096];

  memset(buffer, 0xff, 4096);
  uint64_t hdd_file_size = 1024UL * 1024 * 2100;
  uint64_t i;
  for (i = 0; i < hdd_file_size; i += 4096) {
    pwrite(fd, buffer, 4096, i);
  }

  printf("has written %ld bytes to hdd file %s\n", hdd_file_size, hdd_filename);
  close(fd);
  return 0;

}
