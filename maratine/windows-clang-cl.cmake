# Toolchain : clang-cl 20 + lld-link — AMD64 Windows (hors VS Developer environment)
# Usage : cmake .. -G Ninja -DCMAKE_TOOLCHAIN_FILE=../windows-clang-cl.cmake -DCMAKE_BUILD_TYPE=Release

set(CMAKE_SYSTEM_NAME    Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# ---------------------------------------------------------------
# Chemins (synchronisés avec c_cpp_properties.json)
# ---------------------------------------------------------------
set(_VS_LLVM  "D:/VisualS/VC/Tools/Llvm/x64")
set(_MSVC_DIR "D:/VisualS/VC/Tools/MSVC/14.51.36231")
set(_SDK_ROOT "C:/Program Files (x86)/Windows Kits/10")
set(_SDK_VER  "10.0.26100.0")
set(_CLG_INC  "${_VS_LLVM}/lib/clang/20/include")

# ---------------------------------------------------------------
# Compilateur et linker
# ---------------------------------------------------------------
set(CMAKE_C_COMPILER   "${_VS_LLVM}/bin/clang-cl.exe" CACHE FILEPATH "C compiler")
set(CMAKE_CXX_COMPILER "${_VS_LLVM}/bin/clang-cl.exe" CACHE FILEPATH "C++ compiler")
set(CMAKE_LINKER       "${_VS_LLVM}/bin/lld-link.exe" CACHE FILEPATH "Linker")
set(CMAKE_AR           "${_VS_LLVM}/bin/llvm-lib.exe" CACHE FILEPATH "Archiver")
set(CMAKE_RC_COMPILER  "${_VS_LLVM}/bin/llvm-rc.exe"  CACHE FILEPATH "RC compiler")
set(CMAKE_MT           "${_VS_LLVM}/bin/llvm-mt.exe"  CACHE FILEPATH "MT tool")

# ---------------------------------------------------------------
# Flags compilateur — headers MSVC + SDK + clang built-in
# ---------------------------------------------------------------
set(_INC_FLAGS
  "/I\"${_MSVC_DIR}/include\""
  "/I\"${_SDK_ROOT}/Include/${_SDK_VER}/ucrt\""
  "/I\"${_SDK_ROOT}/Include/${_SDK_VER}/um\""
  "/I\"${_SDK_ROOT}/Include/${_SDK_VER}/shared\""
  "/I\"${_CLG_INC}\""
)
string(JOIN " " _INC_FLAGS_STR ${_INC_FLAGS})

set(CMAKE_C_FLAGS_INIT   "${_INC_FLAGS_STR} /D_WIN64 /D_AMD64_ /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS")
set(CMAKE_CXX_FLAGS_INIT "${_INC_FLAGS_STR} /D_WIN64 /D_AMD64_ /DUNICODE /D_UNICODE /D_CRT_SECURE_NO_WARNINGS /EHsc")

# ---------------------------------------------------------------
# Flags linker — /LIBPATH: pour lld-link (clang-cl MSVC mode)
# Note : build-llvm-win.ps1 initialise aussi $env:LIB pour couvrir
#        les chemins non transmis par cmake lors du compiler test.
# ---------------------------------------------------------------
set(_MSVC_LIB "${_MSVC_DIR}/lib/x64")
set(_UCRT_LIB "${_SDK_ROOT}/Lib/${_SDK_VER}/ucrt/x64")
set(_UM_LIB   "${_SDK_ROOT}/Lib/${_SDK_VER}/um/x64")

set(_LIB_FLAGS "/LIBPATH:\"${_MSVC_LIB}\" /LIBPATH:\"${_UCRT_LIB}\" /LIBPATH:\"${_UM_LIB}\" /machine:x64")

set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_LIB_FLAGS}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_LIB_FLAGS}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_LIB_FLAGS}")

# ---------------------------------------------------------------
# Runtime MSVC — toujours MD (Release CRT, évite msvcrtd.lib absent)
# ---------------------------------------------------------------
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL" CACHE STRING "")
