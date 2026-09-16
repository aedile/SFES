idf_component_register(
    SRCS "display.c" "esp_lcd_st77916.c" "st77916_init_new.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_lcd esp_timer)
