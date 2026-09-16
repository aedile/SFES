idf_component_register(
    SRCS "main.c" "ui.c" "festive.c" "medal.c" "saves.c" "core.c" "music.c"
    INCLUDE_DIRS "."
    REQUIRES display audio snes9x ble_pad driver esp_timer esp_partition nvs_flash esp_adc)

# knobs: idf.py -DMEM_LAYOUT=0 -DDEMO_SECONDS=30 build
foreach(knob MEM_LAYOUT DEMO_SECONDS)
    if(DEFINED ${knob})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${knob}=${${knob}})
    endif()
endforeach()

# Pack roms/ (and roms/art/) into the 'roms' partition image; flashed with the app.
set(ROMS_DIR "${CMAKE_CURRENT_LIST_DIR}/../roms")
file(GLOB ROM_FILES "${ROMS_DIR}/*.zip" "${ROMS_DIR}/*.sfc" "${ROMS_DIR}/*.smc" "${ROMS_DIR}/art/*.png")
set(ROMS_BIN "${CMAKE_BINARY_DIR}/roms.bin")
idf_build_get_property(python PYTHON)
add_custom_command(OUTPUT "${ROMS_BIN}"
    COMMAND ${python} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_roms.py" "${ROMS_DIR}" "${ROMS_BIN}"
    DEPENDS ${ROM_FILES} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_roms.py" "${CMAKE_CURRENT_LIST_DIR}/../tools/artconv.py"
    COMMENT "Packing ROMs from ${ROMS_DIR}")
add_custom_target(roms_bin ALL DEPENDS "${ROMS_BIN}")
esptool_py_flash_to_partition(flash "roms" "${ROMS_BIN}")
add_dependencies(flash roms_bin)
