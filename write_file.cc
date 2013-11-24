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
  if (argc < 2) {
    printf("Create a file.\n"
           "Usage: %s  [file name] \n",
           argv[0]);
    return 0;
  }
  const char *hdd_filename = argv[1];
  int fd = open(hdd_filename, O_CREAT | O_LARGEFILE |O_RDWR, 0666);
  uint8_t buffer[4096];

  memset(buffer, 0xff, 4096);
  uint64_t hdd_file_size = 1024UL * 1024 * 150;
  uint64_t i;
  for (i = 0; i < hdd_file_size; i += 4096) {
    pwrite(fd, buffer, 4096, i);
  }

  printf("has written %ld bytes to hdd file %s\n", hdd_file_size, hdd_filename);
  close(fd);
  return 0;

}
