/* A small inflate, written for this project from RFC 1951. */
#include "inflate.h"

/* out is the whole output (mask all ones), or a 32 KB ring that is also the window (mask 0x7fff), whose every byte also
 * goes to sink as it is made. pos counts all the bytes made. */
typedef struct
{
  const uint8_t *in; uint32_t left, bits; int nbits, err; uint8_t *out; uint32_t pos, max, mask;
  void (*sink)(uint8_t, void *); void *user;
} St;

static inline void put(St *s, uint8_t b)
{
  s->out[s->pos & s->mask] = b;
  s->pos++;
  if (s->sink) s->sink(b, s->user);
}
typedef struct { uint16_t count[16], sym[288]; } Huff;

static int bits(St *s, int need)
{
  uint32_t v = s->bits;
  while (s->nbits < need)
  {
    if (s->left == 0) { s->err = 1; return 0; }
    v |= (uint32_t) *s->in++ << s->nbits;
    s->left--;
    s->nbits += 8;
  }
  s->bits = v >> need;
  s->nbits -= need;
  return (int) (v & ((1u << need) - 1u));
}

static int decode(St *s, const Huff *h)
{
  int code = 0, first = 0, index = 0;
  for (int len = 1; len <= 15; len++)
  {
    code |= bits(s, 1);
    if (s->err) return -1;
    int c = h->count[len];
    if (code - c < first) return h->sym[index + (code - first)];
    index += c;
    first += c;
    first <<= 1;
    code <<= 1;
  }
  s->err = 1;
  return -1;
}

static void build(Huff *h, const uint8_t *len, int n)
{
  uint16_t off[16];
  for (int i = 0; i < 16; i++) h->count[i] = 0;
  for (int i = 0; i < n; i++) h->count[len[i]]++;
  h->count[0] = 0;
  off[1] = 0;
  for (int i = 1; i < 15; i++) off[i + 1] = off[i] + h->count[i];
  for (int i = 0; i < n; i++) if (len[i]) h->sym[off[len[i]]++] = (uint16_t) i;
}

static const uint16_t lbase[29] = { 3,4,5,6,7,8,9,10,11,13,15,17,19,23,27,31,35,43,51,59,67,83,99,115,131,163,195,227,258 };
static const uint8_t  lext[29]  = { 0,0,0,0,0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3,4,4,4,4,5,5,5,5,0 };
static const uint16_t dbase[30] = { 1,2,3,4,5,7,9,13,17,25,33,49,65,97,129,193,257,385,513,769,1025,1537,2049,3073,4097,6145,8193,12289,16385,24577 };
static const uint8_t  dext[30]  = { 0,0,0,0,1,1,2,2,3,3,4,4,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,13,13 };

static int codes(St *s, const Huff *ll, const Huff *d)
{
  for (;;)
  {
    int sym = decode(s, ll);
    if (s->err) return -1;
    if (sym < 256)
    {
      if (s->pos >= s->max) return -1;
      put(s, (uint8_t) sym);
    }
    else if (sym == 256) return 0;
    else
    {
      sym -= 257;
      if (sym >= 29) return -1;
      uint32_t len = lbase[sym] + (uint32_t) bits(s, lext[sym]);
      int ds = decode(s, d);
      if (s->err || ds >= 30) return -1;
      uint32_t dist = dbase[ds] + (uint32_t) bits(s, dext[ds]);
      if (s->err || dist > s->pos || s->pos + len > s->max) return -1;
      for (uint32_t i = 0; i < len; i++) put(s, s->out[(s->pos - dist) & s->mask]);
    }
  }
}

static int run(St *s0)
{
  static Huff ll, dd;
  static uint8_t len[320];
  static const uint8_t order[19] = { 16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15 };
  St s = *s0;
  const uint8_t *src = s.in;
  if (s.left < 2 || (src[0] & 0x0f) != 8 || ((src[0] << 8) | src[1]) % 31 != 0 || (src[1] & 0x20)) return -1;
  s.in += 2; s.left -= 2;
  int last;
  do
  {
    last = bits(&s, 1);
    int type = bits(&s, 2);
    if (s.err) return -1;
    if (type == 0)
    {
      s.bits = 0; s.nbits = 0;
      if (s.left < 4) return -1;
      uint32_t l = s.in[0] | (uint32_t) s.in[1] << 8, nl = s.in[2] | (uint32_t) s.in[3] << 8;
      s.in += 4; s.left -= 4;
      if ((l ^ nl) != 0xffff || l > s.left || s.pos + l > s.max) return -1;
      for (uint32_t i = 0; i < l; i++) put(&s, *s.in++);
      s.left -= l;
    }
    else if (type == 1)
    {
      int i = 0;
      for (; i < 144; i++) len[i] = 8;
      for (; i < 256; i++) len[i] = 9;
      for (; i < 280; i++) len[i] = 7;
      for (; i < 288; i++) len[i] = 8;
      build(&ll, len, 288);
      for (i = 0; i < 30; i++) len[i] = 5;
      build(&dd, len, 30);
      if (codes(&s, &ll, &dd)) return -1;
    }
    else if (type == 2)
    {
      int nl = bits(&s, 5) + 257, nd = bits(&s, 5) + 1, nc = bits(&s, 4) + 4, i = 0;
      if (s.err || nl > 286 || nd > 30) return -1;
      for (; i < nc; i++) len[order[i]] = (uint8_t) bits(&s, 3);
      for (; i < 19; i++) len[order[i]] = 0;
      build(&ll, len, 19);
      i = 0;
      while (i < nl + nd)
      {
        int sym = decode(&s, &ll);
        if (s.err) return -1;
        if (sym < 16) len[i++] = (uint8_t) sym;
        else
        {
          int prev = 0, rep;
          if (sym == 16) { if (i == 0) return -1; prev = len[i - 1]; rep = 3 + bits(&s, 2); }
          else if (sym == 17) rep = 3 + bits(&s, 3);
          else rep = 11 + bits(&s, 7);
          if (s.err || i + rep > nl + nd) return -1;
          while (rep--) len[i++] = (uint8_t) prev;
        }
      }
      static Huff dtmp;
      build(&dtmp, len + nl, nd);
      build(&ll, len, nl);
      if (codes(&s, &ll, &dtmp)) return -1;
    }
    else return -1;
  } while (!last);
  return (int) s.pos;
}

int zinflate(uint8_t *dst, uint32_t max, const uint8_t *src, uint32_t n)
{
  St s = { src, n, 0, 0, 0, dst, 0, max, 0xffffffffu, 0, 0 };
  return run(&s);
}

int zinflate_stream(const uint8_t *src, uint32_t n, uint8_t *window, void (*sink)(uint8_t, void *), void *user)
{
  St s = { src, n, 0, 0, 0, window, 0, 0xffffffffu, ZWINDOW - 1u, sink, user };
  return run(&s);
}

uint32_t zcrc32(const uint8_t *p, uint32_t n)
{
  uint32_t c = 0xffffffffu;
  while (n--)
  {
    c ^= *p++;
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0u - (c & 1u)));
  }
  return ~c;
}
