// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

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

#define USE_MMAP

struct TaskItem {
  uint8_t *buffer;
  uint64_t size;
  uint64_t number_access;
  pthread_mutex_t lock;
  pthread_t  thread_id;
  bool sequential;  // is it sequential / random workload?

  uint64_t begin_page;  // can access pages staring fro this.
  uint64_t number_pages; // size of page range to access by this task.

  uint32_t id;
  uint32_t total_tasks;
  uint64_t* expected_perpage_data;
  uint64_t actual_number_access;
};

static void* AccessHybridMemoryRandomAccess(void *arg) {
  struct timespec tstart, tend;
  clock_gettime(CLOCK_REALTIME, &tstart);
  struct TaskItem *task = (struct TaskItem*)arg;
  uint64_t number_of_pages = task->size >> PAGE_BITS;
  uint32_t rand_seed =
      (tstart.tv_nsec + (uint32_t)task->thread_id) % number_of_pages;

  int64_t latency_ns = 0;
  int64_t max_write_latency_ns = 0;
  int64_t max_read_latency_ns = 0;
  uint64_t target_page_number;
  uint64_t faults_step1, faults_step2;
  dbg("thread %d: work on file page range [%ld - %ld), %ld accesses\n",
      task->id,
      task->begin_page,
      task->begin_page + task->number_pages,
      task->number_access);

  HybridMemoryStats();
  dbg("thread %d: found-pages=%ld, unfound-pages=%ld\n",
      task->id,
      FoundPages(),
      UnFoundPages());
  faults_step1 = NumberOfPageFaults();
  //////////////////////////////////////////
  pthread_mutex_lock(&task->lock);
  for (uint64_t i = 0; i < task->number_access; ++i) {
    target_page_number =
        task->begin_page + rand_r(&rand_seed) % task->number_pages;
    uint64_t* p =
      (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    // 50% : 50% read--write.
    bool read = true;
    if (rand_r(&rand_seed) % 100 < 50) {
      read = false;
    }
    clock_gettime(CLOCK_REALTIME, &tstart);
    if (read) {
      assert(*p == task->expected_perpage_data[target_page_number]);
    } else {
      *p = rand_r(&rand_seed);
      task->expected_perpage_data[target_page_number] = *p;
    }
    clock_gettime(CLOCK_REALTIME, &tend);
    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (read && latency_ns > max_read_latency_ns) {
      max_read_latency_ns = latency_ns;
    } else if (latency_ns > max_write_latency_ns) {
      max_write_latency_ns = latency_ns;
    }
    if (i && i % 2000 == 0) {
      printf("thread %d: read: %ld\n", task->id, i);
    }
    ++task->actual_number_access;
  }
  faults_step2 = NumberOfPageFaults();
  pthread_mutex_unlock(&task->lock);
  printf("\n\n");
  HybridMemoryStats();
  dbg("thread %d: found-pages=%ld, unfound-pages=%ld\n",
      task->id,
      FoundPages(),
      UnFoundPages());
  // Report stats.
  dbg("Thread %d: random-50/50 read/write, max-write-latency = %f usec, "
      "max-read-lat = %f usec\n\t\tsee page faults=%ld\n",
      task->id,
      max_write_latency_ns / 1000.0,
      max_read_latency_ns / 1000.0,
      faults_step2 - faults_step1);
  pthread_exit(NULL);
}

static void* AccessHybridMemoryWriteThenRead(void *arg) {
  struct timespec tstart, tend;
  clock_gettime(CLOCK_REALTIME, &tstart);
  struct TaskItem *task = (struct TaskItem*)arg;
  uint64_t number_of_pages = task->size >> PAGE_BITS;
  uint32_t rand_seed =
      (tstart.tv_nsec + (uint32_t)task->thread_id) % number_of_pages;
  dbg("Thread %u: use rand-seed %d, num-pages %ld\n",
      task->id,  //(uint32_t)task->thread_id,
      rand_seed,
      number_of_pages);

  int64_t latency_ns = 0;
  int64_t max_write_latency_ns = 0;
  int64_t max_read_latency_ns = 0;
  uint64_t target_page_number;
  uint64_t faults_step1, faults_step2, faults_step3;

  printf("thread %d: work on file page range [%ld - %ld), %ld accesses\n",
         task->id,
         task->begin_page,
         task->begin_page + task->number_pages,
         task->number_access);
  //////////////////////////////////////////
  pthread_mutex_lock(&task->lock);
  // If we use hdd-file backed mmap(), the init data is all "F".
#ifdef USE_MMAP
  if (task->sequential) {
    for (uint64_t i = 0; i < task->number_access; ++i) {
      target_page_number = task->begin_page + (i % task->number_pages);
      uint64_t* p =
          (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
      assert(*p == 0xFFFFFFFFFFFFFFFF);
      if (i && i % 1000 == 0) {
        printf("use-mmap: prefault: %ld\n", i);
      }
      ++task->actual_number_access;
    }
  }
  HybridMemoryStats();
#endif
  faults_step1 = NumberOfPageFaults();
  //////////////// Write round: sequential write to every page.
  for (uint64_t i = 0; i < task->number_access; ++i) {
    target_page_number = task->begin_page + (i % task->number_pages);
    uint64_t* p =
      (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    clock_gettime(CLOCK_REALTIME, &tstart);
    if (i < (1ULL << 19) - 10) {
      //assert(*p == 0xFFFFFFFFFFFFFFFF);
    }
    *p = target_page_number;
    clock_gettime(CLOCK_REALTIME, &tend);
    ++task->actual_number_access;
    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (latency_ns > max_write_latency_ns) {
      max_write_latency_ns = latency_ns;
    }
    if (i && i % 1000 == 0) {
      printf("worker: write : %ld\n", i);
    }
  }
  faults_step2 = NumberOfPageFaults();
  dbg("hmem found-pages=%ld, unfound-pages=%ld\n", FoundPages(), UnFoundPages());
  HybridMemoryStats();
  /////////////////// Read round.
  for (uint64_t i = 0; i < task->number_access; ++i) {
    if (task->sequential) {
      target_page_number = task->begin_page + (i % task->number_pages);
    } else {
      target_page_number =
        task->begin_page + rand_r(&rand_seed) % task->number_pages;
    }
    uint64_t* p =
      (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    clock_gettime(CLOCK_REALTIME, &tstart);
    if (*p != target_page_number) {
      if (task->sequential) {
        err("vaddr %p: should be 0x%lx, data = %lx\n", p, i, *p);
      } else {
        err("vaddr %p: should be 0x%lx, data = %lx\n", p, target_page_number, *p);
      }
    }
    clock_gettime(CLOCK_REALTIME, &tend);
    ++task->actual_number_access;
    latency_ns = (tend.tv_sec - tstart.tv_sec) * 1000000000 +
                 (tend.tv_nsec - tstart.tv_nsec);
    if (latency_ns > max_read_latency_ns) {
      max_read_latency_ns = latency_ns;
    }
    if (i && i % 1000 == 0) {
      printf("worker: read: %ld\n", i);
    }
  }
  faults_step3 = NumberOfPageFaults();
  pthread_mutex_unlock(&task->lock);
  dbg("hmem found-pages=%ld, unfound-pages=%ld\n", FoundPages(), UnFoundPages());
  HybridMemoryStats();
  //////////////////////////////////////////

  // Report stats.
  dbg("Thread %d: %s-access: max-write-latency = %f usec, max-read-lat = %f usec\n"
      "\t\twrite-round page faults=%ld, read-round page-faults = %ld\n",
      task->id,
      task->sequential ? "sequential" : "random",
      max_write_latency_ns / 1000.0,
      max_read_latency_ns / 1000.0,
      faults_step2 - faults_step1, faults_step3 - faults_step2);
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
  uint32_t num_hmem_instances = 1;
  uint64_t page_buffer_size = one_mega * 1;
  uint64_t ram_buffer_size = one_mega * 10; //200;
  uint64_t ssd_buffer_size = one_mega * 50; //16 * 128;
  uint64_t hdd_file_size = one_mega * 100; //5000;
  assert(InitHybridMemory("/tmp/hybridmemory/",
                          "hmem",
                          page_buffer_size,
                          ram_buffer_size,
                          ssd_buffer_size,
                          num_hmem_instances) == true);

  // Allocate a big virt-memory, shared by all threads.
#ifdef USE_MMAP
  uint64_t buffer_size = hdd_file_size;
  uint64_t number_pages = buffer_size / 4096;
  uint64_t hdd_file_offset = one_mega * 10;
  uint8_t* buffer = (uint8_t*)hmem_map(
      "/tmp/hybridmemory/hddfile", buffer_size, hdd_file_offset);
  assert(buffer != NULL);
  dbg("Use hmem-map()\n");
  uint64_t real_memory_pages = ram_buffer_size / 4096;
  uint64_t access_memory_pages = hdd_file_size / 4096;
#else
  uint64_t number_pages = 1000ULL * 1000 * 10;
  uint64_t buffer_size = number_pages * 4096;
  uint8_t* buffer = (uint8_t*)hmem_alloc(buffer_size);
  assert(buffer != NULL);
  dbg("Use hmem-alloc()\n");
#endif

  uint64_t* expected_perpage_data = new uint64_t[number_pages];
  assert(expected_perpage_data != NULL);
  dbg("Prepare expected_data array: %p, size = %ld\n",
      expected_perpage_data, sizeof(uint64_t) * number_pages);
  for (uint64_t i = 0; i < number_pages; ++i) {
    expected_perpage_data[i] = 0xffffffffffffffff;
  }

  // Start parallel threads to access the virt-memory.
  uint32_t max_threads = 1;
  TaskItem tasks[max_threads];
  uint64_t number_access = access_memory_pages;
  //uint64_t number_access = 1000UL * 25;

  for (uint32_t number_threads = max_threads; number_threads <= max_threads;
       number_threads *= 2) {
    uint64_t per_task_pages = access_memory_pages / number_threads;
    uint64_t per_task_access = number_access / number_threads;
    uint64_t begin_page = 0;
    for (uint32_t i = 0; i < number_threads; ++i) {
      tasks[i].begin_page = begin_page;
      tasks[i].number_pages = per_task_pages;
      tasks[i].number_access = per_task_access;
      tasks[i].actual_number_access = 0;
      tasks[i].expected_perpage_data = expected_perpage_data;
      begin_page += per_task_pages;

      tasks[i].buffer = buffer;
      tasks[i].size = buffer_size;
      tasks[i].sequential = false;
      tasks[i].id = i;
      tasks[i].total_tasks = number_threads;

      pthread_mutex_init(&tasks[i].lock, NULL);
      pthread_mutex_lock(&tasks[i].lock);
      assert(pthread_create(
                 &tasks[i].thread_id,
                 NULL,
                 //AccessHybridMemoryWriteThenRead,
                 AccessHybridMemoryRandomAccess,
                 &tasks[i]) == 0);
    }
    sleep(1);
    struct timeval tstart, tend;
    uint64_t p1_faults = NumberOfPageFaults();
    gettimeofday(&tstart, NULL);
    // Tell all theads to start.
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_mutex_unlock(&tasks[i].lock);
    }
    uint64_t total_accesses = 0;
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_join(tasks[i].thread_id, NULL);
      total_accesses += tasks[i].actual_number_access;
    }
    gettimeofday(&tend, NULL);
    uint64_t p2_faults = NumberOfPageFaults();
    uint64_t number_faults = p2_faults - p1_faults;
    uint64_t total_usec = (tend.tv_sec - tstart.tv_sec) * 1000000 +
                          (tend.tv_usec - tstart.tv_usec);
    // Each worker thread does one write-round and one read-round.
    printf(
        "\n----------------------- Stats --------------------\n"
        "%d threads, %ld access, %ld page faults in %ld usec, %f usec/page\n"
        "throughput = %ld access / sec\n\n",
        number_threads,
        total_accesses,
        number_faults,
        total_usec,
        (total_usec + 0.0) / number_faults,
        (uint64_t)(total_accesses / (total_usec / 1000000.0)));
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
  uint64_t p1_faults = NumberOfPageFaults();
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
  uint64_t p2_faults = NumberOfPageFaults() - p1_faults;
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
