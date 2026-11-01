# AGENTS.md

Firmware for a Waveshare ESP32-C6-Touch-AMOLED-2.16: a game menu plus five games. Plain C on ESP-IDF 5.5, no LVGL. See README.md for controls and the user-facing overview.

## Build, flash, test

- `scripts/idf.sh build` builds. `scripts/idf.sh flash` flashes. The script loads ESP-IDF and picks the first `/dev/cu.usbmodem*` port; set `ESPPORT` to override.
- `idf.py monitor` needs an interactive terminal. To read the log non-interactively, open the port with pyserial at 115200 baud. Only one process can hold the port, so stop any reader before flashing, or the flash fails with "Packet content transfer stopped".
- Flashing over USB-Serial/JTAG resets the chip and the USB port re-enumerates. A reader has to reconnect after a reset.
- There are no unit tests. Verify on the device: `input.c` logs every tap (with coordinates), swipe and button press at INFO level, which is the main way to check input behaviour without seeing the screen. Anything visual (orientation, colours, smoothness) needs the user to look at the board.
- A build must stay free of warnings.

## Hardware facts that are easy to get wrong

Verified on the real board:
- **Buttons:** BOOT/− = GPIO9 and KEY/+ = GPIO10, both active low with pull-ups. PWR drives GPIO18 **high** through a transistor and also goes to the AXP2101 PWRON pin; a ~4 s hold powers off in hardware. Don't use the AXP2101 PKEY interrupts for PWR, because GPIO18 already gives an instant, debounce-free reading.
- **Display:** the CO5300 is driven with `espressif/esp_lcd_sh8601` in QSPI mode at 40 MHz. It has no reset GPIO; reset by toggling AXP2101 ALDO3 (`pmu_lcd_power_cycle`). Drawn windows must start on even coordinates and have even sizes, which the full-width 32-row bands satisfy.
- **Touch:** CST9220, driven by `waveshare/esp_lcd_touch_cst9217` with `swap_xy = 1, mirror_y = 1`. This gives screen coordinates with (0,0) at the top-left.
- **Audio:** ES8311 via `espressif/esp_codec_dev`, with no PA enable pin. The ES7210 mic ADC is not used.
- **Memory:** no PSRAM and about 420 KB of heap. The ESP32-C6 is a single-core RISC-V without an FPU, so floats are soft-float: fine for game physics, but keep them out of per-pixel loops.
- The I2C bus (GPIO7/8) is shared by touch, AXP2101 and ES8311. The `i2c_master` driver serialises access.

## Architecture

- `main.c` runs a ~30 fps loop: `input_poll` → `update` (state machine: menu / play / pause / game over) → `display_frame(draw)`.
- `display_frame` calls `draw()` once per 32-row band (15 times per frame) with `gfx_target` set to that band. Every draw function must therefore render the **whole scene** from game state, without side effects. Never update state inside `draw`. Use `gfx_visible(y, h)` to skip off-band work cheaply.
- `gfx_sprite` draws pixel-art sprites from string rows (a digit per pixel indexing a palette, `.` transparent), optionally mirrored.
- Colours are RGB565 stored byte-swapped (`RGB()`, `HEX()` in `gfx.h`). Use those macros, or `gfx_mix`, and never raw RGB565 values.
- `sfx(id)` is fire-and-forget: it queues an effect for the audio task. To add a sound, add an `sfx_t` entry in `audio.h` and a note list in `s_sfx` in `audio.c`.
- High scores live in NVS under each game's `nvs_key` (max 15 characters); `store_get` / `store_set` wrap it.

## Adding a game

1. Create `main/games/<name>.c` with a `const game_t game_<name>` (see `game.h`): `start`, `update` (returns `false` once the game is over; the app then shows the game-over screen and keeps calling `draw`), `draw`, `score` and `draw_icon` (an 80×80 menu icon).
2. Add the file to `main/CMakeLists.txt`, the `extern` to `game.h`, and the game to `s_games` in `main.c`. The menu scrolls, so any number of games fits.
3. `main.c` draws a close button (×) in the top-right corner of every game and swallows touches that start there (x ≥ 392, y < 84), so keep that corner free of HUD. BTN_PWR pauses the game unless `uses_pwr` is set, in which case the game gets it (Tetris rotates with it, Crossy Road hops forward, Space Shooter fires a bomb, Pip's Quest jumps) and only the × pauses.
4. Keep games original: genre mechanics are fine, but don't reproduce a specific commercial game's name, characters or look.

## Code style

Match the surrounding code: C17, 4-space indent, `s_` prefix for file-level statics, `UPPER_CASE` defines, short comments only where the reason isn't obvious. Hardware pin numbers belong in `board.h`.
