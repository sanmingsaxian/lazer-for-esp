set(LIBMAD_SOURCES
    libmad/bit.c
    libmad/decoder.c
    libmad/fixed.c
    libmad/frame.c
    libmad/huffman.c
    libmad/layer3.c
    libmad/stream.c
    libmad/synth.c
    libmad/timer.c
    libmad/version.c
)

idf_component_register(
    SRCS "mp3dec.cpp" ${LIBMAD_SOURCES}
    INCLUDE_DIRS "include" "libmad"
    REQUIRES common
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE HAVE_CONFIG_H FPM_DEFAULT OPT_SPEED=1 OPT_SSO=1)
# libmad is C; keep GNU89-style designated initializers and implicit ints legal.
set_source_files_properties(${LIBMAD_SOURCES} PROPERTIES
    COMPILE_FLAGS "-std=gnu99 -Wno-unused-but-set-variable -Wno-sign-compare -Wno-implicit-fallthrough"
)
