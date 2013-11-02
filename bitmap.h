#ifndef BITMAP_H_
#define BITMAP_H_

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <string>

template <uint64_t N>
class BitMap {
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

  uint64_t bit_size() const { return N; }

  uint64_t byte_size() const { return ((N + 63) / 64) * sizeof(uint64_t); }

  // Return a string representation of the bit map.
  // MSB is at left-most side,  LSB at right-most.
  const std::string to_string();

 protected:
  uint64_t map[(N + 63) / 64];
};

template <uint64_t N>
void BitMap<N>::set_all() {
  memset((void*)map, 0xff, (N + 63) / 64 * sizeof(uint64_t));
}

template <uint64_t N>
void BitMap<N>::clear_all() {
  memset((void*)map, 0, (N + 63) / 64 * sizeof(uint64_t));
}

template <uint64_t N>
int BitMap<N>::get(uint64_t pos) {
  assert(pos <= N);
  uint64_t *p = &map[(pos -1 ) / 64];
  if (*p & (1ULL << ((pos - 1) % 64))) {
    return 1;
  } else {
    return 0;
  }
}

template <uint64_t N>
void BitMap<N>::set(uint64_t pos) {
  assert(pos <= N);
  uint64_t *p = &map[(pos -1 ) / 64];
  *p |= (1ULL << ((pos - 1) % 64));
}

template <uint64_t N>
void BitMap<N>::clear(uint64_t pos) {
  assert(pos <= N);
  uint64_t *p = &map[(pos -1 ) / 64];
  *p &= ~(1ULL << ((pos - 1) % 64));
}

template <uint64_t N>
const std::string BitMap<N>::to_string() {
  std::string s;
  for (uint64_t i = N; i >= 1; --i) {
    s.append(get(i) == 1 ? "1" : "0");
  }
  return s;
}

template <uint64_t N>
uint64_t BitMap<N>::ffs(bool toggle) {
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
uint64_t BitMap<N>::ffs() {
  return ffs(false);
}

#endif  // BITMAP_H_
