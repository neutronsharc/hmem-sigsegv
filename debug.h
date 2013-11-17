// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef MYDEBUG_H_
#define MYDEBUG_H_

#include <stdio.h>
#include <assert.h>

#define MYDBG 1

#if MYDBG
#define dbg(fmt, args...)                          \
  do {                                             \
    fprintf(stderr, "%s: " fmt, __func__, ##args); \
    fflush(stderr);                                \
  } while (0)
#else
#define dbg(fmt, args...)
#endif

#define error(fmt, args...)                                 \
  do {                                                      \
    fprintf(stderr, "%s: Error!!  " fmt, __func__, ##args); \
    fflush(stderr);                                         \
  } while (0)

#define err(fmt, args...) error(fmt, ##args)

#define err_exit(fmt, args...) \
  do {                         \
    error(fmt, ##args);        \
    assert(0);                 \
  } while (0)

#endif  // MYDEBUG_H_
