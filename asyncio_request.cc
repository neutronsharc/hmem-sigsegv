#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
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
  completion_callback_params_.clear();
}

void AsyncIORequest::AddCompletionCallback(AsyncIOCompletion callback,
                                           void *callback_param) {
  if (callback) {
    completion_callbacks_.push_back(callback);
    completion_callback_params_.push_back(callback_param);
  }
}

void AsyncIORequest::RunCompletionCallbacks(int result) {
  while (completion_callbacks_.size() > 0) {
    AsyncIOCompletion callback = completion_callbacks_.back();
    completion_callbacks_.pop_back();
    void *params = completion_callback_params_.back();
    completion_callback_params_.pop_back();
    callback(this, result, params);
  }
}
