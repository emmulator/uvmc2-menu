/* Against real firmware images: finds the list, unpacks every game and checks size and CRC. */
#include "fwcat.h"
#include "inflate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FwCat c;
static uint8_t flash[FW_SIZE], window[ZWINDOW];
static uint32_t s_crc;
/* zcrc32 one byte at a time, for streamed output */
static void crc_byte(uint8_t b, void *u) { (void) u; s_crc ^= b; for (int k = 0; k < 8; k++) s_crc = (s_crc >> 1) ^ (0xedb88320u & (0u - (s_crc & 1u))); }
static uint8_t out[0x40000];
int main(int a, char **v)
{
  int fails = 0;
  for (int f = 1; f < a; f++)
  {
    memset(flash, 0xff, sizeof flash);
    FILE *h = fopen(v[f], "rb");
    if (!h) { perror(v[f]); return 2; }
    size_t got = fread(flash, 1, sizeof flash, h); fclose(h); (void) got;
    int n = fw_scan(&c, flash);
    int ok = 0, serve = 0, bad = 0, nodev = 0, nocat = 0;
    for (int i = 0; i < n; i++)
    {
      FwGame *g = &c.game[i];
      if (g->dev[0] == FW_NONE) nodev++;
      if (g->cat == FW_NONE) nocat++;
      if (!fw_can_serve(&c, i)) continue;
      serve++;
      /* every game through the streaming inflate (what the videos use), and the rest also in one go */
      s_crc = 0xffffffffu;
      int r = zinflate_stream(fw_game_data(&c, i), fw_game_packed(&c, i), window, crc_byte, 0);
      int one = g->size > sizeof out || (zinflate(out, sizeof out, fw_game_data(&c, i), fw_game_packed(&c, i)) == (int) g->size
                                          && zcrc32(out, g->size) == g->crc);
      char nm[40]; fw_game_name(&c, i, nm, sizeof nm);
      if (r == (int) g->size && ~s_crc == g->crc && one) ok++;
      else { printf("  BAD %s r=%d size=%u\n", nm, r, g->size); bad++; }
    }
    printf("%s: list %08x, %d games, %d devs (%s...), %d cats (%s...), no dev %d, no cat %d, servable %d, verified %d, bad %d\n",
           v[f], c.table, n, c.ndevs, c.dev[0], c.ncats, c.cat[0], nodev, nocat, serve, ok, bad);
    static FwTitle ti;
    int tf = fw_title(flash, &ti);
    printf("  title:"); for (int i = 0; i < ti.n; i++) printf(" [%d] %s", ti.y[i], ti.line[i]); printf("\n");
    if (!tf || ti.n < 5 || strcmp(ti.line[0], "ARCADE JASON") || strncmp(ti.line[ti.n - 1], "V 1.", 4)) { puts("  title not found"); fails++; }
    const uint8_t *pl = fw_stream_player(flash);
    printf("  video player: %s\n", pl ? "found" : "NOT FOUND");
    if (!pl || pl[0] != 'g') fails++;
    if (n < 200 || bad || c.ncats < 5 || c.ndevs < 10) fails++;
  }
  /* inflate refuses junk rather than running off */
  static const uint8_t junk[] = { 0x78, 0xda, 0xff, 0xff, 0xff, 0xff };
  if (zinflate(out, 100, junk, sizeof junk) >= 0) { puts("junk accepted"); fails++; }
  /* a game that is not there: no list in blank flash */
  memset(flash, 0xff, sizeof flash);
  if (fw_scan(&c, flash) != 0) { puts("found a list in blank flash"); fails++; }
  if (fw_stream_player(flash)) { puts("found a video player in blank flash"); fails++; }
  static FwTitle none;
  if (fw_title(flash, &none) != 0 || none.n != 4 || strcmp(none.line[0], "ARCADE JASON")) { puts("no credit without a firmware title"); fails++; }
  /* made-up lists: exactly as many as fit is complete, one more is reported */
  for (int want = FW_MAX_GAMES - 1; want <= FW_MAX_GAMES + 1; want++)
  {
    memset(flash, 0xff, sizeof flash);
    memcpy(flash + 0x90000, "GAME", 5);
    for (int i = 0; i < want; i++)
    {
      uint32_t r[6] = { FW_FLASH + 0x80000, 100, 50, 0, FW_FLASH + 0x90000, (uint32_t) i & 0xff };
      memcpy(flash + 0x10000 + i * 24, r, sizeof r);
    }
    int found = fw_scan(&c, flash);
    int cut = want > FW_MAX_GAMES;
    if (found != (want < FW_MAX_GAMES ? want : FW_MAX_GAMES) || c.cut_off != cut)
    { printf("made-up list of %d: found %d, cut_off %d\n", want, found, c.cut_off); fails++; }
  }
  printf("%s\n", fails ? "FAILED" : "OK");
  return fails != 0;
}
