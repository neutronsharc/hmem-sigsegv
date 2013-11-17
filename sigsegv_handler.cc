// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "sigsegv_handler.h"

static void InitSigsegvSigaction(struct sigaction* action) {
  // TODO: why we should block most signals while SIGSEGV is processed?
  //
  // Block most signals while SIGSEGV is being handled.
  // Signals SIGKILL, SIGSTOP cannot be blocked.
  // Signals SIGCONT, SIGTSTP, SIGTTIN, SIGTTOU are not blocked because
  //   dealing with these signals seems dangerous
  // Signals SIGILL, SIGABRT, SIGFPE, SIGSEGV, SIGTRAP, SIGIOT, SIGEMT, SIGBUS,
  //   SIGSYS, SIGSTKFLT are not blocked because these are synchronous signals,
  //   which may require immediate intervention, otherwise the process may
  //   starve.
  //
  // Sigs in the sa_mask will be blocked
  sigemptyset(&action->sa_mask);
#ifdef SIGHUP
    sigaddset (&action->sa_mask, SIGHUP);
#endif
#ifdef SIGINT
    sigaddset (&action->sa_mask, SIGINT);
#endif
#ifdef SIGQUIT
    sigaddset (&action->sa_mask, SIGQUIT);
#endif
#ifdef SIGPIPE
    sigaddset (&action->sa_mask, SIGPIPE);
#endif
#ifdef SIGALRM
    sigaddset (&action->sa_mask, SIGALRM);
#endif
#ifdef SIGTERM
    sigaddset (&action->sa_mask, SIGTERM);
#endif
#ifdef SIGUSR1
    sigaddset (&action->sa_mask, SIGUSR1);
#endif
#ifdef SIGUSR2
    sigaddset (&action->sa_mask, SIGUSR2);
#endif
#ifdef SIGCHLD
    sigaddset (&action->sa_mask, SIGCHLD);
#endif
#ifdef SIGCLD
    sigaddset (&action->sa_mask, SIGCLD);
#endif
#ifdef SIGURG
    sigaddset (&action->sa_mask, SIGURG);
#endif
#ifdef SIGIO
    sigaddset (&action->sa_mask, SIGIO);
#endif
#ifdef SIGPOLL
    sigaddset (&action->sa_mask, SIGPOLL);
#endif
#ifdef SIGXCPU
    sigaddset (&action->sa_mask, SIGXCPU);
#endif
#ifdef SIGXFSZ
    sigaddset (&action->sa_mask, SIGXFSZ);
#endif
#ifdef SIGVTALRM
    sigaddset (&action->sa_mask, SIGVTALRM);
#endif
#ifdef SIGPROF
    sigaddset (&action->sa_mask, SIGPROF);
#endif
#ifdef SIGPWR
    sigaddset (&action->sa_mask, SIGPWR);
#endif
#ifdef SIGLOST
    sigaddset (&action->sa_mask, SIGLOST);
#endif
#ifdef SIGWINCH
    sigaddset (&action->sa_mask, SIGWINCH);
#endif
}

// Uninstall sighandler if it is installed.
SigSegvHandler::~SigSegvHandler() {
  UninstallHandler();
}

bool SigSegvHandler::InstallHandler(signal_handler_t handler) {
  struct sigaction sa;
  memset(&sa, 0, sizeof(sa));
  sigemptyset(&sa.sa_mask);
  InitSigsegvSigaction(&sa);

  sa.sa_flags = SA_SIGINFO;  // need a siginfo_t in callback func
  sa.sa_sigaction = handler; //SigSegvAction;
  // sa.sa_sigaction = test_sigsegv_handler;
  // sa.sa_handler = term_handler;
  if (sigaction(SIGSEGV, &sa, &old_action_) != 0) {
    err("fail to reg sig SIGSEGV...\n");
    return false;
  }
  have_installed_handler_ = true;
  return true;
}

bool SigSegvHandler::UninstallHandler() {
  if (!have_installed_handler_) {
    return true;
  }
  dbg("Uninstall sigsegv handler...\n");
  if (sigaction(SIGSEGV, &old_action_, NULL) != 0) {
    err("fail to restore SIGSEGV...\n");
    return false;
  }
  have_installed_handler_ = false;
  return true;
}
