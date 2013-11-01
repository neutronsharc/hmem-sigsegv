#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <pthread.h>

#include "debug.h"
#include "hybrid_memory_lib.h"
#include "hybrid_memory_const.h"

struct TaskItem {
  uint8_t *buffer;
  uint64_t size;
  uint64_t number_access;
  pthread_mutex_t lock;
  pthread_t  thread_id;
};

static void* AccessHybridMemoryWriteThenRead(void *arg) {
  struct timespec tstart, tend;
  clock_gettime(CLOCK_REALTIME, &tstart);
  struct TaskItem *task = (struct TaskItem*)arg;
  uint64_t number_of_pages = task->size >> PAGE_BITS;
  uint32_t rand_seed =
      (tstart.tv_nsec + (uint32_t)task->thread_id) % number_of_pages;
  dbg("thread %u: use rand-seed %d, num-pages %ld\n",
      (uint32_t)task->thread_id,
      rand_seed,
      number_of_pages);

  int64_t latency_ns = 0;
  int64_t max_write_latency_ns = 0;
  int64_t max_read_latency_ns = 0;
  pthread_mutex_lock(&task->lock);
  // Write round.
  for (uint64_t i = 0; i < task->number_access; ++i) {
    //uint32_t page_number = rand_r(&rand_seed) % number_of_pages;
    uint64_t page_number = i % number_of_pages;
    uint64_t* p = (uint64_t*)(task->buffer + (page_number << PAGE_BITS) + 16);
    clock_gettime(CLOCK_REALTIME, &tstart);
    *p = i;
    clock_gettime(CLOCK_REALTIME, &tend);
    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (latency_ns > max_write_latency_ns) {
      max_write_latency_ns = latency_ns;
    }
  }
  // Read round.
  for (uint64_t i = 0; i < task->number_access; ++i) {
    uint64_t page_number = i % number_of_pages;
    uint64_t* p = (uint64_t*)(task->buffer + (page_number << PAGE_BITS) + 16);
    clock_gettime(CLOCK_REALTIME, &tstart);
    if (*p != i) {
      err("vaddr %p: should be 0x%lx, data = %lx\n", p, i, *p);
    }
    clock_gettime(CLOCK_REALTIME, &tend);
    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (latency_ns > max_read_latency_ns) {
      max_read_latency_ns = latency_ns;
    }
  }
  pthread_mutex_unlock(&task->lock);
  dbg("Thread %u: max-write-latency = %f usec, max-read-lat = %f usec\n",
      (uint32_t)task->thread_id,
      max_write_latency_ns / 1000.0,
      max_read_latency_ns / 1000.0);
  pthread_exit(NULL);
}

static void* AccessHybridMemory(void *arg) {
  struct timespec tstart, tend;
  clock_gettime(CLOCK_REALTIME, &tstart);
  struct TaskItem *task = (struct TaskItem*)arg;
  uint64_t number_of_pages = task->size >> PAGE_BITS;
  uint32_t rand_seed =
      (tstart.tv_nsec + (uint32_t)task->thread_id) % number_of_pages;
  dbg("thread %u: use rand-seed %d, num-pages %ld\n",
      (uint32_t)task->thread_id,
      rand_seed,
      number_of_pages);

  int64_t latency_ns = 0;
  int64_t max_latency_ns = 0;
  pthread_mutex_lock(&task->lock);
  for (uint64_t i = 0; i < task->number_access; ++i) {
    //uint32_t page_number = rand_r(&rand_seed) % number_of_pages;
    uint32_t page_number = i % number_of_pages;
    uint8_t* p = (uint8_t*)(task->buffer + (page_number << PAGE_BITS) + 16);
    clock_gettime(CLOCK_REALTIME, &tstart);
    // 50% read, 50% write.
    if (page_number % 2 == 0) {  // write-access
      *p = 0xff;
    } else {
      if (*p != 0xff) {   // read-access
        err("vaddr %p: data = %x\n", p, *p);
      }
    }
    clock_gettime(CLOCK_REALTIME, &tend);
    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (latency_ns > max_latency_ns) {
      max_latency_ns = latency_ns;
    }
  }
  pthread_mutex_unlock(&task->lock);
  dbg("Thread %u: max-latency = %f usec\n",
      (uint32_t)task->thread_id,
      max_latency_ns / 1000.0);
  pthread_exit(NULL);
}

static void TestMultithreadAccess() {
  // Prepare hybrid-mem.
  uint64_t one_mega = 1024ULL * 1024;
  uint32_t num_hmem_instances = 8;
  uint64_t page_buffer_size = one_mega * 16;
  uint64_t ram_buffer_size = one_mega * 1000;
  uint64_t ssd_buffer_size = one_mega* 100000;
  assert(InitHybridMemory("ssd",
                          "hmem",
                          page_buffer_size,
                          ram_buffer_size,
                          ssd_buffer_size,
                          num_hmem_instances) == true);

  // Allocate a big virt-memory, shared by all threads.
  uint64_t number_pages = 1000ULL * 1000 * 10;
  uint64_t buffer_size = number_pages * 4096;
  uint8_t* buffer = (uint8_t*)hmem_alloc(buffer_size);
  assert(buffer != NULL);

  // Start parallel threads to access the virt-memory.
  uint32_t max_threads = 1;
  TaskItem tasks[max_threads];
  uint64_t number_access = 1000UL * 200;
      //ram_buffer_size / 4096 - 1; //1000ULL * 100;

  for (uint32_t number_threads = 1; number_threads <= max_threads; number_threads *= 2) {
    for (uint32_t i = 0; i < number_threads; ++i) {
      tasks[i].buffer = buffer;
      tasks[i].size = buffer_size;
      tasks[i].number_access = number_access;
      pthread_mutex_init(&tasks[i].lock, NULL);
      pthread_mutex_lock(&tasks[i].lock);
      assert(pthread_create(
                 //&tasks[i].thread_id, NULL, AccessHybridMemory, &tasks[i]) ==
                 &tasks[i].thread_id, NULL, AccessHybridMemoryWriteThenRead, &tasks[i]) ==
             0);
    }
    sleep(1);
    struct timeval tstart, tend;
    uint64_t p1_faults = GetNumberOfPageFaults();
    gettimeofday(&tstart, NULL);
    // Tell all theads to start.
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_mutex_unlock(&tasks[i].lock);
    }
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_join(tasks[i].thread_id, NULL);
    }
    gettimeofday(&tend, NULL);
    uint64_t p2_faults = GetNumberOfPageFaults();
    uint64_t number_faults = p2_faults - p1_faults;
    uint64_t total_usec = (tend.tv_sec - tstart.tv_sec) * 1000000 +
                          (tend.tv_usec - tstart.tv_usec);
    printf(
        "%d threads, %ld access, %ld page faults in %ld usec, %f usec/page\n"
        "throughput = %ld access / sec\n\n\n",
        number_threads,
        number_access * number_threads,
        number_faults,
        total_usec,
        (total_usec + 0.0) / number_faults,
        (uint64_t)(number_access * number_threads / (total_usec / 1000000.0)));
  }

  // Free hmem.
  hmem_free(buffer);
  ReleaseHybridMemory();
}

static void TestHybridMemory() {
  uint32_t num_hmem_instances = 64;
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
  //TestHybridMemory();
  TestMultithreadAccess();
  return 0;
}
