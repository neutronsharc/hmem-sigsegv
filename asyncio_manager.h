// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef ASYNCIO_MANAGER_H_
#define ASYNCIO_MANAGER_H_

#include <libaio.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>

#include <vector>

#include "free_list.h"

// Default max requests defined at "/sys/block/sda/queue/nr_requests".
#define MAX_OUTSTANDING_ASYNCIO (2048)

class AsyncIORequest;

// This class implements asynchronous I/O managements for file operations.
class AsyncIOManager {
 public:
  AsyncIOManager() : is_ready_(false), ioctx_(0) {}
  virtual ~AsyncIOManager() { Release(); }

  bool Release();

  bool Init(uint64_t max_outstanding_ios);

  AsyncIORequest* GetRequest();

  void FreeRequest(AsyncIORequest* request);

  bool Submit(AsyncIORequest* request);

  bool Submit(std::vector<AsyncIORequest*>& request);

  // Performing a blocking poll to reap at least "min_completions"
  // completion  events.
  // This function returns when at least "min_completions" have been
  // reaped, or "timeout" expires, which ever happens first.
  //
  // Returns actual number of completions reaped.
  uint64_t WaitForEventsWithTimeout(uint64_t min_completions,
                                    uint64_t max_completions,
                                    struct timespec *timeout);

  // Perform a non-blocking poll to retrieve up to "number_completions"
  // completion events.
  //
  // Return actual number of completion events.
  uint64_t Poll(uint64_t number_completions);

  // Perform a blocking poll to wait for either "number_requests" completions
  // are available, or the timeout expires, whichever happens first.
  //
  // Returns actual number of completion events.
  uint64_t Wait(uint64_t number_completions, struct timespec *timeout);

  const io_context_t io_context() const { return ioctx_; }

  uint64_t number_free_requests() {
    return request_freelist_.AvailObjects();
  }

 protected:
  bool is_ready_;

  FreeList<AsyncIORequest> request_freelist_;

  // Context of the async-io.
  io_context_t ioctx_;

  // This manager can handle up to this many outstanding async-ios.
  uint64_t max_outstanding_ios_;

  // Currently there are these many outstanding IOs.
  uint64_t current_outstanding_ios_;
};

#endif  // ASYNCIO_MANAGER_H_
