idf_component_register(
    SRCS
        # Application Layer
        "main.c"

        # Service Layer
        "sensor_service.c"
        "display_service.c"
        "power_manager.c"

        # Driver Layer
        "sht45.c"
        "ble_server.c"
        "battery_monitor.c"
        "button_handler.c"
        "i2c_manager.c"
        "epd_driver.c"
        "epd_ui.c"
        "epd_font.c"
        "epd_icons.c"

        # Utility Layer
        "process_context.c"
        "spiffs_logger.c"

        # Resources (Fonts/Glyphs)
        "sensor_glyphs_24px.c"

    INCLUDE_DIRS
        "../include"

    REQUIRES bt driver nvs_flash esp_timer spiffs
)
