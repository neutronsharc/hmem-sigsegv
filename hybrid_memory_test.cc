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
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#include "debug.h"
#include "hybrid_memory_lib.h"
#include "hybrid_memory_const.h"
#include "utils.h"


#define USE_MMAP

uint64_t GetSum(std::vector<uint32_t> values) {
  uint64_t sum = 0;
  for (uint64_t i = 0; i < values.size(); ++i) {
    sum += values[i];
  }
  return sum;
}

struct TaskItem {
  uint8_t *buffer;
  uint64_t size;
  uint64_t number_access;
  pthread_mutex_t lock_1;
  pthread_mutex_t lock_2;
  pthread_t  thread_id;
  bool sequential;  // is it sequential / random workload?

  uint64_t begin_page;  // can access pages staring fro this.
  uint64_t number_pages; // size of page range to access by this task.

  uint32_t id;
  uint32_t total_tasks;
  uint32_t read_write_ratio;  // read:write ratio.
  // 0: write-only; 50: half-half, 100: read-only

  uint64_t actual_number_access;
  uint64_t* expected_perpage_data;

  uint32_t* read_latency_usec;
  uint64_t  number_reads;
  uint32_t* write_latency_usec;
  uint64_t  number_writes;
  uint64_t max_read_latency_usec;
  uint64_t max_write_latency_usec;
  uint64_t workload_time_usec;  // how much time spent to run the workload.
};

