idf_component_register(
    SRCS "main.c"
         "../sprinkles/gfx.c"
         "../sprinkles/pet.c"
         "../sprinkles/sprites.c"
         "../sprinkles/app.c"
         "../sprinkles/board_esp32.c"
    INCLUDE_DIRS "." ".."
    REQUIRES esp_timer esp_lcd driver nvs_flash esp_hw_support
)
