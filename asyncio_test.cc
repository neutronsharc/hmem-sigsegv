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

#include <algorithm>
#include <iostream>
#include <iomanip>

#include "asyncio_request.h"
#include "asyncio_manager.h"

using namespace std;

// async IO test params.
int queueDepth = 128;
double writeRatio = 0;
int ioSize = 4096;
long numIO = 100L;
long ioRange = 1024L * 1024 * 1024;
char filename[1024] = {'0'};
int *readLatency;
int *writeLatency;
int *perBatchRqsts;
int ioBatchSize = 1;
uint64_t targetQPS = 100000L;

uint64_t copy_read_requests = 0;
uint64_t copy_completions = 0;
uint64_t copy_write_requests = 0;


static bool ShouldThrottleQPS(uint64_t startTimeUsec,
                              uint64_t rqstsSoFar,
                              uint64_t targetqps) {

   uint64_t actualSpentTime = NowInUsec() - startTimeUsec;
   uint64_t targetSpentTime = rqstsSoFar * 1000000L / targetqps;
   return (actualSpentTime < targetSpentTime);
}

static void FullAsyncIOCompletion(AsyncIORequest *orig_request,
                                  int result,
                                  void *p1,
                                  void* p2) {
  if (result != ioSize) {
    err("aio failed, ret %d != %d.\n", result, ioSize);
    return;
  }
  long lat = NowInUsec() - (long)orig_request->reserved2;
  long idxInLatencyArray = (long)orig_request->reserved1;
  uint64_t idxInBatchArray = (uint64_t)(p2);
  orig_request->finish();

  perBatchRqsts[idxInBatchArray]--;
  //dbg("rqst batch %ld: offset %ld finished. batch remin %d\n",
  //    idxInBatchArray,
  //    orig_request->file_offset(),
  //    perBatchRqsts[idxInBatchArray]);

  if (perBatchRqsts[idxInBatchArray] == 0) {
    //dbg("rqst batch %ld finished, will update %s lat in %ld : %ld us\n",
    //    idxInBatchArray,
    //    orig_request->io_type() == READ ? "read" : "write",
    //    idxInLatencyArray,
    //    lat);
    if (orig_request->io_type() == READ) {
      readLatency[idxInLatencyArray] = lat;
    } else {
      writeLatency[idxInLatencyArray] = lat;
    }
  }
  std::vector<uint8_t* >* buffer_list = (std::vector<uint8_t*> *)p1;
  buffer_list->push_back((uint8_t*)orig_request->buffer());
}

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

