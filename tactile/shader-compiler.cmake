# Build-time WGSL translation for cache requests recorded by this Dawn revision.
option(DAWN_BUILD_SHADER_COMPILER "Build the offline Dawn shader cache compiler" OFF)

if (DAWN_BUILD_SHADER_COMPILER AND NOT ANDROID AND NOT CMAKE_CROSSCOMPILING)
    if (NOT TINT_BUILD_WGSL_READER OR NOT TINT_BUILD_SPV_WRITER)
        message(FATAL_ERROR "dawn_shader_compiler requires the Tint WGSL reader and SPIR-V writer")
    endif()

    add_executable(dawn_shader_compiler "${CMAKE_CURRENT_LIST_DIR}/shader-compiler.cpp")
    target_link_libraries(dawn_shader_compiler PRIVATE
        dawn_internal_config
        dawn::dawn_native
        libtint
        absl::flat_hash_map
        absl::flat_hash_set
        absl::str_format_internal
        absl::strings
    )
    common_compile_options(dawn_shader_compiler)
endif()
