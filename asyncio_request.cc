#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include "asyncio_request.h"

void AsyncIORequest::Prepare(int file_handle, void *buffer, uint64_t size,
                             uint64_t file_offset, IOType io_type) {
  assert(is_active_ == true);
  file_handle_ = file_handle;
  buffer_ = buffer;
  size_ = size;
  file_offset_ = file_offset;
  io_type_ = io_type;
  completion_callbacks_.clear();
  completion_callbacks_param1_.clear();
  completion_callbacks_param2_.clear();
}

void AsyncIORequest::AddCompletionCallback(AsyncIOCompletion callback,
                                           void * param1, void* param2) {
  if (callback) {
    completion_callbacks_.push_back(callback);
    completion_callbacks_param1_.push_back(param1);
    completion_callbacks_param2_.push_back(param2);
  }
}

void AsyncIORequest::RunCompletionCallbacks(int result) {
  while (completion_callbacks_.size() > 0) {
    AsyncIOCompletion callback = completion_callbacks_.back();
    completion_callbacks_.pop_back();
    void *param1 = completion_callbacks_param1_.back();
    completion_callbacks_param1_.pop_back();
    void *param2 = completion_callbacks_param2_.back();
    completion_callbacks_param2_.pop_back();
    callback(this, result, param1, param2);
  }
}

void AsyncIORequest::Dump() {
  fprintf(stderr,
          "asyncio-request: buffer %p, size %ld, file %d, offset %ld, op=%d\n",
          buffer_, size_, file_handle_, file_offset_, io_type_);
  fflush(stderr);
}
