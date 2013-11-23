#define _FILE_OFFSET_BITS 64

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <string.h>
#include <unistd.h>

#include "asyncio_request.h"
#include "asyncio_manager.h"

uint64_t copy_read_requests = 0;
uint64_t copy_completions = 0;
uint64_t copy_write_requests = 0;

static void CopyReadCompletion(AsyncIORequest *orig_request, int result,
                               void *p, void* p2) {
  if (result != 4096) {
    err("copy-read failed.\n");
    return;
  }
  AsyncIOInfo* aio_info = (AsyncIOInfo*)p;
  AsyncIORequest *request = orig_request->asyncio_manager()->GetRequest();
  while (!request) {
    dbg("no request avail, copy-read-rqst %ld, copy-write-rqst %ld, "
        "copy-complete %ld, wait...\n",
        copy_read_requests, copy_write_requests, copy_completions);
    sleep(1);
    request = orig_request->asyncio_manager()->GetRequest();
  }
  request->Prepare(aio_info->file_handle_,
                   aio_info->buffer_,
                   aio_info->size_,
                   aio_info->file_offset_,
                   aio_info->io_type_);
  request->asyncio_manager()->Submit(request);
  ++copy_write_requests;

  delete aio_info;
}

static void TestFileAsyncIo() {
  const char *source_filename = "/tmp/hybridmemory/source";
  const char *target_filename = "/tmp/hybridmemory/target";

  int source_fd =
      open(source_filename, O_CREAT | O_TRUNC | O_RDWR | O_DIRECT, 0666);
  int target_fd =
      open(target_filename, O_CREAT | O_TRUNC | O_RDWR | O_DIRECT, 0666);
  assert(source_fd > 0);
  assert(target_fd > 0);

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint32_t rand_seed = ts.tv_nsec;

  AsyncIOManager aio_manager;
  uint64_t aio_max_nr = 256;
  //aio_manager.Init(MAX_OUTSTANDING_ASYNCIO);
  aio_manager.Init(aio_max_nr);

  uint64_t file_size = 4096UL * 128;//1024UL * 1024 * 1; //* 100;
  uint64_t iosize = 4096;

  uint64_t number_requests = 0;
  uint64_t number_completions = 0;

  // Produce the file.
  uint8_t* databuffer;
  assert(posix_memalign((void**)&databuffer, 4096, file_size) == 0);
  for (uint64_t pos = 0; pos < file_size; pos += iosize) {
    AsyncIORequest *request = aio_manager.GetRequest();
    while (!request) {
      usleep(1000);
      number_completions += aio_manager.Poll(1);
      request = aio_manager.GetRequest();
    }
    uint8_t* buffer = databuffer + pos;
    memset(buffer, rand_r(&rand_seed), 4096);
    request->Prepare(source_fd, buffer, iosize, pos, WRITE);
    assert(aio_manager.Submit(request));
    ++number_requests;
    number_completions += aio_manager.Poll(1);
  }
  dbg("have submitted %ld rqst, got %ld completions\n", number_requests,
      number_completions);
  while (number_completions < number_requests) {
    number_completions +=
        aio_manager.Wait(number_requests - number_completions, NULL);
  }
  dbg("Source file completed. Have submitted %ld rqst, got %ld completions\n", number_requests,
      number_completions);

  // Copy source file to target file.
  for (uint64_t pos = 0; pos < file_size; pos += iosize) {
    AsyncIORequest *request = aio_manager.GetRequest();
    while (!request) {
      dbg("no request avail, wait...\n");
      sleep(1);
      request = aio_manager.GetRequest();
    }
    request->Prepare(source_fd, databuffer + pos, iosize, pos, READ);

    AsyncIOInfo* aio_info = new AsyncIOInfo();
    aio_info->file_handle_ = target_fd;
    aio_info->buffer_ = databuffer + pos;
    aio_info->size_ = iosize;
    aio_info->file_offset_ = pos;
    aio_info->io_type_ = WRITE;
    request->AddCompletionCallback(CopyReadCompletion, (void*)aio_info, NULL);

    assert(aio_manager.Submit(request));
    ++copy_read_requests;
    copy_completions += aio_manager.Poll(1);
  }
  dbg("Copy from %s to %s:  %ld copy reads rqsts, %ld completions\n",
      source_filename, target_filename, copy_read_requests, copy_completions);
  while (copy_completions < (copy_read_requests + copy_write_requests)) {
    copy_completions += aio_manager.Wait(
        copy_read_requests + copy_write_requests - copy_completions, NULL);
  }
  dbg("Copy finished. %ld copy reads rqsts, %ld copy write rqst, %ld completions\n",
      copy_read_requests, copy_write_requests, copy_completions);
}

