idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES display audio snes9x driver esp_timer esp_partition)

# bench knobs: idf.py -DMEM_LAYOUT=2 -DAPU_OFF=1 -DFRAMESKIP=1 -DSOUND=1 build
foreach(knob MEM_LAYOUT APU_OFF FRAMESKIP SOUND)
    if(DEFINED ${knob})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${knob}=${${knob}})
    endif()
endforeach()

# Pack roms/ into the 'roms' partition image; flashed with the app. A new file in roms/ is
# picked up by the next build (CONFIGURE_DEPENDS is not allowed here, so the glob reruns per configure).
set(ROMS_DIR "${CMAKE_CURRENT_LIST_DIR}/../roms")
file(GLOB ROM_FILES "${ROMS_DIR}/*.zip" "${ROMS_DIR}/*.sfc" "${ROMS_DIR}/*.smc")
set(ROMS_BIN "${CMAKE_BINARY_DIR}/roms.bin")
idf_build_get_property(python PYTHON)
add_custom_command(OUTPUT "${ROMS_BIN}"
    COMMAND ${python} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_roms.py" "${ROMS_DIR}" "${ROMS_BIN}"
    DEPENDS ${ROM_FILES} "${CMAKE_CURRENT_LIST_DIR}/../tools/pack_roms.py"
    COMMENT "Packing ROMs from ${ROMS_DIR}")
add_custom_target(roms_bin ALL DEPENDS "${ROMS_BIN}")
esptool_py_flash_to_partition(flash "roms" "${ROMS_BIN}")
add_dependencies(flash roms_bin)
