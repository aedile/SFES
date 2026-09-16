# Snes9x 2005 as carried in retro-go (see LICENSE). Compiled with retro-go's flags.
# gfx.c, tile.c, clip.c and render.c are the render side: they see their own copies of the
# PPU globals (render_PPU, render_IPPU, render_Memory) and run on core 0 from the log (rlog.c).
file(GLOB SNES9X_SRCS "src/*.c")
idf_component_register(SRCS ${SNES9X_SRCS} INCLUDE_DIRS "." "src" REQUIRES esp_hw_support display)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function -Wno-unused-variable -Wno-array-bounds -Wno-implicit-fallthrough
    -DRIGHTSHIFT_IS_SAR -DFAST_LSB_WORD_ACCESS -DNO_ZERO_LUT
    -O3 -fjump-tables -ftree-switch-conversion)
set_source_files_properties(src/gfx.c src/tile.c src/clip.c src/render.c PROPERTIES
    COMPILE_OPTIONS "-DSFES_RENDER_SIDE;-DPPU=render_PPU;-DIPPU=render_IPPU;-DMemory=render_Memory")
