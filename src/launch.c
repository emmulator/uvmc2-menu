/* launch.c: replaces the running image (this menu) with another .um2.
 *
 * The new image overwrites us in SRAM, so the last step runs from scratch memory, as a copied stub:
 *   0x20080000  the path (the firmware leaves it here for the game)
 *   0x20080100  the stub's arguments
 *   0x20080400  the stub
 * Core 1's stack starts at 0x20081000 going down and core 1 is stopped first; core 0's stack is in SCRATCH_Y,
 * above all of this. um2_parse has already refused anything that loads at or above 0x20080000. */
#include "launch.h"
#include <stdint.h>
#include <string.h>
#include "pico/bootrom.h"
#include "boot/picoboot_constants.h"
#include "pico/multicore.h"
#include "hardware/resets.h"
#include "hardware/sync.h"

typedef struct
{
  uint32_t reboot_fn, count, src, start, bytes;
  um2_block block[UM2_MAX_BLOCKS];
} launch_args;

#define SCR_PATH ((char *) 0x20080000u)
#define SCR_ARGS ((launch_args *) 0x20080100u)
#define SCR_STUB ((void *) 0x20080400u)

extern const char launch_stub_begin[], launch_stub_end[];

/* r0 = launch_args. Copies the blocks, sets scratch 0 as the firmware does, calls rom_reboot(RAM_IMAGE|NO_RETURN). */
__asm__(
  ".syntax unified\n .thumb\n .section .text.launch_stub,\"ax\"\n .balign 4\n"
  ".global launch_stub_begin\n .global launch_stub_end\n .thumb_func\n"
  "launch_stub_begin:\n"
  "  mov r4, r0\n"
  "  ldr r1, [r4, #4]\n"
  "  ldr r2, [r4, #8]\n"
  "  add r5, r4, #20\n"
  "1: cbz r1, 4f\n"
  "  ldmia r5!, {r6, r7}\n"
  "2: cbz r7, 3f\n"
  "  ldr r3, [r2], #4\n"
  "  str r3, [r6], #4\n"
  "  subs r7, r7, #1\n"
  "  b 2b\n"
  "3: subs r1, r1, #1\n"
  "  b 1b\n"
  "4: movw r1, #0x8000\n"
  "  movt r1, #0x400d\n"
  "  movw r0, #0x4332\n"
  "  movt r0, #0x554d\n"
  "  str r0, [r1, #12]\n"
  "  movw r0, #0x103\n"
  "  movs r1, #10\n"
  "  ldr r2, [r4, #12]\n"
  "  ldr r3, [r4, #16]\n"
  "  ldr r6, [r4, #0]\n"
  "  blx r6\n"
  "5: b 5b\n"
  " .balign 4\n"
  "launch_stub_end:\n"
  " .text\n");

void launch_go(const char *path, const um2_info *u)
{
  launch_args a;
  memset(&a, 0, sizeof a);
  a.reboot_fn = (uint32_t) rom_func_lookup(ROM_FUNC_REBOOT);
  a.count = u->count;
  a.src = LAUNCH_PSRAM;
  a.start = u->block[0].addr;
  a.bytes = u->bytes;
  memcpy(a.block, u->block, sizeof a.block);

  /* Nobody may touch memory or the bus from here on. */
  multicore_reset_core1();
  (void) save_and_disable_interrupts();
  reset_block_mask(RESETS_RESET_DMA_BITS | RESETS_RESET_PIO0_BITS | RESETS_RESET_PIO1_BITS | RESETS_RESET_PIO2_BITS);

  size_t n = strlen(path);
  if (n > 255) n = 255;
  memcpy(SCR_PATH, path, n);
  SCR_PATH[n] = 0;
  *SCR_ARGS = a;
  /* launch_stub_begin is a .thumb_func, so its address as data has bit 0 set: clear it, or the copy starts a byte late. */
  const char *stub = (const char *) ((uintptr_t) launch_stub_begin & ~(uintptr_t) 1);
  memcpy(SCR_STUB, stub, (size_t) (launch_stub_end - stub));
  __asm__ volatile ("dsb\n isb");
  ((void (*)(launch_args *)) ((uint32_t) SCR_STUB | 1u))(SCR_ARGS);
  for (;;) { }
}

void launch_cartridge_reboot(void)
{
  rom_reboot(BOOT_TYPE_NORMAL | REBOOT2_FLAG_NO_RETURN_ON_SUCCESS, 10, 0, 0);
  for (;;) { }
}
