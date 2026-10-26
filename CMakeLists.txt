cmake_minimum_required(VERSION 3.18)
project(smb_native LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# default is Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# flag to compile python module or C++ only
option(BUILD_PYTHON_BINDINGS "Build Python bindings using Pybind11" OFF)

# remove unused/unreachable code in the recompiled code (might add 1-2% of performance)
option(REMOVE_RESIDUAL "Toggle removal of unused/unreachable code in environment.cpp" ON)

# flags
add_compile_options(-Wall -Wextra -march=native)

# debug
set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g -fno-omit-frame-pointer")
# -fsanitize=address,undefined,bounds -fno-sanitize-recover=address,undefined,bounds

# release
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -fno-semantic-interposition -fomit-frame-pointer")

# require python3 to run dump_rom.py
find_package(Python3 REQUIRED COMPONENTS Interpreter)

# running dump_rom.py to generate game_data.hpp
set(ROM_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/smb.nes)
set(DUMP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/assets/dump_rom.py)
set(GAME_DATA_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/smb/data/game_data.hpp)

if(NOT EXISTS ${GAME_DATA_HEADER}) # if not game_data.hpp
    add_custom_command(
        OUTPUT ${GAME_DATA_HEADER}
        COMMAND Python3::Interpreter ${DUMP_SCRIPT}
        DEPENDS ${DUMP_SCRIPT} ${ROM_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Waiting for ROM extraction via 'dump_rom.py'..."
        VERBATIM
    )

    # fake target so that the header is generated before the build
    add_custom_target(make_game_data DEPENDS ${GAME_DATA_HEADER})
else()
    # empty target
    add_custom_target(make_game_data)
endif()

if(REMOVE_RESIDUAL)
    add_compile_definitions(REMOVE_RESIDUAL)
endif()

# include everything except bindings/
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/bindings/.*")

# C++ native module 'smb_core'
add_library(smb_core OBJECT ${CORE_SOURCES})

set_source_files_properties(src/core/environment.cpp PROPERTIES
    COMPILE_OPTIONS "-falign-labels=1;-falign-jumps=1;-falign-loops=1;-fno-gcse"
)

add_dependencies(smb_core make_game_data)
set_target_properties(smb_core PROPERTIES 
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

target_include_directories(smb_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# shared C++ library for C++ integration
add_library(smb_native SHARED $<TARGET_OBJECTS:smb_core>)
target_include_directories(smb_native PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(smb_native PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)

# rules to install C++ library
include(GNUInstallDirs)

install(TARGETS smb_native
    EXPORT smb_native_targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

# install include/ to system include dir excluding game_data.hpp (unreferenced in the headers)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} PATTERN "game_data.hpp" EXCLUDE)

install(EXPORT smb_native_targets
    FILE smb_nativeTargets.cmake
    NAMESPACE smb::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/smb_native
)

# python module '__smb_native__'
if(BUILD_PYTHON_BINDINGS)
    find_package(Python3 REQUIRED COMPONENTS Development.Module)

    if(NOT pybind11_DIR)
        execute_process(
            COMMAND "${Python3_EXECUTABLE}" -m pybind11 --cmakedir
            OUTPUT_VARIABLE pybind11_OUTPUT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE pybind11_RESULT
        )
        if(pybind11_RESULT EQUAL 0 AND pybind11_OUTPUT)
            set(pybind11_DIR "${pybind11_OUTPUT}" CACHE PATH "Path to pybind11 CMake" FORCE)
        endif()
    endif()

    find_package(pybind11 REQUIRED)

    pybind11_add_module(__smb_native__ 
        src/bindings/python_bindings.cpp 
        $<TARGET_OBJECTS:smb_core>
    )

    target_include_directories(__smb_native__ PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

    set_target_properties(__smb_native__ PROPERTIES 
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python/smb_native"
        INTERPROCEDURAL_OPTIMIZATION TRUE
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
    )
endif()

