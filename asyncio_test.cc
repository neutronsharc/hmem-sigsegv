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
                               void *p) {
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

  int soruce_fd =
      open(source_filename, O_CREAT | O_TRUNC | O_RDWR | O_DIRECT, 0666);
  int target_fd =
      open(target_filename, O_CREAT | O_TRUNC | O_RDWR | O_DIRECT, 0666);

  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  uint32_t rand_seed = ts.tv_nsec;

  AsyncIOManager aio_manager;
  //aio_manager.Init(MAX_OUTSTANDING_ASYNCIO);
  aio_manager.Init(16);

  uint64_t file_size = 4096UL * 16;//1024UL * 1024 * 1; //* 100;
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
      request = aio_manager.GetRequest();
    }
    uint8_t* buffer = databuffer + pos;
    memset(buffer, rand_r(&rand_seed), 4096);
    request->Prepare(soruce_fd, buffer, iosize, pos, WRITE);
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
    request->Prepare(soruce_fd, databuffer + pos, iosize, pos, READ);

    AsyncIOInfo* aio_info = new AsyncIOInfo();
    aio_info->file_handle_ = target_fd;
    aio_info->buffer_ = databuffer + pos;
    aio_info->size_ = iosize;
    aio_info->file_offset_ = pos;
    aio_info->io_type_ = WRITE;
    request->AddCompletionCallback(CopyReadCompletion, (void*)aio_info);

    assert(aio_manager.Submit(request));
    ++copy_read_requests;
    copy_completions += aio_manager.Poll(1);
  }
  while (copy_completions < (copy_read_requests + copy_write_requests)) {
    copy_completions += aio_manager.Wait(
        copy_read_requests + copy_write_requests - copy_completions, NULL);
  }
}

int main(int argc, char **argv) {
  TestFileAsyncIo();
  printf("PASS\n");
  return 0;
}
