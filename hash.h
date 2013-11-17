// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef HASH_H_
#define HASH_H_

#define ENDIAN_BIG 0
#define ENDIAN_LITTLE 1

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

uint32_t hash(const void* key,          // the key to hash.
              size_t length,            // byte-length of the key.
              const uint32_t initval);  // init val, can be 0.

#endif  // HASH_H_
