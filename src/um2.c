#include "um2.h"
#include <string.h>
static uint32_t rd(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t) p[3] << 24; }
static void wr(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

int um2_parse(const uint8_t *h, uint32_t n, uint32_t size, um2_info *o)
{
  memset(o, 0, sizeof *o);
  if (n < 12) return UM2_E_SHORT;
  if (memcmp(h, "2CMU", 4) != 0) return UM2_E_MAGIC;
  uint32_t c = rd(h + 8);
  if (c == 0 || c > UM2_MAX_BLOCKS) return UM2_E_COUNT;
  if (n < 12 + c * 8) return UM2_E_SHORT;
  uint64_t total = 0;
  for (uint32_t i = 0; i < c; i++)
  {
    uint32_t a = rd(h + 12 + i * 8), w = rd(h + 16 + i * 8);
    if (a < UM2_SRAM_BASE || a >= UM2_SRAM_LIMIT || (uint64_t) a + (uint64_t) w * 4 > UM2_SRAM_LIMIT) return UM2_E_RANGE;
    o->block[i].addr = a;
    o->block[i].words = w;
    total += (uint64_t) w * 4;
  }
  o->count = c;
  o->payload_off = 12 + c * 8;
  if (o->payload_off + total > size) return UM2_E_TRUNC;
  o->bytes = (uint32_t) total;
  return UM2_OK;
}

const char *um2_error(int e)
{
  switch (e)
  {
    case UM2_OK: return "OK";
    case UM2_E_SHORT: return "FILE TOO SHORT";
    case UM2_E_MAGIC: return "NOT A UM2";
    case UM2_E_COUNT: return "BAD BLOCK COUNT";
    case UM2_E_RANGE: return "LOADS OUTSIDE SRAM";
    case UM2_E_TRUNC: return "FILE TRUNCATED";
  }
  return "ERROR";
}

int um2_is_off(const uint8_t *h) { return (rd(h + 8) & UM2_OFF_BIT) != 0; }
void um2_set_off(uint8_t *h, int off)
{
  uint32_t c = rd(h + 8);
  wr(h + 8, off ? c | UM2_OFF_BIT : c & ~UM2_OFF_BIT);
}