// async-io, submit a group of request at a time.
void GroupSubmitAsycIO(std::string file_name, uint64_t file_size, uint64_t rqst_per_submit) {
  AsyncIOManager aio_manager;
  uint64_t aio_max_nr = 512;
  assert(rqst_per_submit < aio_max_nr);
  aio_manager.Init(aio_max_nr);

  int fd = open(file_name.c_str(), O_RDWR | O_DIRECT, 0666);
  assert(fd > 0);

  file_size = RoundUpToPageSize(file_size);
  uint64_t file_pages = file_size / PAGE_SIZE;
  dbg("Will perform Async IO on file %s, size %ld (%ld pages)\n",
      file_name.c_str(), file_size, file_pages);

  uint64_t total_accesses = file_pages;
  uint8_t* data_buffer;
  assert(posix_memalign((void **)&data_buffer, 4096,
                        PAGE_SIZE * rqst_per_submit) == 0);
  memset(data_buffer, 0, PAGE_SIZE * rqst_per_submit);

  uint32_t iosize = PAGE_SIZE;
  uint64_t number_reads = 0;
  uint64_t number_writes = 0;
  uint64_t number_completions = 0;
  uint64_t max_batch_latency_usec = 0;
  uint64_t t1, t2;

  uint64_t time_begin = NowInUsec();
  uint32_t rand_seed = (uint32_t)time_begin;
  for (uint64_t i = 0; i < total_accesses; i += rqst_per_submit) {
    t1 = NowInUsec();
    std::vector<AsyncIORequest*> rqsts;
    for (uint64_t j = 0; j < rqst_per_submit; ++j) {
      uint64_t target_page = rand_r(&rand_seed) % file_pages;
      bool read = true;
      if (rand_r(&rand_seed) % 100 > 50) {
        read = false;
      }
      AsyncIORequest *request = aio_manager.GetRequest();
      assert(request);
      request->Prepare(fd,
                       data_buffer + j * PAGE_SIZE,
                       iosize,
                       target_page * PAGE_SIZE,
                       read ? READ : WRITE);
      if (read) ++number_reads;
      else ++number_writes;
      rqsts.push_back(request);
      if (i && (i + j) % 1000 == 0) {
        dbg("group submit Async IO: %ld...\n", i + j);
      }
    }
    assert(aio_manager.Submit(rqsts) == true);
    while (number_completions < (number_reads + number_writes)) {
      number_completions += aio_manager.Poll(1);
    }
    t2 = NowInUsec() - t1;
    if (t2 > max_batch_latency_usec) max_batch_latency_usec = t2;
  }
  uint64_t total_time = NowInUsec() - time_begin;
  close(fd);
  free(data_buffer);

  printf("\n=======================\n");
  printf("Group-submit async IO: total %ld ops (%ld reads, %ld writes) in %f sec, "
         "%f ops/sec\n"
         "%ld IOs per submit, max-lat %ld per batch\n"
         "avg-lat = %ld usec\n",
         total_accesses, number_reads, number_writes, total_time / 1000000.0,
         total_accesses / (total_time / 1000000.0), rqst_per_submit,
         max_batch_latency_usec, total_time / total_accesses);
  printf("=======================\n");
}

// async-io, a single IO is posted at a time.
// Linux aio supports only "scattered read/write" in that, the buffers can be
// discrete, but the position in file is a contiguous section.
void SimpleAsycIO(std::string file_name, uint64_t file_size, uint64_t rqst_per_batch) {
  AsyncIOManager aio_manager;
  uint64_t aio_max_nr = 512;
  assert(rqst_per_batch < aio_max_nr);
  aio_manager.Init(aio_max_nr);

  int fd = open(file_name.c_str(), O_RDWR | O_DIRECT, 0666);
  assert(fd > 0);

  file_size = RoundUpToPageSize(file_size);
  uint64_t file_pages = file_size / PAGE_SIZE;
  dbg("Will perform Async IO on file %s, size %ld (%ld pages)\n",
      file_name.c_str(), file_size, file_pages);

  uint64_t total_accesses = file_pages;
  uint8_t* data_buffer;
  assert(posix_memalign((void **)&data_buffer, 4096,
                        PAGE_SIZE * rqst_per_batch) == 0);
  memset(data_buffer, 0, PAGE_SIZE * rqst_per_batch);

  uint32_t iosize = PAGE_SIZE;
  uint64_t number_reads = 0;
  uint64_t number_writes = 0;
  uint64_t number_completions = 0;
  uint64_t max_batch_latency_usec = 0;
  uint64_t t1, t2;

  uint64_t time_begin = NowInUsec();
  uint32_t rand_seed = (uint32_t)time_begin;
  for (uint64_t i = 0; i < total_accesses; i += rqst_per_batch) {
    t1 = NowInUsec();
    for (uint64_t j = 0; j < rqst_per_batch; ++j) {
      uint64_t target_page = rand_r(&rand_seed) % file_pages;
      bool read = true;
      if (rand_r(&rand_seed) % 100 > 50) {
        read = false;
      }
      AsyncIORequest *request = aio_manager.GetRequest();
      assert(request);
      request->Prepare(fd,
                       data_buffer + j * PAGE_SIZE,
                       iosize,
                       target_page * PAGE_SIZE,
                       read ? READ : WRITE);
      if (read) ++number_reads;
      else ++number_writes;
      assert(aio_manager.Submit(request));
      number_completions += aio_manager.Poll(1);
      if (i && (i + j) % 1000 == 0) {
        dbg("Simple Async IO: %ld...\n", i + j);
      }
    }
    while (number_completions < (number_reads + number_writes)) {
      number_completions += aio_manager.Poll(1);
    }
    t2 = NowInUsec() - t1;
    if (t2 > max_batch_latency_usec) max_batch_latency_usec = t2;
  }
  uint64_t total_time = NowInUsec() - time_begin;
  close(fd);
  free(data_buffer);

  printf("\n=======================\n");
  printf("Simple Async IO: total %ld ops (%ld reads, %ld writes) in %f sec, "
         "%f ops/sec\n"
         "1 op per rqst, %ld rqsts per batch, max-lat %ld per batch\n"
         "avg-lat = %ld usec\n",
         total_accesses, number_reads, number_writes, total_time / 1000000.0,
         total_accesses / (total_time / 1000000.0), rqst_per_batch,
         max_batch_latency_usec, total_time / total_accesses);
  printf("=======================\n");
}

