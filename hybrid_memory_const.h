// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef HYBRID_MEMORY_CONST_H_
#define HYBRID_MEMORY_CONST_H_

#define PAGE_BITS (12)
#define PAGE_SIZE (1ULL << PAGE_BITS)
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define SSD_PAGE_OFFSET_BITS (24)
#define HMEM_ID_BITS (8)

// 2^4 = 16 consecutive pages are treated in one chunk,
// and one chunk is basic unit of round-robin to spread
// virtual-address to hmem-instances.
// Chunk is also the basic unit to load data from hdd file.
#define VADDRESS_CHUNK_BITS (4)

#define MAX_VIRTUAL_ADDRESS_RANGES (1024)

#define MAX_HMEM_INSTANCES (128)

// Page allocation table for flash-cache: each bitmap at the PAT leaf
// level represents 2^12 pages.
#define BITMAP_BITS (12)

// Flash-cache's Page stats table: a PTE node includes 2^12 pages.
#define PTE_BITS (12)

#define USE_ASYNCIO

#endif  // HYBRID_MEMORY_CONST_H_
