#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>

#include <string>

int main(int argc, char **argv) {
  uint64_t one_mega = 1024UL * 1024;
  uint64_t file_size = one_mega * 100;
  uint64_t total_file_pages = file_size / 4096;
  uint8_t buffer[4096];

  uint64_t repeats = total_file_pages * 1;
  std::string filename = "/tmp/hybridmemory/hddfile";

  bool sequential = false;

  int fd = open(filename.c_str(), O_RDWR | O_DIRECT, 0666);
  assert(fd > 0);

  struct timespec tstart, tend;
  clock_gettime(CLOCK_REALTIME, &tstart);
  uint32_t rand_seed = tstart.tv_nsec % total_file_pages;

  uint64_t latency_ns = 0;
  uint64_t max_read_latency_ns = 0;

  struct timespec sum_tstart, sum_tend;
  clock_gettime(CLOCK_REALTIME, &sum_tstart);
  for (uint64_t i = 0; i < repeats; ++i) {
    uint64_t target_page;
    if (sequential) {
      target_page = i % total_file_pages;
    } else {
      target_page = rand_r(&rand_seed) % total_file_pages;
    }
    uint64_t to_read = 4096;
    clock_gettime(CLOCK_REALTIME, &tstart);
    assert(pread(fd, buffer, to_read, target_page << 12) == to_read);
    clock_gettime(CLOCK_REALTIME, &tend);

    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (latency_ns > max_read_latency_ns) {
      max_read_latency_ns = latency_ns;
    }
    if (i && i % 1000 == 0) {
      printf("worker: %ld\n", i);
    }
  }
  clock_gettime(CLOCK_REALTIME, &sum_tend);
  uint64_t total_usec = (sum_tend.tv_sec - sum_tstart.tv_sec) * 1000000 +
                        (sum_tend.tv_nsec - sum_tstart.tv_nsec) / 1000;
  printf(
      "workload: \"%s\", %ld ops, %f seconds, avg-lat = %ld usec, "
      "throughput= %ld /sec, max-lat = %ld usec\n",
      sequential ? "sequential" : "random",
      repeats,
      total_usec / 1000000.0,
      total_usec / repeats,
      (uint64_t)(repeats / (total_usec / 1000000.0)),
      max_read_latency_ns / 1000);

  return 0;
}
