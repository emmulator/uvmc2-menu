/* The firmware's list of built-in games, found by its shape so that a firmware update needs nothing from us. No hardware here. */
#ifndef FWCAT_H
#define FWCAT_H
#include <stdint.h>
#define FW_MAX_GAMES 256
#define FW_MAX_LISTS 48
#define FW_NAME 28
#define FW_NONE 255
#define FW_FLASH 0x10000000u
#define FW_SIZE  0x01000000u

typedef struct { uint32_t ptr, size, packed; uint32_t crc; uint8_t dev[2], cat, writable; } FwGame;
typedef struct
{
  const uint8_t *flash;                 /* where flash can be read */
  uint32_t table;                       /* flash address of the game list; 0 = not found */
  int ngames, ndevs, ncats;
  int cut_off;                          /* the firmware has more games than FW_MAX_GAMES */
  FwGame game[FW_MAX_GAMES];
  char dev[FW_MAX_LISTS][FW_NAME];
  char cat[16][FW_NAME];
} FwCat;

int fw_scan(FwCat *c, const uint8_t *flash);        /* the number of games, 0 if not found */
int fw_game_name(const FwCat *c, int i, char *out, int max);
const uint8_t *fw_game_data(const FwCat *c, int i); /* its zlib stream (after the save-page byte of a 192 KB game) */
uint32_t fw_game_packed(const FwCat *c, int i);     /* the length of that */
int fw_can_serve(const FwCat *c, int i);            /* 1 if cart.c has a scheme for its size (32 KB or less, 64 KB, 192 KB, stream) */

/* The video player the firmware streams its videos through (VIDE's ExtremePlayer: "VIDEOPLAYER" in its header,
 * CART_STREAM_PLAYER bytes, checked by CRC-32). 0 if this firmware has none. */
const uint8_t *fw_stream_player(const uint8_t *flash);

/* The title screen of the firmware's own menu: the lines of its cartridge header, which the console shows at power-on
 * (ARCADE JASON, ULTIMATE, VECTREX, MULTICART 2, the version). y is the header's, +127 at the top. */
#define FW_TITLE_LINES 6
typedef struct { int n; int y[FW_TITLE_LINES]; char line[FW_TITLE_LINES][24]; } FwTitle;
int fw_title(const uint8_t *flash, FwTitle *t);     /* 1 if found; if not, t holds the credit without a version */
#endif
