# The ROM to embed: a file in roms/ (.zip with an .sfc/.smc inside, or a bare .sfc). Override: idf.py -DROM="name.zip" build
set(ROM "Legend of Zelda, The_ A Link to the Past.zip" CACHE STRING "ROM file in roms/")
set(ROM_SFC "${CMAKE_BINARY_DIR}/rom.sfc")
set(PROJ "${CMAKE_CURRENT_LIST_DIR}/..")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_build_get_property(python PYTHON)
    execute_process(
        COMMAND ${python} "${PROJ}/tools/extract_rom.py" "${PROJ}/roms/${ROM}" "${ROM_SFC}"
        RESULT_VARIABLE rc)
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "could not extract roms/${ROM}")
    endif()
endif()

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES display audio snes9x driver esp_timer
    EMBED_FILES "${ROM_SFC}")

# bench knobs: idf.py -DMEM_LAYOUT=2 -DAPU_OFF=1 -DFRAMESKIP=1 build
foreach(knob MEM_LAYOUT APU_OFF FRAMESKIP SOUND)
    if(DEFINED ${knob})
        target_compile_definitions(${COMPONENT_LIB} PRIVATE ${knob}=${${knob}})
    endif()
endforeach()
