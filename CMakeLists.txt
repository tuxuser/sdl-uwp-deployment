cmake_minimum_required(VERSION 3.25)
project(sdl_uwp_onboarding LANGUAGES C CXX)

if(NOT CMAKE_SYSTEM_NAME STREQUAL "WindowsStore")
  message(FATAL_ERROR "Configure with -DCMAKE_TOOLCHAIN_FILE=cmake/clang-cl-toolchain.cmake")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDL3-uwp, built by tools/build_sdl3.sh.
set(SDL3_UWP_PREFIX "${CMAKE_SOURCE_DIR}/build/sdl3-prefix" CACHE PATH "SDL3-uwp install prefix")
find_package(SDL3 CONFIG REQUIRED PATHS "${SDL3_UWP_PREFIX}" NO_DEFAULT_PATH)

set(generated "${CMAKE_BINARY_DIR}/generated")
add_custom_command(
  OUTPUT "${generated}/embedded_manifest.h"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/assets/manifest.txt
          -DOUTPUT=${generated}/embedded_manifest.h -DNAME=kEmbeddedManifest -DTYPE=char
          -DTERMINATE=ON -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS "${CMAKE_SOURCE_DIR}/assets/manifest.txt" "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  COMMENT "Embedding assets/manifest.txt")
add_custom_command(
  OUTPUT "${generated}/web_page.h"
  COMMAND ${CMAKE_COMMAND} -DINPUT=${CMAKE_SOURCE_DIR}/web/index.html
          -DOUTPUT=${generated}/web_page.h -DNAME=kWebPage -DTYPE=unsigned\ char
          -P ${CMAKE_SOURCE_DIR}/cmake/embed.cmake
  DEPENDS "${CMAKE_SOURCE_DIR}/web/index.html" "${CMAKE_SOURCE_DIR}/cmake/embed.cmake"
  COMMENT "Embedding web/index.html")

add_executable(onboarding WIN32
  src/main.cpp
  src/app.cpp
  src/manifest/manifest.cpp
  src/net/http_server.cpp
  src/net/lan_address.cpp
  src/storage/catalog.cpp
  src/storage/file_io.cpp
  src/storage/file_store.cpp
  src/storage/importer.cpp
  src/storage/sha256.cpp
  src/ui/draw.cpp
  src/ui/focus.cpp
  thirdparty/qrcodegen/qrcodegen.cpp
  "${generated}/embedded_manifest.h"
  "${generated}/web_page.h")
target_include_directories(onboarding PRIVATE src "${generated}" thirdparty/qrcodegen)
target_compile_definitions(onboarding PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
target_compile_options(onboarding PRIVATE /EHsc /W3)
target_link_libraries(onboarding PRIVATE SDL3::SDL3-shared bcrypt WindowsApp)

# The DLL has to sit next to the executable in the package.
add_custom_command(TARGET onboarding POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3::SDL3-shared> $<TARGET_FILE_DIR:onboarding>)
