// SSD-Assisted Hybrid Memory.
// Author: Xiangyong Ouyang (neutronsharc@gmail.com)
// Created on: 2011-11-11

#ifndef BITMAP_H_
#define BITMAP_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>

template <uint64_t N>
class Bitmap {
 public:
  // Find least-significant-bit that's set, and clear it to 0.
  // LSB is 1,  the MSB of this bitmap is N.
  // Return the bit position, or 0 if no bit is '1'.
  uint64_t ffs();

  // In addition to the above.
  // If "toggle" is true, it clear the bit to '0' before returning.
  uint64_t ffs(bool toggle);

  // Set the bit value to '1' at bit position "pos".
  // LSB pos = 1, MSB pos = N.
  void set(uint64_t pos);

  // Set entire bit array to 1.
  void set_all();

  // Clear the bit value to '0' at bit position "pos".
  // LSB pos = 1, MSB pos = N.
  void clear(uint64_t pos);

  // Clear entire bit array to 0.
  void clear_all();

  // Return bit value (1 or 0) at position "pos".
  // LSB pos = 1, MSB pos = N.
  int get(uint64_t pos);

  // Return number of bits that are set.
  uint64_t number_of_set_bits();

  // Return number of bits that are cleared.
  uint64_t number_of_clear_bits();

  // Size of the map in bits.
  uint64_t bit_size() const { return N; }

  // Size of this map in bytes.
  uint64_t byte_size() const { return ((N + 63) / 64) * sizeof(uint64_t); }

  // Return a string representation of the bit map.
  // MSB is at left-most side,  and LSB at right-most.
  const std::string to_string();

 protected:
  // Internal storage of this bitmap.
  uint64_t map[(N + 63) / 64];
};

template <uint64_t N>
void Bitmap<N>::set_all() {
  memset((void*)map, 0xff, (N + 63) / 64 * sizeof(uint64_t));
}

template <uint64_t N>
void Bitmap<N>::clear_all() {
  memset((void*)map, 0, (N + 63) / 64 * sizeof(uint64_t));
}

template <uint64_t N>
int Bitmap<N>::get(uint64_t pos) {
  assert(pos > 0 && pos <= N);
  uint64_t *p = &map[(pos -1 ) / 64];
  if (*p & (1ULL << ((pos - 1) % 64))) {
    return 1;
  } else {
    return 0;
  }
}

template <uint64_t N>
uint64_t Bitmap<N>::number_of_set_bits() {
  uint64_t number_set_bits = 0;
  for (uint64_t i = 1; i <= N; ++i) {
    if (get(i) == 1) {
      ++number_set_bits;
    }
  }
  return number_set_bits;
}

template <uint64_t N>
uint64_t Bitmap<N>::number_of_clear_bits() {
  uint64_t number_clear_bits = 0;
  for (uint64_t i = 1; i <= N; ++i) {
    if (get(i) == 0) {
      ++number_clear_bits;
    }
  }
  return number_clear_bits;
}

template <uint64_t N>
void Bitmap<N>::set(uint64_t pos) {
  assert(pos > 0 && pos <= N);
  uint64_t *p = &map[(pos -1 ) / 64];
  *p |= (1ULL << ((pos - 1) % 64));
}

template <uint64_t N>
void Bitmap<N>::clear(uint64_t pos) {
  assert(pos > 0 && pos <= N);
  uint64_t *p = &map[(pos -1 ) / 64];
  *p &= ~(1ULL << ((pos - 1) % 64));
}

template <uint64_t N>
const std::string Bitmap<N>::to_string() {
  std::string s;
  for (uint64_t i = N; i >= 1; --i) {
    s.append(get(i) == 1 ? "1" : "0");
  }
  return s;
}

template <uint64_t N>
uint64_t Bitmap<N>::ffs(bool toggle) {
  uint64_t number_values = (N + 63) / 64;
  for (uint64_t i = 0; i < number_values; ++i) {
    int pos = ffsll(map[i]);
    if (pos > 0) {
      uint64_t  retval = i * 64 + pos;
      if (retval > N) {
        return 0;
      }
      if (toggle) {
        map[i] &= ~(1ULL << (pos - 1));
      }
      return retval;
    }
  }
  return 0;
}

template <uint64_t N>
uint64_t Bitmap<N>::ffs() {
  return ffs(false);
}

#endif  // BITMAP_H_
