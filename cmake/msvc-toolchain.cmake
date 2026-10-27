# Windows Store (UWP) build of SDL3-uwp with MSVC running under Wine, from an
# msvc-wine install (MSVC_ROOT). SDL3-uwp contains C++/CX, which only MSVC
# compiles. cl.exe /ZW also needs .NET Framework 4.x (alink.dll) in the Wine
# prefix.

set(CMAKE_SYSTEM_NAME WindowsStore)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT DEFINED ENV{MSVC_ROOT})
  set(ENV{MSVC_ROOT} "$ENV{HOME}/.local/share/msvc")
endif()
if(NOT DEFINED ENV{MSVC_VERSION})
  set(ENV{MSVC_VERSION} "14.51.36231")
endif()
if(NOT DEFINED ENV{WINSDK_VERSION})
  set(ENV{WINSDK_VERSION} "10.0.26100.0")
endif()
set(BIN "$ENV{MSVC_ROOT}/bin/x64")
set(CMAKE_C_COMPILER "${BIN}/cl")
set(CMAKE_CXX_COMPILER "${BIN}/cl")
set(CMAKE_LINKER "${BIN}/link")
set(CMAKE_AR "${BIN}/lib")
set(CMAKE_RC_COMPILER "${BIN}/rc")
set(CMAKE_MT "${BIN}/mt")

# /ZW needs the C++/CX platform metadata and the Windows SDK union metadata.
# cl.exe under Wine only resolves these as backslash paths.
string(REPLACE "/" "\\" VC_WIN "z:$ENV{MSVC_ROOT}/VC/Tools/MSVC/$ENV{MSVC_VERSION}")
string(REPLACE "/" "\\" KIT_WIN "z:$ENV{MSVC_ROOT}/Windows Kits/10")
set(CMAKE_CXX_FLAGS_INIT "\"/AI${VC_WIN}\\lib\\x86\\store\\references\" \"/AI${KIT_WIN}\\UnionMetadata\\$ENV{WINSDK_VERSION}\"")

# The store CRT imports the *_app.dll runtime that Microsoft.VCLibs.140.00
# provides. /LIBPATH takes precedence over the desktop libraries in LIB. The
# path uses forward slashes because the shell before the Wine wrapper drops
# backslashes from link flags.
set(UWP_LINK_FLAGS "\"/LIBPATH:z:$ENV{MSVC_ROOT}/VC/Tools/MSVC/$ENV{MSVC_VERSION}/lib/x64/store\" WindowsApp.lib")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${UWP_LINK_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${UWP_LINK_FLAGS}")

set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)