// Keep the async-io queue full to get the max throughput.
void FullAsycIO() {
  //std::string file_name, uint64_t file_size,
  //  uint64_t queue_depth, bool read) {
  AsyncIOManager aio_manager;
  uint64_t aio_max_nr = queueDepth;
  aio_manager.Init(aio_max_nr);

  int fd = open(filename, O_RDWR | O_DIRECT, 0666);
  assert(fd > 0);

  long file_size = FileSize(filename);
  uint64_t file_pages = RoundUpToPageSize(file_size - 4095) / PAGE_SIZE;
  dbg("Will perform full-async IO on file %s, size %ld (%ld pages), "
      "aio-queue depth=%ld\n",
      filename, file_size, file_pages, aio_max_nr);

  uint8_t* data_buffer;
  uint64_t data_buffer_size = PAGE_SIZE * aio_max_nr;
  assert(posix_memalign((void **)&data_buffer, PAGE_SIZE, data_buffer_size)
      == 0);
  memset(data_buffer, 0xff, data_buffer_size);
  std::vector<uint8_t*> buffer_list;
  for (uint64_t i = 0; i < data_buffer_size; i += PAGE_SIZE) {
    buffer_list.push_back(data_buffer + i);
  }

  // To record r/w latency.
  readLatency= (int*)malloc(numIO * sizeof(int));
  writeLatency= (int*)malloc(numIO * sizeof(int));
  perBatchRqsts = (int*)malloc(numIO * sizeof(int));
  memset(readLatency, 0, numIO * sizeof(int));
  memset(writeLatency, 0, numIO * sizeof(int));
  memset(perBatchRqsts, 0, numIO * sizeof(int));

  //uint32_t iosize = PAGE_SIZE;
  uint64_t number_reads = 0;
  uint64_t number_writes = 0;
  uint64_t number_completions = 0;
  //uint64_t total_accesses = file_pages;
  uint64_t issued_rqst = 0;

  dbg("async io: iosize = %d\n", ioSize);
  uint64_t begin_usec = NowInUsec();
  uint32_t rand_seed = (uint32_t)begin_usec;
  int write_thresh = writeRatio * 1000000;
  uint64_t batchIssued = 0;
  uint64_t readBatches = 0, writeBatches = 0;

  while (issued_rqst < numIO) {
    while (issued_rqst < numIO &&
           buffer_list.size() >= ioBatchSize &&
           aio_manager.number_free_requests() >= ioBatchSize) {

      if (number_completions < number_reads + number_writes) {
        int i;
        while ((i = aio_manager.Poll(1)) > 0) {
          number_completions += 1;
        }
      }

      if (ShouldThrottleQPS(begin_usec, issued_rqst, targetQPS)) {
        break;
      }
      bool read = true;
      if (rand_r(&rand_seed) % 1000000 < write_thresh) {
        read = false;
      }

      int rqstsInBatch = std::min<int>(numIO - issued_rqst, ioBatchSize);
      perBatchRqsts[batchIssued] = rqstsInBatch;

      uint64_t batchStartTime = NowInUsec();
      for (int rqst = 0; rqst < rqstsInBatch; rqst++) {
        uint64_t target_page = rand_r(&rand_seed) % file_pages;

        AsyncIORequest *request = aio_manager.GetRequest();
        uint8_t* buf = buffer_list.back();
        buffer_list.pop_back();
        request->Prepare(fd,
                         buf,
                         ioSize,
                         target_page * PAGE_SIZE,
                         read ? READ : WRITE);
        request->set_batch_size(rqstsInBatch);
        request->AddCompletionCallback(FullAsyncIOCompletion,
                                       &buffer_list,
                                       (void*)batchIssued);
        //dbg("submit rqst %ld, batch %ld\n", issued_rqst, batchIssued);
        assert(aio_manager.Submit(request) == true);
        if (read) {
          request->reserved1 = (void*)(readBatches);
          number_reads++;
        } else {
          request->reserved1 = (void*)(writeBatches);
          number_writes++;
        }
        request->reserved2 = (void*)batchStartTime;
        ++issued_rqst;
      }
      if (read) {
        readBatches++;
      } else {
        writeBatches++;
      }
      batchIssued++;
      if (issued_rqst && (batchIssued % 200000 == 0)) {
        dbg("issued %ld rqsts in %ld batches, batch size = %d\n",
            issued_rqst, batchIssued, rqstsInBatch);
      }
      if (number_completions < number_reads + number_writes) {
        int i;
        while ((i = aio_manager.Poll(1)) > 0) {
          number_completions += 1;
        }
      }
    }
    if (number_completions < number_reads + number_writes) {
      int i;
      while ((i = aio_manager.Poll(1)) > 0) {
        number_completions += 1;
      }
    }
  }
  while (number_completions < number_reads + number_writes) {
    number_completions += aio_manager.Poll(1);
  }
  uint64_t total_time = NowInUsec() - begin_usec;
  close(fd);
  assert(buffer_list.size() == aio_max_nr);
  free(data_buffer);

  double readmin, read10, read20, read50, read90, read99, read999, readmax;
  double writemin, write10, write20, write50, write90, write99, write999, writemax;
  if (number_reads > 0) {
    std::sort(readLatency, readLatency + readBatches);
    readmin = readLatency[0] / 1000.0;
    read10 = readLatency[(int)(readBatches * 0.1)] / 1000.0;
    read20 = readLatency[(int)(readBatches * 0.2)] / 1000.0;
    read50 = readLatency[(int)(readBatches * 0.5)] / 1000.0;
    read90 = readLatency[(int)(readBatches * 0.9)] / 1000.0;
    read99 = readLatency[(int)(readBatches * 0.99)] / 1000.0;
    read999 = readLatency[(int)(readBatches * 0.999)] / 1000.0;
    readmax = readLatency[readBatches  - 1] / 1000.0;
  }
  if (number_writes > 0) {
    std::sort(writeLatency, writeLatency + writeBatches);
    writemin = writeLatency[0] / 1000.0;
    write10 = writeLatency[(int)(writeBatches * 0.1)] / 1000.0;
    write20 = writeLatency[(int)(writeBatches * 0.2)] / 1000.0;
    write50 = writeLatency[(int)(writeBatches * 0.5)] / 1000.0;
    write90 = writeLatency[(int)(writeBatches * 0.9)] / 1000.0;
    write99 = writeLatency[(int)(writeBatches * 0.99)] / 1000.0;
    write999 = writeLatency[(int)(writeBatches * 0.999)] / 1000.0;
    writemax = writeLatency[writeBatches - 1] / 1000.0;
  }

  printf("\n=======================\n");
  printf("Full-Async-io: queue-depth=%ld, %ld ops (%ld reads, %ld writes) "
         "in %ld batches, %d ops/batch, completed in %f sec, "
         "%f ops/sec, bandwidth %f MB/s\n",
         aio_max_nr,
         numIO,
         number_reads,
         number_writes,
         batchIssued,
         ioBatchSize,
         total_time / 1000000.0,
         numIO / (total_time / 1000000.0),
         (numIO * ioSize + 0.0) / total_time);
  if (number_reads > 0) {
  printf("Read Latency (ms)\n");
  cout << setw(15) << "min"
       << setw(15) << "10 %"
       << setw(15) << "20 %"
       << setw(15) << "50 %"
       << setw(15) << "90 %"
       << setw(15) << "99 %"
       << setw(15) << "99.9 %"
       << setw(15) << "max" << endl;
  cout << setw(15) << readmin
       << setw(15) << read10
       << setw(15) << read20
       << setw(15) << read50
       << setw(15) << read90
       << setw(15) << read99
       << setw(15) << read999
       << setw(15) << readmax << endl;
  }
  if (number_writes > 0) {
  printf("Write Latency (ms)\n");
  cout << setw(15) << "min"
       << setw(15) << "10 %"
       << setw(15) << "20 %"
       << setw(15) << "50 %"
       << setw(15) << "90 %"
       << setw(15) << "99 %"
       << setw(15) << "99.9 %"
       << setw(15) << "max" << endl;
  cout << setw(15) << writemin
       << setw(15) << write10
       << setw(15) << write20
       << setw(15) << write50
       << setw(15) << write90
       << setw(15) << write99
       << setw(15) << write999
       << setw(15) << writemax << endl;
  }
  printf("=======================\n");
  free(perBatchRqsts);
  free(readLatency);
  free(writeLatency);
}

