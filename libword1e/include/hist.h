#pragma once

#include <stdint.h>

typedef uint64_t Histogram[2];

static inline int
hist_count(const Histogram hist, char letter)
{
	uint64_t l = letter - 'A';
	int idx = l >> 4;
	uint64_t shift = (l & 0xf) * 4;
	uint64_t mask = (uint64_t)0xf << shift;
	return __builtin_popcountll(hist[idx] & mask);
}

static inline void
hist_add_letter(Histogram hist, char letter)
{
	uint64_t l = letter - 'A';
	int idx = l >> 4;
	uint64_t shift = (l & 0xf) * 4;
	uint64_t mask = (uint64_t)0xf << shift;
	hist[idx] |= (((hist[idx] & mask) << 1) & mask) | ((uint64_t)1 << shift);
}

static inline void
hist_remove_letter(Histogram hist, char letter)
{
	uint64_t l = letter - 'A';
	int idx = l >> 4;
	uint64_t shift = (l & 0xf) * 4;
	uint64_t mask = (uint64_t)0xf << shift;
	hist[idx] = (hist[idx] & ~mask) | ((hist[idx] & mask) >> 1) & mask;
}

