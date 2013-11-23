#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <unistd.h>

#include <memory>
#include <string>

#include "asyncio_manager.h"
#include "asyncio_request.h"


bool AsyncIOManager::Init(uint64_t max_outstanding_ios) {
  assert(is_ready_ == false);
  assert(max_outstanding_ios <= MAX_OUTSTANDING_ASYNCIO);
  int ret = io_setup(max_outstanding_ios, &ioctx_);
  if (ret != 0) {
    err("io_setup failed return: %d\n", ret);
    assert(0);
  }

  bool page_align = false;
  bool pin_memory = true;
  uint64_t payload_datasize = 0;
  assert(request_freelist_.Init("asyncio-freelist", max_outstanding_ios,
                                payload_datasize, page_align,
                                pin_memory) == true);

  max_outstanding_ios_ = max_outstanding_ios;
  current_outstanding_ios_ = 0;
  is_ready_ = true;
  return is_ready_;
}

bool AsyncIOManager::Release() {
  if (is_ready_) {
    dbg("Release io-context for asyncio.\n");
    if (current_outstanding_ios_ > 0) {
      err("Still has outstanding async-ios\n");
    }
    io_destroy(ioctx_);
    ioctx_ = 0;
    request_freelist_.Release();
    is_ready_ = false;
  }
  return true;
}

AsyncIORequest* AsyncIOManager::GetRequest() {
  AsyncIORequest* request = request_freelist_.New();
  if (request) {
    request->set_active(true);
    request->set_asyncio_manager(this);
  }
  return request;
}

void AsyncIOManager::FreeRequest(AsyncIORequest* request) {
  assert(request->is_active() == true);
  assert(request->number_completion_callbacks() == 0);
  request->set_active(false);
  request_freelist_.Free(request);
}

bool AsyncIOManager::Submit(AsyncIORequest* request) {
  struct iocb iocb;
  struct iocb *iocbs = &iocb;
  switch (request->io_type()) {
  case READ:
    io_prep_pread(&iocb, request->file_handle(), request->buffer(),
                  request->size(), request->file_offset());
    break;
  case WRITE:
    io_prep_pwrite(&iocb, request->file_handle(), request->buffer(),
                   request->size(), request->file_offset());
    break;
  default:
    err("Unknown io type %d\n", request->io_type());
    return false;
  }
  iocb.data = (void *)request;

  if (io_submit(ioctx_, 1, &iocbs) == 1) {
    ++current_outstanding_ios_;
    return true;
  } else {
    err("submit buffer %p size %ld to file %d offset %ld type %d failed\n",
        request->buffer(), request->size(), request->file_handle(),
        request->file_offset(), request->io_type());
    return false;
  }
}

bool AsyncIOManager::Submit(std::vector<AsyncIORequest*>& requests) {
  uint64_t number_requests = requests.size();
  struct iocb iocb[number_requests];
  struct iocb *iocbs[number_requests];
  for (uint64_t i = 0; i < requests.size(); ++i) {
    switch (requests[i]->io_type()) {
    case READ:
      io_prep_pread(&iocb[i], requests[i]->file_handle(), requests[i]->buffer(),
                    requests[i]->size(), requests[i]->file_offset());
      break;
    case WRITE:
      io_prep_pwrite(&iocb[i], requests[i]->file_handle(),
                     requests[i]->buffer(), requests[i]->size(),
                     requests[i]->file_offset());
      break;
    default:
      err("Unknown io type %d\n", requests[i]->io_type());
      return false;
    }
    iocb[i].data = (void *)requests.at(i);
    iocbs[i] = &iocb[i];
  }
  if (io_submit(ioctx_, number_requests, iocbs) == number_requests) {
    current_outstanding_ios_ += number_requests;
    return true;
  } else {
    err("Try to group submit %ld requests, but failed.", number_requests);
    return false;
  }
}

uint64_t AsyncIOManager::WaitForEventsWithTimeout(
    uint64_t min_completions,
    uint64_t max_completions,
    struct timespec *timeout) {
  struct io_event events[max_completions];
  uint64_t completed_requests =
      io_getevents(ioctx_, min_completions, max_completions, events, timeout);
  assert(completed_requests <= max_completions);
  assert(completed_requests <= current_outstanding_ios_);
  current_outstanding_ios_ -= completed_requests;
  for (uint64_t i = 0; i < completed_requests; ++i) {
    AsyncIORequest *request = (AsyncIORequest *)events[i].data;
    if (events[i].res != request->size()) {
      err("aio error at: buffer %p size %ld to file %d offset %ld type %d, errno=%d\n",
          request->buffer(), request->size(), request->file_handle(),
          request->file_offset(), request->io_type(), (int)(events[i].res));
    }
    request->RunCompletionCallbacks(events[i].res);
    FreeRequest(request);
  }
  return completed_requests;
}

uint64_t AsyncIOManager::Poll(uint64_t number_requests) {
  return WaitForEventsWithTimeout(0, number_requests, NULL);
}

uint64_t AsyncIOManager::Wait(uint64_t number_requests,
                              struct timespec *timeout) {
  return WaitForEventsWithTimeout(number_requests, number_requests, timeout);
}
