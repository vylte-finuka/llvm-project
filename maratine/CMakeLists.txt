# Vyft Ltd - Maratine Language Project CMakeLists

cmake_minimum_required(VERSION 3.20)
project(Maratine)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find LLVM
find_package(LLVM REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Source files
set(MARATINE_SOURCES
    lib/MaratineLexer.cpp
    lib/MaratineParser.cpp
    lib/MaratineCodeGen.cpp
    lib/MaratineOVC.cpp
)

# Compiler library
add_library(maratine_compiler STATIC ${MARATINE_SOURCES})
target_include_directories(maratine_compiler PUBLIC include)

# Get LLVM libraries
llvm_map_components_to_libnames(llvm_libs core passes support)

# Link LLVM
target_link_libraries(maratine_compiler PUBLIC ${llvm_libs})

# Compiler executable
add_executable(maratine-cc tools/maratine-cc.cpp)
target_link_libraries(maratine-cc maratine_compiler ${llvm_libs})

# Testing
enable_testing()
add_subdirectory(test)

# Installation
install(TARGETS maratine-cc DESTINATION bin)
install(FILES include/Maratine*.h DESTINATION include/maratine)

message(STATUS "Maratine project configured successfully")
