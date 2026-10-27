# Windows Store (UWP) cross build from Linux with clang-cl and lld-link.
#
# Headers and libraries come from an msvc-wine install (MSVC_ROOT) so the STL,
# the store CRT and the MSVC-built SDL3-uwp all match. C++/WinRT headers come
# from an xwin splat (XWIN_ROOT) of the same Windows SDK, because msvc-wine
# lowercases their names.

set(CMAKE_SYSTEM_NAME WindowsStore)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)
set(CMAKE_C_COMPILER clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_LINKER lld-link)
set(CMAKE_RC_COMPILER llvm-rc)
set(CMAKE_MT llvm-mt)

if(NOT DEFINED ENV{MSVC_ROOT})
  set(ENV{MSVC_ROOT} "$ENV{HOME}/.local/share/msvc")
endif()
if(NOT DEFINED ENV{XWIN_ROOT})
  set(ENV{XWIN_ROOT} "$ENV{HOME}/.cache/cargo-xwin/xwin")
endif()
if(NOT DEFINED ENV{MSVC_VERSION})
  set(ENV{MSVC_VERSION} "14.51.36231")
endif()
if(NOT DEFINED ENV{WINSDK_VERSION})
  set(ENV{WINSDK_VERSION} "10.0.26100.0")
endif()
set(VC "$ENV{MSVC_ROOT}/VC/Tools/MSVC/$ENV{MSVC_VERSION}")
set(KIT "$ENV{MSVC_ROOT}/Windows Kits/10")
set(KV "$ENV{WINSDK_VERSION}")

# Source files include a few SDK headers with a case the Linux file system
# does not find. C++/WinRT headers from xwin are lower case, so sources
# include them in lower case.
set(SHIM "${CMAKE_BINARY_DIR}/caseshim")
file(MAKE_DIRECTORY "${SHIM}")
file(CREATE_LINK "${KIT}/Include/${KV}/um/objbase.h" "${SHIM}/ObjBase.h" SYMBOLIC)
file(CREATE_LINK "${KIT}/Include/${KV}/shared/sdkddkver.h" "${SHIM}/SDKDDKVer.h" SYMBOLIC)

set(INC "\"/imsvc${SHIM}\" \"/imsvc${VC}/include\" \"/imsvc${KIT}/Include/${KV}/ucrt\" \"/imsvc${KIT}/Include/${KV}/um\" \"/imsvc${KIT}/Include/${KV}/shared\" \"/imsvc${KIT}/Include/${KV}/winrt\" \"/imsvc$ENV{XWIN_ROOT}/sdk/include/cppwinrt\"")
# Visual Studio sets this for store projects. It hides desktop-only APIs and
# makes SDL3-uwp's headers select their WinRT code.
set(INC "${INC} /DWINAPI_FAMILY=WINAPI_FAMILY_APP")
set(CMAKE_C_FLAGS_INIT "${INC} -fuse-ld=lld /clang:-msse4.1")
set(CMAKE_CXX_FLAGS_INIT "${INC} -fuse-ld=lld /clang:-msse4.1")

# The store CRT imports the *_app.dll runtime that Microsoft.VCLibs.140.00
# provides, and takes its system imports from the WindowsApp umbrella library.
set(LIBS "\"/libpath:${VC}/lib/x64/store\" \"/libpath:${VC}/lib/x64\" \"/libpath:${KIT}/Lib/${KV}/um/x64\" \"/libpath:${KIT}/Lib/${KV}/ucrt/x64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${LIBS} WindowsApp.lib")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${LIBS} WindowsApp.lib")
# clang-cl reads LIB when it drives the link itself, as in CMake's compiler checks.
set(ENV{LIB} "${VC}/lib/x64/store;${VC}/lib/x64;${KIT}/Lib/${KV}/um/x64;${KIT}/Lib/${KV}/ucrt/x64")

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
