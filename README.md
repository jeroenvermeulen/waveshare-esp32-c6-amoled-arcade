# Waveshare ESP32-C6 AMOLED Arcade

A small handheld arcade for the [Waveshare ESP32-C6-Touch-AMOLED-2.16](https://docs.waveshare.com/ESP32-C6-Touch-AMOLED-2.16): a game menu and three original games, played with the touch screen and the three buttons on top.

| Game | Genre | Goal |
|---|---|---|
| **Tetris** | Falling-block puzzle | Fill complete rows to clear them. Speed rises every 10 lines. |
| **Breakout** | Brick breaker | Clear every brick. Power-ups: **W** wide paddle, **M** multi-ball, **S** slow ball, **+** extra life. |
| **Crossy Road** | Lane crossing | Help Blip across roads and rivers for as long as you can. The screen keeps creeping forward. |

High scores and the sound setting are saved in flash.

## Controls

The three buttons on top, seen from the front: **BOOT/−** (left), **PWR** (middle), **KEY/+** (right).

| | Touch | Left / right buttons | PWR |
|---|---|---|---|
| **Menu** | Tap a game to start; tap the speaker icon to toggle sound | Move the selection | Start the selected game |
| **Tetris** | Tap: rotate · Drag: move · Drag down: soft drop · Flick down: hard drop · Flick up: rotate back | Move left / right (hold to repeat) | Rotate |
| **Breakout** | Paddle follows your finger; lift your finger to launch | Move the paddle | Pause |
| **Crossy Road** | Tap or swipe up: hop forward · Swipe left / right / down: hop that way | Hop left / right | Pause |

In every game, tap the **×** in the top-right corner to pause, then choose resume or quit. In Breakout and Crossy Road, **PWR** pauses too.

Hold **PWR** for about 4 seconds to power off; press it to power on again. Outside a game the screen dims after 1 minute idle, and on battery the board powers off after 5 minutes idle.

## Building and flashing

You need [ESP-IDF 5.5](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32c6/get-started/). The display, touch and audio drivers are fetched from the ESP Component Registry on the first build.

```sh
scripts/idf.sh build             # build
scripts/idf.sh flash monitor     # flash and watch the serial log (Ctrl+] to quit)
```

`scripts/idf.sh` loads ESP-IDF from `~/esp/esp-idf` and uses the first `/dev/cu.usbmodem*` port it finds. Override this with `IDF_PATH`, `IDF_PYTHON_ENV_PATH` or `ESPPORT`. If ESP-IDF is already active in your shell, plain `idf.py` works too.

**VS Code:** *Terminal → Run Task…* offers **Build**, **Flash**, **Flash and monitor**, **Monitor**, **Clean** and **Menuconfig**. **Build** is also the default build task (⇧⌘B).

If flashing fails because the port is busy, close any serial monitor first. If the board doesn't respond at all, enter download mode: power off (hold **PWR**, or unplug with no battery), then hold **BOOT** while powering on again.

To go back to the factory demo, flash `03_Firmware/01_Fac-v1.0.0.bin` from [Waveshare's repository](https://github.com/waveshareteam/ESP32-C6-Touch-AMOLED-2.16) following their instructions.

## Hardware notes

| Part | Connection |
|---|---|
| CO5300 480×480 AMOLED | QSPI: CLK 0, D0–D3 1–4, CS 15. Reset by power-cycling AXP2101 ALDO3. |
| CST9220 touch (CST9217 driver) | I2C SDA 8 / SCL 7, INT 5, RST 11 |
| AXP2101 power management | I2C 0x34: battery %, charging state, power off |
| ES8311 speaker codec | I2C, I2S MCLK 19, BCLK 20, WS 22, DOUT 23 |
| BOOT/− button | GPIO9, active low |
| KEY/+ button | GPIO10, active low |
| PWR button | AXP2101 PWRON, and GPIO18 via a transistor (active high) |

The ESP32-C6 has no PSRAM, so a full 480×480 frame (450 KB) does not fit in RAM. The display driver renders each frame in fifteen 32-row bands into two DMA buffers that alternate. Each game redraws its whole scene for every band. The main loop targets 30 fps.

## Project layout

```
main/
  main.c          menu, pause and game-over screens, high scores, dimming / auto power-off
  board.c/.h      pin map, I2C bus, AXP2101 power chip
  display.c/.h    QSPI panel setup and banded frame rendering
  gfx.c/.h        drawing primitives and 5x7 bitmap font (font5x7.h)
  input.c/.h      buttons, touch, tap / swipe detection
  audio.c/.h      ES8311 setup and a tiny chip-tune sound-effect synth
  store.c/.h      NVS key/value storage
  util.c/.h       random numbers
  games/          tetris.c (Tetris), breakout.c (Breakout), crossyroad.c (Crossy Road)
scripts/idf.sh    idf.py wrapper that loads ESP-IDF
```

The hardware setup follows Waveshare's ESP-IDF examples for this board.
