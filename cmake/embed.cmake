# Writes a C++ header holding the bytes of a file as an array.
#
#   cmake -DINPUT=<file> -DOUTPUT=<header> -DNAME=<symbol> -DTYPE=<char|unsigned char>
#         [-DTERMINATE=ON] -P embed.cmake
#
# TERMINATE appends a NUL so the array can be used as a C string.

file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" length)
set(bytes "")
set(count 0)
math(EXPR last "${length} - 2")
if(length GREATER 0)
  foreach(i RANGE 0 ${last} 2)
    string(SUBSTRING "${content}" ${i} 2 byte)
    string(APPEND bytes "0x${byte},")
    math(EXPR count "${count} + 1")
    if(count EQUAL 24)
      string(APPEND bytes "\n")
      set(count 0)
    endif()
  endforeach()
endif()
if(TERMINATE)
  string(APPEND bytes "0x00")
endif()
if(TYPE STREQUAL "char")
  # Byte values above 0x7f do not fit a signed char initializer.
  set(decl "inline constexpr char ${NAME}[] = {")
  string(REGEX REPLACE "0x([0-9a-f][0-9a-f])" "static_cast<char>(0x\\1)" bytes "${bytes}")
else()
  set(decl "inline constexpr unsigned char ${NAME}[] = {")
endif()
file(WRITE "${OUTPUT}.tmp" "// Generated from ${INPUT}. Do not edit.\n#pragma once\n\n${decl}\n${bytes}\n};\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")
