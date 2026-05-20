set(jiaofu_models
    "../jiaofu/models/lgbm_cooked_weight_g.txt"
    "../jiaofu/models/lgbm_cooked_energy_kcal.txt"
    "../jiaofu/models/lgbm_cooked_protein_g.txt"
    "../jiaofu/models/lgbm_cooked_fat_g.txt"
    "../jiaofu/models/lgbm_cooked_carbohydrate_g.txt"
    "../jiaofu/models/lgbm_cooked_sodium_mg.txt"
    "../jiaofu/models/lgbm_cooked_cholesterol_mg.txt"
    "../jiaofu/models/lgbm_cooked_vitamin_c_mg.txt"
    "../jiaofu/models/lgbm_cooked_calcium_mg.txt"
    "../jiaofu/models/lgbm_cooked_iron_mg.txt"
    "../jiaofu/models/lgbm_cooked_potassium_mg.txt")

set(packed_jiaofu_model "${CMAKE_CURRENT_BINARY_DIR}/jiaofu_models.bin")

idf_component_register(
    SRCS "main.c"
    "nutrition_app.cpp"
    "jiaofu_inference.cpp"
    "lvgl_demo_ui.c"
    "HAL/esp_lcd_dsi/esp_lcd_dsi.c"
    "HAL/display/ST7701S/st7701s.c"
    "HAL/display/RM69A10/rm69a10.c"
    "HAL/display/test_lcd/test_lcd.c"    
    "HAL/lv_drv/lv_drv.c"  
    "HAL/I2C/I2c_drv.c"                 
    "HAL/PCA9536/pca9536.c"
    INCLUDE_DIRS 
    # "."
    # "HAL/esp_lcd_dsi/"
    # "HAL/display/"
    # # "HAL/display/ST7701S/"
    # # "HAL/display/RM69A10/"
    # # "HAL/display/test_lcd/"
    # "HAL/PCA9536/"
    # "HAL/lv_drv/"
    # "HAL/I2C/" 
    "."
    "HAL/"
    REQUIRES "esp_lcd" 
    PRIV_REQUIRES "driver"
    )

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT "${packed_jiaofu_model}"
        COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_jiaofu_models.py"
                "${CMAKE_CURRENT_LIST_DIR}/../jiaofu/models"
                "${packed_jiaofu_model}"
        DEPENDS ${jiaofu_models} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_jiaofu_models.py"
        VERBATIM)
    add_custom_target(pack_jiaofu_models DEPENDS "${packed_jiaofu_model}")
    add_dependencies(${COMPONENT_LIB} pack_jiaofu_models)
    target_add_binary_data(${COMPONENT_LIB} "${packed_jiaofu_model}" BINARY)
endif()