// async-io, submit a group of request at a time.
void GroupSubmitAsycIO(std::string file_name, uint64_t file_size,
    uint64_t rqst_per_submit) {
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
  int write_thresh = writeRatio * 10000;
  for (uint64_t i = 0; i < total_accesses; i += rqst_per_submit) {
    t1 = NowInUsec();
    std::vector<AsyncIORequest*> rqsts;
    for (uint64_t j = 0; j < rqst_per_submit; ++j) {
      uint64_t target_page = rand_r(&rand_seed) % file_pages;
      bool read = true;
      if (rand_r(&rand_seed) % 10000 < write_thresh) {
        read = false;
      }
      AsyncIORequest *request = aio_manager.GetRequest();
      assert(request);
      if (read) {
        request->reserved1 = (void*)number_reads;
        number_reads++;
      } else {
        request->reserved1 = (void*)number_writes;
        number_writes++;
      }
      request->Prepare(fd,
                       data_buffer + j * PAGE_SIZE,
                       iosize,
                       target_page * PAGE_SIZE,
                       read ? READ : WRITE);
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
void SyncIOTest(std::string file_name, uint64_t file_size, bool read) {
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
    if (i && i % 2000 == 0) {
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

void help() {
  printf("Test async IO perf with varied queue depth, mixed r/w ratio: \n");
  printf("parameters: \n");
  printf("-q <N>           : queue depth. Def = 128\n");
  printf("-f <filename>    : file to r/w\n");
  printf("-w <write ratio> : 0 is no write(read-only), 0.1 is 10% write,\n"
         "                   and so on, 1 is write only.\n");
  printf("-z <N>           : r/w size. must <= 4096. Def to 4096. \n"
         "                   R/W is always aligned to 4K boundary.\n");
  printf("-n <N>           : num of I/Os to perform. Def = 1000\n");
  printf("-b <batch size>  : How many requests in a batch. Def to 1.\n");
  printf("-p <qps>         : target QPS.  Def = 1000000.\n");
  printf("-r <range>       : will perform IO within this range. Def to 1 GB\n"
         "                   must <= file size.\n");
  printf("-h               : this message\n");
}

int main(int argc, char **argv) {
  int c;
  if (argc == 1) {
    help();
    return 0;
  }

  while ((c = getopt(argc, argv, "hq:f:w:z:n:r:b:p:")) != EOF) {
    switch(c) {
      case 'h':
        help();
        return 0;
      case 'q':
        queueDepth = atoi(optarg);
        printf("queue depth = %d\n", queueDepth);
        break;
      case 'p':
        targetQPS = atol(optarg);
        printf("target qps = %ld\n", targetQPS);
        break;
      case 'b':
        ioBatchSize = atoi(optarg);
        printf("%d requests in a batch\n", ioBatchSize);
        break;
      case 'w':
        writeRatio = atof(optarg);
        printf("write ratio = %f\n", writeRatio);
        break;
      case 'z':
        ioSize = atoi(optarg);
        printf("io size = %d\n", ioSize);
        break;
      case 'n':
        numIO = atol(optarg);
        printf("number of IO = %ld\n", numIO);
        break;
      case 'r':
        ioRange = atol(optarg);
        break;
      case 'f':
        strcpy(filename, optarg);
        printf("filename = %s\n", filename);
        break;
      case '?':
        help();
        return 0;
      default:
        help();
        return 0;
    }
  }
  if (optind < argc) {
    help();
    return 0;
  }
  if (filename[0] == 0) {
    printf("must give file name.\n");
    return 0;
  }
  if (ioRange % 4096) {
    ioRange -= (ioRange % 4096);
  }
  long filesize = FileSize(filename);
  if (filesize <= 0 || ioRange > filesize) {
    printf("io range %ld > real file size %ld\n", ioRange, filesize);
    return 0;
  }
  printf("will perform IO within range [0, %ld)\n", ioRange);

  std::string file_name = argv[1];
  uint64_t file_size = 1024UL * 1024 * 1024;

  bool read = true;

  //SyncIOTest(file_name, file_size, read);

  //uint64_t rqsts_per_batch = atoi(argv[2]);
  //SimpleAsycIO(file_name, file_size, rqsts_per_batch);

  //printf("\n\n***********  Group submit aio::\n");
  //GroupSubmitAsycIO(file_name, file_size, rqsts_per_batch);

  //printf("\n\n***********  Deep-queue aio::\n");
  //uint64_t queue_depth = atoi(argv[2]);
  FullAsycIO();
  //file_name, file_size, queue_depth, read);

  printf("PASS\n");
  return 0;
}
