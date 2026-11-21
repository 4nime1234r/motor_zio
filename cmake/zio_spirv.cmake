set(ZIO_SPIRV_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(zio_add_spirv_shader TARGET GLSL_SOURCE OUTPUT_HEADER SYMBOL)
    if(NOT Vulkan_GLSLC_EXECUTABLE)
        message(FATAL_ERROR "zio_add_spirv_shader: glslc not found - is the Vulkan SDK installed and VULKAN_SDK set?")
    endif()

    set(generatedDir "${CMAKE_CURRENT_BINARY_DIR}/generated")
    set(spirvBinary "${generatedDir}/${OUTPUT_HEADER}.spv")
    set(headerPath "${generatedDir}/${OUTPUT_HEADER}")

    get_filename_component(sourceAbs "${GLSL_SOURCE}" ABSOLUTE)

    add_custom_command(
        OUTPUT "${headerPath}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${generatedDir}"
        COMMAND "${Vulkan_GLSLC_EXECUTABLE}" "${sourceAbs}" -o "${spirvBinary}"
        COMMAND ${CMAKE_COMMAND} -DINPUT=${spirvBinary} -DOUTPUT=${headerPath} -DSYMBOL=${SYMBOL}
                -P "${ZIO_SPIRV_CMAKE_DIR}/zio_embed_spirv.cmake"
        DEPENDS "${sourceAbs}" "${ZIO_SPIRV_CMAKE_DIR}/zio_embed_spirv.cmake"
        COMMENT "zio: compiling ${GLSL_SOURCE} to SPIR-V (${SYMBOL})"
        VERBATIM
    )
    target_sources(${TARGET} PRIVATE "${headerPath}")
    target_include_directories(${TARGET} PRIVATE "${generatedDir}")
endfunction()
