# Snes9x 2005 as carried in retro-go (GPL-2.0, see LICENSE). Compiled with retro-go's flags.
file(GLOB SNES9X_SRCS "src/*.c")
idf_component_register(SRCS ${SNES9X_SRCS} INCLUDE_DIRS "." "src")
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function -Wno-unused-variable -Wno-array-bounds -Wno-implicit-fallthrough
    -DRIGHTSHIFT_IS_SAR -DFAST_LSB_WORD_ACCESS -DNO_ZERO_LUT
    -O2 -fjump-tables -ftree-switch-conversion)
