function(zio_enable_optimizations target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:/O2 /Oi /Ot /GL>
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:/LTCG>
        )
    else()
        target_compile_options(${target} PRIVATE
            $<$<CONFIG:Release>:-O3 -fomit-frame-pointer -flto>
        )
        target_link_options(${target} PRIVATE
            $<$<CONFIG:Release>:-flto>
        )
    endif()
endfunction()
