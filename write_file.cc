#define _FILE_OFFSET_BITS 64

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc != 4) {
    printf("Usage: %s [file name] [offset in MB] [size in MB]\n", argv[0]);
    return 0;
  }

  uint64_t meg = 1024L * 1024;
  const char *filename = argv[1];
  int offsetmb = atoi(argv[2]);
  int sizemb = atoi(argv[3]);

  uint64_t start_offset = offsetmb * meg;
  uint64_t write_total_size = sizemb * meg;
  uint64_t end_offset = start_offset + write_total_size;

  int fd = open(filename, O_CREAT | O_LARGEFILE |O_RDWR, 0666);

  uint8_t buffer[meg];
  memset(buffer, 0xff, meg);

  uint64_t offset;
  uint64_t written = 0;

  printf("will write to file %s [%ld - %ld]\n", filename, start_offset, end_offset);

  for (offset = start_offset; offset < end_offset; offset += meg) {
    uint64_t ret = pwrite(fd, buffer, meg, offset);
    if (ret != meg) {
      printf("write failed at file %s offset %ld, ret %ld\n", filename, offset, ret);
    }
    written += ret;
    if ((written / meg) % 1000 == 0) {
      printf("has written %ld MB to file %s\n", written / meg, filename);
    }
  }

  printf("has written %ld bytes to file %s at offset %ld\n", write_total_size, filename, start_offset);
  close(fd);
  return 0;

}
