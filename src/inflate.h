#ifndef INFLATE_H
#define INFLATE_H
#include <stdint.h>
/* Unpacks a zlib stream (RFC 1950/1951) into dst. Returns the bytes written, or -1 if the data is bad or too big.
 * Keeps nothing of its own but static tables; dst is the window. Not reentrant. */
int zinflate(uint8_t *dst, uint32_t max, const uint8_t *src, uint32_t n);
/* The same, for output too big to hold: every byte goes to sink as it is made, and window (ZWINDOW bytes, the most a
 * zlib stream can refer back) keeps the last of them. Returns the bytes made, or -1. */
#define ZWINDOW 0x8000u
int zinflate_stream(const uint8_t *src, uint32_t n, uint8_t *window, void (*sink)(uint8_t, void *), void *user);
uint32_t zcrc32(const uint8_t *p, uint32_t n);
#endif