// Simple sync-io: random, one-by-one.
void SyncIOTest(std::string file_name, uint64_t file_size) {
  int fd = open(file_name.c_str(), O_RDWR | O_DIRECT, 0666);
  assert(fd > 0);

  file_size = RoundUpToPageSize(file_size);
  uint64_t file_pages = file_size / PAGE_SIZE;
  dbg("Will perform Sync IO on file %s, size %ld (%ld pages)\n",
      file_name.c_str(), file_size, file_pages);

  uint64_t total_accesses = file_pages;
  uint8_t* data_buffer;
  assert(posix_memalign((void**)&data_buffer, 4096, PAGE_SIZE) == 0);
  memset(data_buffer, 0, PAGE_SIZE);

  uint32_t iosize = PAGE_SIZE;
  uint64_t number_reads = 0;
  uint64_t number_writes = 0;
  uint64_t max_read_latency_usec = 0;
  uint64_t max_write_latency_usec = 0;
  uint64_t t1, t2;

  uint64_t time_begin = NowInUsec();
  uint32_t rand_seed = (uint32_t)time_begin;
  for (uint64_t i = 0; i < total_accesses; ++i) {
    uint64_t target_page = rand_r(&rand_seed) % file_pages;
    bool read = true;
    if (rand_r(&rand_seed) % 100 > 50) {
      read = false;
    }
    if (read == true) {
      t1 = NowInUsec();
      if (pread(fd, data_buffer, iosize, target_page * PAGE_SIZE) != iosize) {
        perror("read failed:");
      }
      t2 = NowInUsec() - t1;
      if (t2 > max_read_latency_usec) max_read_latency_usec = t2;
      ++number_reads;
    } else {
      t1 = NowInUsec();
      if (pwrite(fd, data_buffer, iosize, target_page * PAGE_SIZE) != iosize) {
        perror("write failed:");
      }
      t2 = NowInUsec() - t1;
      if (t2 > max_write_latency_usec) max_write_latency_usec = t2;
      ++number_writes;
    }
    if (i && i % 1000 == 0) {
      dbg("Sync IO: %ld...\n", i);
    }
  }
  uint64_t total_time = NowInUsec() - time_begin;
  close(fd);
  free(data_buffer);

  printf("\n=======================\n");
  printf("Sync IO: total %ld ops (%ld reads, %ld writes) in %f sec, %f ops/sec\n"
         "avg-lat = %ld usec, max-read-lat %ld usec, max-write-lat %ld usec\n",
         total_accesses, number_reads, number_writes, total_time / 1000000.0,
         total_accesses / (total_time / 1000000.0),
         total_time / total_accesses,
         max_read_latency_usec, max_write_latency_usec);
}

int main(int argc, char **argv) {
  std::string file_name = "/tmp/hybridmemory/hddfile";
  uint64_t file_size = 1024UL * 1024 * 30;

  SyncIOTest(file_name, file_size);

  uint64_t rqsts_per_batch = 32;
  SimpleAsycIO(file_name, file_size, rqsts_per_batch);

  GroupSubmitAsycIO(file_name, file_size, rqsts_per_batch);

  printf("PASS\n");
  return 0;
}
