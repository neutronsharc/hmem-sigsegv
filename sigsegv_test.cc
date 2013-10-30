#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>

#include "debug.h"
#include "sigsegv_handler.h"
#include "hybrid_memory.h"
#include "vaddr_range.h"

static void TestSigseg() {
  SigSegvHandler handler;
  assert(handler.InstallHandler() == true);

  // NOTE::  in order to malloc huge size, shall let linux kernel use:
  // sysctl -w vm.overcommit_memory=1
  uint64_t number_pages = 1000ULL * 1000;
  uint64_t buffer_size = number_pages * 4096;
  uint64_t alignment = 4096;
  uint8_t *buffer;
  assert(posix_memalign((void**)(&buffer), alignment, buffer_size) == 0);
  assert(mprotect(buffer, buffer_size, PROT_NONE) == 0);

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
  for (uint64_t i = 0; i < number_pages; ++i) {
    uint64_t *pdata = (uint64_t*)(buffer + i * 4096 + 16);
    assert(*pdata == i + 1);
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
  free(buffer);
  assert(handler.UninstallHandler() == true);
}

int main(int argc, char **argv) {
  TestSigseg();
  return 0;
}
