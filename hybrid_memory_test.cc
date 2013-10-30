#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "debug.h"
#include "hybrid_memory.h"
#include "sigsegv_handler.h"

static void TestHybridMemory() {
  uint32_t num_hmem_instances = 16;
  uint64_t page_buffer_size = PAGE_SIZE * 1000 * num_hmem_instances;
  uint64_t ram_buffer_size = PAGE_SIZE * 10000 * num_hmem_instances;
  uint64_t ssd_buffer_size = PAGE_SIZE * 100000 * num_hmem_instances;
  assert(InitHybridMemory("ssd",
                          "hmem",
                          page_buffer_size,
                          ram_buffer_size,
                          ssd_buffer_size,
                          num_hmem_instances) == true);

  uint64_t number_pages = 1000ULL * 1000 * 10;
  uint64_t buffer_size = number_pages * 4096;
  uint8_t *buffer = (uint8_t*)hmem_alloc(buffer_size);
  assert(buffer != NULL);

  dbg("before page fault...\n");
  sleep(5);

  dbg("start page fault...\n");
  struct timeval tstart, tend;
  gettimeofday(&tstart, NULL);
  for (uint64_t i = 0; i < number_pages; ++i) {
    uint64_t *pdata = (uint64_t*)(buffer + i * 4096 + 16);
    *pdata = i + 1;
  }
  gettimeofday(&tend, NULL);
  uint64_t total_usec = (tend.tv_sec - tstart.tv_sec) * 1000000 +
      (tend.tv_usec - tstart.tv_usec);
  uint64_t p1_faults = GetNumberOfPageFaults();
  printf("%ld page faults in %ld usec, %f usec/page\n",
         p1_faults,
         total_usec,
         (total_usec + 0.0) / p1_faults);

  dbg("will verify memory...\n");
  gettimeofday(&tstart, NULL);
  uint64_t count = 0;
  for (uint64_t i = 0; i < number_pages; ++i) {
    uint64_t *pdata = (uint64_t*)(buffer + i * 4096 + 16);
    if (*pdata != i + 1) {
      count++;
    }
    //assert(*pdata == i + 1);
  }
  gettimeofday(&tend, NULL);
  total_usec = (tend.tv_sec - tstart.tv_sec) * 1000000 +
      (tend.tv_usec - tstart.tv_usec);
  uint64_t p2_faults = GetNumberOfPageFaults() - p1_faults;
  printf("%ld page faults in %ld usec, %f usec/page\n",
         p2_faults,
         total_usec,
         (total_usec + 0.0) / p2_faults);

  dbg("will free memory...\n");
  sleep(5);
  hmem_free(buffer);
  ReleaseHybridMemory();
}

int main(int argc, char **argv) {
  TestHybridMemory();
  return 0;
}
