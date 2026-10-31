idf_component_register(
    SRCS "main.c" "board.c" "display.c" "gfx.c" "input.c" "audio.c" "store.c" "util.c"
         "games/blocks.c" "games/bricks.c" "games/hopper.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_i2c esp_driver_spi esp_driver_i2s esp_driver_gpio esp_lcd esp_timer nvs_flash)