static void* AccessHybridMemoryRandomAccess(void *arg) {
  struct TaskItem *task = (struct TaskItem*)arg;
  uint64_t total_number_of_pages = task->size >> PAGE_BITS;
  uint32_t rand_seed =
      (NowInUsec() + (uint32_t)task->thread_id) % total_number_of_pages;

  task->read_latency_usec = new uint32_t[task->number_access];
  task->write_latency_usec = new uint32_t[task->number_access];
  assert(task->read_latency_usec && task->write_latency_usec);

  dbg("thread %d: work on file page range [%ld - %ld), %ld accesses\n",
      task->id,
      task->begin_page,
      task->begin_page + task->number_pages,
      task->number_access);
  dbg("thread %d: found-pages=%ld, unfound-pages=%ld\n",
      task->id,
      FoundPages(),
      UnFoundPages());
  uint64_t faults_step1, faults_step2;

  //////////////////////////////////////////
  // Prefault the pages.
  pthread_mutex_lock(&task->lock_1);
  for (uint64_t i = 0; i < task->number_access; ++i) {
    uint64_t target_page_number = task->begin_page + (i % task->number_pages);
    uint64_t* p =
        (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    *p = rand_r(&rand_seed);
    task->expected_perpage_data[target_page_number] = *p;
    if (i && i % 10000 == 0) {
      printf("Task %d: prefault: %ld\n", task->id, i);
    }
  }
  pthread_mutex_unlock(&task->lock_1);

  //////////////////////////////////////////
  // Start workload.
  faults_step1 = NumberOfPageFaults();
  HybridMemoryStats();
  pthread_mutex_lock(&task->lock_2);
  uint64_t begin_usec = NowInUsec();
  for (uint64_t i = 0; i < task->number_access; ++i) {
    uint64_t target_page_number =
        task->begin_page + rand_r(&rand_seed) % task->number_pages;
    uint64_t* p =
      (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    // 50% : 50% read--write.
    bool read = false;
    if (rand_r(&rand_seed) % 100 < task->read_write_ratio) {
      read = true;
    }
    uint64_t begin_usec = NowInUsec();
    if (read) {
      assert(*p == task->expected_perpage_data[target_page_number]);
    } else {
      *p = rand_r(&rand_seed);
      task->expected_perpage_data[target_page_number] = *p;
    }
    uint64_t latency_usec = NowInUsec() - begin_usec;
    if (read && latency_usec > task->max_read_latency_usec) {
      task->max_read_latency_usec = latency_usec;
    } else if (latency_usec > task->max_write_latency_usec) {
      task->max_write_latency_usec = latency_usec;
    }
    if (read) {
      task->read_latency_usec[task->number_reads++] = latency_usec;
    } else {
      task->write_latency_usec[task->number_writes++] = latency_usec;
    }
    if (i && i % 10000 == 0) {
      dbg("Thread %d: random-work r-w ratio %d: %ld\n",
          task->id, task->read_write_ratio, i);
    }
    ++task->actual_number_access;
  }
  task->workload_time_usec = NowInUsec() - begin_usec;
  faults_step2 = NumberOfPageFaults();
  pthread_mutex_unlock(&task->lock_2);

  dbg("Thread %d: found-pages=%ld, unfound-pages=%ld\n",
      task->id,
      FoundPages(),
      UnFoundPages());
  // Report stats.
  dbg("Thread %d: read-write ratio %ld: %ld reads, %ld writes, page faults=%ld\n",
      task->id,
      task->read_write_ratio,
      task->number_reads,
      task->number_writes,
      faults_step2 - faults_step1);
  printf("\n\n");
  HybridMemoryStats();
  pthread_exit(NULL);
}

static void* AccessHybridMemoryWriteThenRead(void *arg) {
  struct TaskItem *task = (struct TaskItem*)arg;
  uint64_t total_number_of_pages = task->size >> PAGE_BITS;
  uint32_t rand_seed =
      (NowInUsec() + (uint32_t)task->thread_id) % total_number_of_pages;

  int64_t max_write_latency_usec = 0;
  int64_t max_read_latency_usec = 0;
  uint64_t faults_step1, faults_step2, faults_step3;

  dbg("Thread %d: work on file page range [%ld - %ld), %ld accesses\n",
      task->id,
      task->begin_page,
      task->begin_page + task->number_pages,
      task->number_access);
  //////////////////////////////////////////
  // Warm up the cache layers.
  // If we use hdd-file backed mmap(), the init data is all "F".
  pthread_mutex_lock(&task->lock_1);
#ifdef USE_MMAP
  for (uint64_t i = 0; i < task->number_access; ++i) {
    uint64_t target_page_number = task->begin_page + (i % task->number_pages);
    uint64_t* p =
        (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    if (task->sequential) {
      // Assume the backing hdd file is all FF at beinning.
      assert(*p == 0xFFFFFFFFFFFFFFFF);
    }
    if (i && i % 2000 == 0) {
      dbg("Thread %d: prefault: %ld\n", task->id, i);
    }
  }
#endif
  pthread_mutex_unlock(&task->lock_1);
  faults_step1 = NumberOfPageFaults();
  HybridMemoryStats();

  pthread_mutex_lock(&task->lock_2);
  //////////////////////////////////////////
  // Write round: sequential write to every page.
  for (uint64_t i = 0; i < task->number_access; ++i) {
    uint64_t target_page_number = task->begin_page + (i % task->number_pages);
    uint64_t* p =
      (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    uint64_t begin_usec = NowInUsec();
    *p = target_page_number;
    uint64_t latency_usec = NowInUsec() - begin_usec;
    if (latency_usec > max_write_latency_usec) {
      max_write_latency_usec = latency_usec;
    }
    if (i && i % 2000 == 0) {
      dbg("Thread %d: write: %ld\n", task->id, i);
    }
    ++task->actual_number_access;
  }
  faults_step2 = NumberOfPageFaults();
  dbg("Thread %d: seq-write round: hmem found-pages=%ld, unfound-pages=%ld\n",
      task->id, FoundPages(), UnFoundPages());
  HybridMemoryStats();
  //////////////////////////////////////////
  /////////////////// Read round.
  for (uint64_t i = 0; i < task->number_access; ++i) {
    uint64_t target_page_number;
    if (task->sequential) {
      target_page_number = task->begin_page + (i % task->number_pages);
    } else {
      target_page_number =
        task->begin_page + rand_r(&rand_seed) % task->number_pages;
    }
    uint64_t* p =
      (uint64_t*)(task->buffer + (target_page_number << PAGE_BITS) + 16);
    uint64_t begin_usec = NowInUsec();
    if (*p != target_page_number) {
      if (task->sequential) {
        err("vaddr %p: should be 0x%lx, data = %lx\n", p, i, *p);
      } else {
        err("vaddr %p: should be 0x%lx, data = %lx\n", p, target_page_number, *p);
      }
    }
    ++task->actual_number_access;
    uint64_t latency_usec = NowInUsec() - begin_usec;
    if (latency_usec > max_read_latency_usec) {
      max_read_latency_usec = latency_usec;
    }
    if (i && i % 2000 == 0) {
      dbg("Thread %d: read: %ld\n", task->id, i);
    }
  }
  faults_step3 = NumberOfPageFaults();
  pthread_mutex_unlock(&task->lock_2);
  dbg("Thread %d: read-round, hmem found-pages=%ld, unfound-pages=%ld\n",
      task->id, FoundPages(), UnFoundPages());
  HybridMemoryStats();
  //////////////////////////////////////////

  // Report stats.
  dbg("Thread %d: %s-access: max-write-latency = %ld usec, max-read-lat = %ld usec\n"
      "\t\twrite-round page faults=%ld, read-round page-faults = %ld\n",
      task->id,
      task->sequential ? "sequential" : "random",
      max_write_latency_usec,
      max_read_latency_usec,
      faults_step2 - faults_step1, faults_step3 - faults_step2);
  pthread_exit(NULL);
}

static void TestMultithreadAccess(char* flash_dir, char* hdd_file) {
  // Prepare hybrid-mem.
  uint32_t max_threads = 1;
  uint32_t num_hmem_instances = 1;
  // r-w ratio:   100: read only,  50: r/w half,  0: write-only.
  uint32_t read_write_ratio = 50;

  uint64_t one_mega = 1024ULL * 1024;
  uint64_t page_buffer_size = 4096UL * 16; //one_mega * 1;
  uint64_t ram_buffer_size = one_mega * 96; //200;
  uint64_t ssd_buffer_size = one_mega * 50; //16 * 128;
  uint64_t hdd_file_size = one_mega * 50; //5000;
  if (!IsDir(flash_dir)) {
    err("Please give a flash dir: \"%s\" is not a dir\n", flash_dir);
    return;
  }
  assert(InitHybridMemory(flash_dir,
                          "hmem",
                          page_buffer_size,
                          ram_buffer_size,
                          ssd_buffer_size,
                          num_hmem_instances) == true);

  // Allocate a big virt-memory, shared by all threads.
#ifdef USE_MMAP
  if (!hdd_file || !IsFile(hdd_file)) {
    err("Please provide a valid hdd file.\n");
    return;
  }
  std::string hdd_file_s = hdd_file;

  uint64_t vaddress_space_size = hdd_file_size;
  uint64_t total_number_pages = vaddress_space_size / 4096;
  uint64_t hdd_file_offset = one_mega * 0;
  uint8_t *virtual_address =
      (uint8_t *)hmem_map(hdd_file_s, vaddress_space_size, hdd_file_offset);
  assert(virtual_address != NULL);
  uint64_t real_memory_pages = ram_buffer_size / 4096;
  uint64_t access_vaddr_pages = hdd_file_size / 4096;
  dbg("Use hmem-map()\n");
#else
  uint64_t total_number_pages = 1000ULL * 1000 * 10;
  uint64_t vaddress_space_size = total_number_pages * 4096;
  uint8_t* virtual_address = (uint8_t*)hmem_alloc(vaddress_space_size);
  assert(virtual_address != NULL);
  uint64_t real_memory_pages = ram_buffer_size / 4096;
  uint64_t access_vaddr_pages = ssd_buffer_size / 4096;
  dbg("Use hmem-alloc()\n");
#endif

  uint64_t* expected_perpage_data = new uint64_t[total_number_pages];
  assert(expected_perpage_data != NULL);
  dbg("Prepare expected_data array: %p, size = %ld\n",
      expected_perpage_data, sizeof(uint64_t) * total_number_pages);
  for (uint64_t i = 0; i < total_number_pages; ++i) {
    expected_perpage_data[i] = 0xffffffffffffffff;
  }

  // Start parallel threads to access the virt-memory.
  TaskItem tasks[max_threads];
  uint64_t total_number_access = access_vaddr_pages;

  for (uint32_t number_threads = max_threads; number_threads <= max_threads;
       number_threads *= 2) {
    memset(tasks, 0, sizeof(TaskItem) * max_threads);
    uint64_t per_task_pages = access_vaddr_pages / number_threads;
    uint64_t per_task_access = total_number_access / number_threads;
    uint64_t begin_page = 0;
    for (uint32_t i = 0; i < number_threads; ++i) {
      tasks[i].id = i;
      tasks[i].begin_page = begin_page;
      tasks[i].number_pages = per_task_pages;
      tasks[i].number_access = per_task_access;
      tasks[i].actual_number_access = 0;
      tasks[i].expected_perpage_data = expected_perpage_data;
      tasks[i].read_write_ratio = read_write_ratio;

      tasks[i].buffer = virtual_address;
      tasks[i].size = vaddress_space_size;
      tasks[i].sequential = false;
      tasks[i].total_tasks = number_threads;

      begin_page += per_task_pages;

      pthread_mutex_init(&tasks[i].lock_1, NULL);
      pthread_mutex_init(&tasks[i].lock_2, NULL);
      pthread_mutex_lock(&tasks[i].lock_1);
      pthread_mutex_lock(&tasks[i].lock_2);
      assert(pthread_create(
                 &tasks[i].thread_id,
                 NULL,
                 //AccessHybridMemoryWriteThenRead,
                 AccessHybridMemoryRandomAccess,
                 &tasks[i]) == 0);
    }
    sleep(1);

    ///////////////////////////
    // Tell all threads to warm up.
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_mutex_unlock(&tasks[i].lock_1);
    }
    sleep(2);
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_mutex_lock(&tasks[i].lock_1);
    }
    // Tell all threads to start read workload.
    uint64_t p1_faults = NumberOfPageFaults();
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_mutex_unlock(&tasks[i].lock_2);
    }
    uint64_t tstart = NowInUsec();
    uint64_t total_accesses = 0;
    for (uint32_t i = 0; i < number_threads; ++i) {
      pthread_join(tasks[i].thread_id, NULL);
      total_accesses += tasks[i].actual_number_access;
    }
    uint64_t total_usec = NowInUsec() - tstart;

    ///////////////////////////
    uint64_t p2_faults = NumberOfPageFaults();
    uint64_t number_faults = p2_faults - p1_faults;

    // Per-thread percentile latency for read and write.
    if (read_write_ratio > 0) {
      printf("\nThread_id\tRead_ops\tavg-lat(usec)\t50-lat(usec)\t90-lat(usec)"
             "\t95-lat(usec)\t99-lat(usec)\tmax(usec)\n");
      for (uint32_t i = 0; i < number_threads; ++i) {
        if (tasks[i].read_latency_usec != NULL) {
          uint64_t number_reads = tasks[i].number_reads;
          std::vector<uint32_t> values(tasks[i].read_latency_usec,
                                       tasks[i].read_latency_usec +
                                           number_reads);
          std::sort(values.begin(), values.end());
          printf("%d\t%ld\t%ld\t%d\t%d\t%d\t%d\t%ld\n", i, number_reads,
                 GetSum(values) / tasks[i].number_reads,
                 values[(int)(number_reads * 0.5)],
                 values[(int)(number_reads * 0.9)],
                 values[(int)(number_reads * 0.95)],
                 values[(int)(number_reads * 0.99)],
                 tasks[i].max_read_latency_usec);
        }
      }
    }
    if (read_write_ratio < 100) {
      printf("\nThread_id\tWrite_ops\tavg-lat(usec)\t50-lat(usec)\t90-lat(usec)"
             "\t95-lat(usec)\t99-lat(usec)\tmax-lat(usec)\n");
      for (uint32_t i = 0; i < number_threads; ++i) {
        if (tasks[i].write_latency_usec != NULL) {
          uint64_t number_writes = tasks[i].number_writes;
          std::vector<uint32_t> values(tasks[i].write_latency_usec,
                                       tasks[i].write_latency_usec +
                                           number_writes);
          std::sort(values.begin(), values.end());
          printf("%d\t%ld\t%ld\t%d\t%d\t%d\t%d\t%ld\n", i, number_writes,
                 GetSum(values) / tasks[i].number_writes,
                 values[(int)(number_writes * 0.5)],
                 values[(int)(number_writes * 0.9)],
                 values[(int)(number_writes * 0.95)],
                 values[(int)(number_writes * 0.99)],
                 tasks[i].max_write_latency_usec);
          dbg("thread %d: workload time %ld, act-num-acc %ld\n", tasks[i].id,
              tasks[i].workload_time_usec, tasks[i].actual_number_access);
        }
      }
    }
    for (uint32_t i = 0; i < number_threads; ++i) {
      delete[] tasks[i].read_latency_usec;
      delete[] tasks[i].write_latency_usec;
    }
    // Total stats
    printf("\n----------------------- Stats --------------------\n"
           "%d threads, %ld access, %ld page faults in %f sec, \n"
           " %f usec/access, throughput = %ld access / sec\n\n",
           number_threads, total_accesses, number_faults,
           total_usec / 1000000.0, (total_usec + 0.0) / total_accesses,
           (uint64_t)(total_accesses / (total_usec / 1000000.0)));
  }

  // Free hmem.
  hmem_free(virtual_address);
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
  if (argc < 2) {
    printf("Hybrid memory basic test.\n"
           "Usage 1: %s  [flash-cache dir] \n"
           "Usage 2: %s  [flash-cache dir] [hdd backing file]\n"
           "Usage 1 allocates a virtual addr space on flash, \n"
           "usage 2 maps the hdd file to virtul address and \n"
           "uses the flash as a huge cache.\n",
           argv[0], argv[0]);
    return 0;
  }
  TestMultithreadAccess(argv[1], argv[2]);
  return 0;
}
