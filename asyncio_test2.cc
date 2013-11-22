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

static void CopyWriteCompletion(AsyncIORequest *request, int result,
                               void *p, void* p2) {
  if (result != 4096) {
    err("copy-write failed.\n");
    return;
  }
}

static void CopyReadCompletion(AsyncIORequest *request, int result,
                               void *p, void* p2) {
  if (result != 4096) {
    err("copy-read failed.\n");
    return;
  }
  AsyncIORequest *followup_request = (AsyncIORequest*)p;
  request->asyncio_manager()->Submit(followup_request);
  ++copy_write_requests;
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
    AsyncIORequest *followup_request = aio_manager.GetRequest();
    while (!request || !followup_request) {
      dbg("no request avail, wait...\n");
      exit(1);
    }
    request->Prepare(source_fd, databuffer + pos, iosize, pos, READ);
    request->AddCompletionCallback(CopyReadCompletion, (void *)followup_request,
                                   NULL);
    followup_request->Prepare(target_fd, databuffer + pos, iosize, pos, WRITE);
    followup_request->AddCompletionCallback(CopyWriteCompletion, NULL, NULL);

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

int main(int argc, char **argv) {
  TestFileAsyncIo();
  printf("PASS\n");
  return 0;
}
