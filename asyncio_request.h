// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef ASYNCIO_REQUEST_H_
#define ASYNCIO_REQUEST_H_

#include <libaio.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

class AsyncIOManager;

enum IOType {
  READ = 0,
  WRITE = 1,
};

// This struct represents all necessary information about an async-io.
struct AsyncIOInfo {
  // To which file the IO is performed.
  int file_handle_;
  // Data buffer.
  void* buffer_;
  // Size of data to read/write.
  uint64_t size_;
  // Offset into the file where IO is performed.
  uint64_t file_offset_;
  // Type of IO. Currently only read/write are supported.
  IOType io_type_;
};

class AsyncIORequest;
class AsyncIOManager;

// Completion callback for an async-io.
typedef void (*AsyncIOCompletion)(AsyncIORequest*, int, void*, void*);

// This class  represent an async IO request.
class AsyncIORequest {
 public:
  AsyncIORequest() : is_active_(false) {}

  ~AsyncIORequest() {}

  void Prepare(int file_handle, void *buffer, uint64_t size,
               uint64_t file_offset, IOType io_type);

  bool WaitForCompletion();

  bool Poll();

  void set_active(bool active) { is_active_ = active; }

  void set_asyncio_manager(AsyncIOManager *manager) {
    asyncio_manager_ = manager;
  }

  const bool is_active() const { return is_active_; }

  AsyncIOManager* asyncio_manager() const { return asyncio_manager_; }

  // This is to please the free list template. Not used.
  void* data;

  const uint64_t number_completion_callbacks() const {
    return completion_callbacks_.size();
  }

  void AddCompletionCallback(AsyncIOCompletion callback, void *param1,
                             void *param2);

  void RunCompletionCallbacks(int result);

  int file_handle() const { return file_handle_; }

  void* buffer() const { return buffer_; }

  uint64_t size() const { return size_; }

  uint64_t file_offset() const { return file_offset_; }

  IOType io_type() const { return io_type_; }

  void Dump();

 protected:
  bool is_active_;

  // Manager of the async-io context.
  AsyncIOManager* asyncio_manager_;

  // Target file to perform IO.
  int file_handle_;

  // User buffer where data is stored.
  void* buffer_;

  // Size of the data IO.
  uint64_t size_;

  // The IO starts at this file offset.
  uint64_t file_offset_;

  // Type of the IO: read/write.
  IOType io_type_;

  std::vector<AsyncIOCompletion> completion_callbacks_;
  std::vector<void*> completion_callbacks_param1_;
  std::vector<void*> completion_callbacks_param2_;
};

#endif  // ASYNCIO_REQUEST_H_
