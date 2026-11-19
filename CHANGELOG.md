# Changelog

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
