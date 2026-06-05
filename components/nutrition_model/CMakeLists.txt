set(nutricook_models
    "../../nutricook/models/lgbm_cooked_weight_g.txt"
    "../../nutricook/models/lgbm_cooked_energy_kcal.txt"
    "../../nutricook/models/lgbm_cooked_protein_g.txt"
    "../../nutricook/models/lgbm_cooked_fat_g.txt"
    "../../nutricook/models/lgbm_cooked_carbohydrate_g.txt"
    "../../nutricook/models/lgbm_cooked_sodium_mg.txt"
    "../../nutricook/models/lgbm_cooked_cholesterol_mg.txt"
    "../../nutricook/models/lgbm_cooked_vitamin_c_mg.txt"
    "../../nutricook/models/lgbm_cooked_calcium_mg.txt"
    "../../nutricook/models/lgbm_cooked_iron_mg.txt"
    "../../nutricook/models/lgbm_cooked_potassium_mg.txt")

set(packed_nutricook_model "${CMAKE_CURRENT_BINARY_DIR}/nutricook_models.bin")

idf_component_register(
    SRCS
        "src/nutricook_inference.cpp"
    INCLUDE_DIRS
        "include"
    PRIV_INCLUDE_DIRS
        "src"
    REQUIRES
        esp_timer
        log
)

if(NOT CMAKE_SCRIPT_MODE_FILE)
    add_custom_command(
        OUTPUT "${packed_nutricook_model}"
        COMMAND ${PYTHON} "${CMAKE_CURRENT_LIST_DIR}/../../tools/pack_nutricook_models.py"
                "${CMAKE_CURRENT_LIST_DIR}/../../nutricook/models"
                "${packed_nutricook_model}"
        DEPENDS ${nutricook_models} "${CMAKE_CURRENT_LIST_DIR}/../../tools/pack_nutricook_models.py"
        VERBATIM)
    add_custom_target(pack_nutricook_models DEPENDS "${packed_nutricook_model}")
    add_dependencies(${COMPONENT_LIB} pack_nutricook_models)
    target_add_binary_data(${COMPONENT_LIB} "${packed_nutricook_model}" BINARY)
endif()

