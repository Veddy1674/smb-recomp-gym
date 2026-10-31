cmake_minimum_required(VERSION 3.18)
project(smb_native LANGUAGES CXX)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# INTERPROCEDURAL_OPTIMIZATION will be enabled if supported
include(CheckIPOSupported)
check_ipo_supported(RESULT SMB_IPO_OK OUTPUT SMB_IPO_MSG)

# -march=native only if the compiler accepts it (e.g. old apple clang on arm doesn't)
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag(-march=native SMB_HAS_MARCH_NATIVE)

# default is Release
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build." FORCE)
endif()

# what to build: python module, C++ shared library, or both
option(BUILD_PYTHON_BINDINGS "Build Python bindings using nanobind" OFF)
option(BUILD_CPP_LIBRARY "Build and install the shared C++ library" ON)

if(NOT BUILD_PYTHON_BINDINGS AND NOT BUILD_CPP_LIBRARY)
    message(FATAL_ERROR "nothing to build, enable BUILD_PYTHON_BINDINGS and/or BUILD_CPP_LIBRARY")
endif()

# remove unused/unreachable code in the recompiled code (might add 1-2% of performance)
option(REMOVE_RESIDUAL "Toggle removal of unused/unreachable code in environment.cpp" ON)

# flags (NOTE: -march=native is on by default because the build is never redistributed anyway)
if(MSVC)
    message(WARNING "MSVC is untested, on Windows use WSL2 or MinGW/MSYS2")
else()
    add_compile_options(-Wall -Wextra)
    if(SMB_HAS_MARCH_NATIVE)
        add_compile_options(-march=native)
    endif()

    # debug
    set(CMAKE_CXX_FLAGS_DEBUG "-O1 -g -fno-omit-frame-pointer")
    # -fsanitize=address,undefined,bounds -fno-sanitize-recover=address,undefined,bounds

    # release
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -fno-semantic-interposition")
endif()

# require python to run dump_rom.py
if(BUILD_PYTHON_BINDINGS)
    # used to dump rom and generate python lib
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter Development.Module)
else()
    # used only to dump rom
    find_package(Python 3.10 REQUIRED COMPONENTS Interpreter)
endif()

# rom must be provided by the user (never distributed)
set(ROM_FILE ${CMAKE_CURRENT_SOURCE_DIR}/assets/smb.nes)

set(DUMP_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/assets/dump_rom.py)
set(ROM_MANIFEST ${CMAKE_CURRENT_SOURCE_DIR}/include/smb/data/rom_manifest.json)
set(GAME_DATA_HEADER ${CMAKE_CURRENT_SOURCE_DIR}/include/smb/data/game_data.hpp)

if(NOT EXISTS ${ROM_FILE})
    message(FATAL_ERROR "missing ROM: put your own smb.nes in ${CMAKE_CURRENT_SOURCE_DIR}/assets/")
endif()

# run dump_rom.py to generate game_data.hpp (only reruns if script, rom or manifest changed)
add_custom_command(
    OUTPUT ${GAME_DATA_HEADER}
    COMMAND Python::Interpreter ${DUMP_SCRIPT}
    DEPENDS ${DUMP_SCRIPT} ${ROM_FILE} ${ROM_MANIFEST}
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Waiting for ROM extraction via 'dump_rom.py'..."
    VERBATIM
)

# fake target so that the header is generated before the build
add_custom_target(make_game_data DEPENDS ${GAME_DATA_HEADER})

if(REMOVE_RESIDUAL)
    add_compile_definitions(REMOVE_RESIDUAL)
endif()

# include everything except bindings/
file(GLOB_RECURSE CORE_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(FILTER CORE_SOURCES EXCLUDE REGEX "src/bindings/.*")

# C++ native module 'smb_core'
add_library(smb_core OBJECT ${CORE_SOURCES})

# add special flags to environment.cpp (gcc only, clang ignores/warns on these)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(src/core/environment.cpp PROPERTIES
        COMPILE_OPTIONS "-falign-labels=1;-falign-jumps=1;-falign-loops=1;-fno-gcse"
    )
endif()

add_dependencies(smb_core make_game_data)
set_target_properties(smb_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    INTERPROCEDURAL_OPTIMIZATION ${SMB_IPO_OK}
)

target_include_directories(smb_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

find_package(OpenMP COMPONENTS CXX)
if(OpenMP_CXX_FOUND)
    target_link_libraries(smb_core PUBLIC OpenMP::OpenMP_CXX) # compile
endif()

# shared C++ library for C++ integration
if(BUILD_CPP_LIBRARY)
    add_library(smb_native SHARED $<TARGET_OBJECTS:smb_core>)
    target_include_directories(smb_native PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    )
    set_target_properties(smb_native PROPERTIES INTERPROCEDURAL_OPTIMIZATION ${SMB_IPO_OK})

    if(OpenMP_CXX_FOUND)
        target_link_libraries(smb_native PRIVATE OpenMP::OpenMP_CXX) # link
    endif()

    # rules to install C++ library
    include(GNUInstallDirs)

    install(TARGETS smb_native
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    )

    # install include/ to system include dir
    install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        PATTERN "game_data.hpp" EXCLUDE
        PATTERN "rom_manifest.json" EXCLUDE
    )
endif()

# python module '__smb_native__'
if(BUILD_PYTHON_BINDINGS)
    # find nanobind python library
    execute_process(
        COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
        OUTPUT_STRIP_TRAILING_WHITESPACE OUTPUT_VARIABLE nanobind_ROOT
        RESULT_VARIABLE nb_result
    )
    if(NOT nb_result EQUAL 0)
        message(FATAL_ERROR "nanobind not found for ${Python_EXECUTABLE}, run: pip install nanobind")
    endif()

    find_package(nanobind CONFIG REQUIRED)

    nanobind_add_module(__smb_native__ NB_STATIC NOMINSIZE
        src/bindings/python_bindings.cpp
        $<TARGET_OBJECTS:smb_core>
    )

    target_include_directories(__smb_native__ PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(OpenMP_CXX_FOUND)
        target_link_libraries(__smb_native__ PRIVATE OpenMP::OpenMP_CXX)
    endif()

    set_target_properties(__smb_native__ PROPERTIES INTERPROCEDURAL_OPTIMIZATION ${SMB_IPO_OK})

    if(SKBUILD)
        # pip build (scikit-build-core): installed into the wheel, the committed .pyi is packaged from python/
        install(TARGETS __smb_native__ LIBRARY DESTINATION smb_native)
    else()
        # plain cmake dev build: drop module and stub straight into the source tree
        set_target_properties(__smb_native__ PROPERTIES
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/python/smb_native"
        )

        # .pyi generation
        nanobind_add_stub(smb_stub
            MODULE __smb_native__
            OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/python/smb_native/__smb_native__.pyi
            PYTHON_PATH $<TARGET_FILE_DIR:__smb_native__>
            DEPENDS __smb_native__
        )
    endif()
endif()