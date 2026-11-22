if(NOT DEFINED DIR OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "zio_embed_sdk.cmake requiere -DDIR= y -DOUTPUT=")
endif()

file(GLOB_RECURSE headers RELATIVE "${DIR}" "${DIR}/*.h")
list(SORT headers)
set(arrays "")
set(table "")
set(index 0)
foreach(name IN LISTS headers)
    file(READ "${DIR}/${name}" hex HEX)
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
    string(LENGTH "${hex}" hexLength)
    math(EXPR size "${hexLength} / 2")
    string(APPEND arrays "const unsigned char kFile${index}[] = {${bytes}0};\n")
    string(APPEND table "        {\"${name}\", kFile${index}, ${size}},\n")
    math(EXPR index "${index} + 1")
endforeach()

file(WRITE "${OUTPUT}.tmp"
"#include \"scripting/SdkData.h\"

namespace zio::scripting
{

namespace
{

${arrays}
}

const SdkFile* sdkFiles(int& count)
{
    static const SdkFile files[] = {
${table}    };
    count = ${index};
    return files;
}

}
")
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}")
