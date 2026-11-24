# Changelog

## 1.2.0

- **NEW**, a top-level entry in `menu.default.ini`: the newest 10 game files on the card by the dates the card holds for them,
  wherever they are and whether or not an `.ini` file lists them, newest first. 4 starts a file, 3 marks a favorite, and 2
  switches to only the newest *unmapped* files, as in BROWSE. `new = n` in `menu/settings.ini` changes the count (1 to 20).
  The cartridge has no clock, so NEW shows the newest files; it cannot say how long ago they were added. Hide it with
  `[NEW]` and `hide = 1` in `menu.ini`.
- The README now says the menu needs firmware 1.1.12a or later (the first version that looks for `autostart.um2`) and has only
  been tested on 1.1.13a, and credits Jason Kopp ("Arcade Jason") for the hardware and Ralf Kornmann for the firmware.
- `examples/settings.ini` shows the `new =` line; `THIRD_PARTY_NOTICES.md` credits the hardware correctly.

## 1.0.1

- README reorganised and corrected: every built-in game is listed (not only some sizes), the `folder = @fw` rule's `also`
  applies to every built-in, how to come back from `.um2` and `.bin` games, a clearer "Putting your games in the menu",
  developer details moved to "How it works", photos of the menu and a link to a demonstration video.
- `menu.default.ini` and `examples/menu.ini`: comments say "folder rule" and "does not categorize" throughout.
- The release archive holds only what a user needs: the card files, documents, examples, the README's photos and the two
  tools that run with plain Python 3 (`gen_card_ini.py`, `hs_dump.py`) with the catalogue they read.

## 1.0.0 (first release, tested on firmware 1.1.13a)

- A card-driven menu in place of the firmware's, started through the firmware's `autostart.um2` hook. Layered `.ini` files in
  plain-text format: `menu.default.ini` (shipped), other `menu/*.ini` alphabetically, `menu.ini` last.
- Lists the firmware's built-in games from the cartridge's flash, in the firmware's categories; card games only where a folder
  rule, entry or file section says so. No default rules for card files.
- BROWSE SD CARD: every folder and `.um2`, `.bin`, `.vec` file, the whole path in the heading, a note of what the folder leaves
  out, button 3 favorites, button 2 toggles an "unmapped files only" view.
- Favorites (`menu/favorites.ini`), notes and control lines per game, hiding, categories and sort order.
- Serves `.bin` / `.vec` games: up to 32 KB, 64 KB (PB6), 256 KB (flash) and streamed files over 256 KB (streaming is not yet
  run on a console).
- High scores shared with the firmware's own table (built-ins, files up to 32 KB, 64 KB PB6 files); 256 KB games save back into their
  own `.bin`. DIAGNOSTICS dumps the score table; `tools/hs_dump.py` decodes it.
- SYSTEM: CALIBRATE SCREEN, DIAGNOSTICS, TRANSFER MODE (the card as a USB drive, `menu/transfer.um2`), TURN THIS MENU OFF.
- Title screen with credits for the firmware and for this menu.
- `examples/menu.ini` and `examples/e-srg.ini`; host tests (`make test`); GPL-3.0 licence and third-party notices.
- Not done: a built-in flash game's save is not kept.
