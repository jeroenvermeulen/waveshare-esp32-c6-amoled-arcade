idf_component_register(
    SRCS "main.c" "board.c" "display.c" "gfx.c" "input.c" "audio.c" "store.c" "util.c"
         "games/tetris.c" "games/breakout.c" "games/crossyroad.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_spi esp_driver_i2s esp_driver_gpio esp_lcd esp_timer nvs_flash)
