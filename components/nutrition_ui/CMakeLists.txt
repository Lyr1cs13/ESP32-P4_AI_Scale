idf_component_register(
    SRCS
        "src/nutrition_app.cpp"
        "src/nutricook_ui_font_16.c"
        "src/lvgl_demo_ui.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        freertos
        log
        lvgl
        nutrition_model
)

