// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef SIGSEGV_HANDLER_H_
#define SIGSEGV_HANDLER_H_

// NOTE: at C,  need to define "_GNU_SOURCE",
// This opens __USE_GNU which is needed for ucontext_t.

#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/ucontext.h>
#include <unistd.h>

typedef void (*signal_handler_t)(int, siginfo_t*, void*);

class SigSegvHandler {
 public:
  SigSegvHandler() : have_installed_handler_(false) {}
  virtual ~SigSegvHandler();

  // Install the handler (the method SigSegvAction) for sigsegv.
  bool InstallHandler(signal_handler_t handler);

  // Uninstall the new handler and restore to previous handler.
  bool UninstallHandler();

 protected:
  // Indicate if we have installed the new sigsegv handler.
  bool have_installed_handler_;

  // A copy of the old handler.
  struct sigaction old_action_;
};

#endif  // SIGSEGV_HANDLER_H_
