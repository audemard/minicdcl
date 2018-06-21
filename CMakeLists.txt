cmake_minimum_required(VERSION 2.6 FATAL_ERROR)

project(minicdcl)

#--------------------------------------------------------------------------------------------------
# Configurable options:

option(STATIC_BINARIES "Link binaries statically." ON)
option(USE_SORELEASE   "Use SORELEASE in shared library filename." ON)

#--------------------------------------------------------------------------------------------------
# Library version:

set(MINISAT_SOMAJOR   2)
set(MINISAT_SOMINOR   2)
set(MINISAT_SORELEASE 0)

# Compute VERSION and SOVERSION:
if (USE_SORELEASE)
    set(MINISAT_VERSION ${MINISAT_SOMAJOR}.${MINISAT_SOMINOR}.${MINISAT_SORELEASE})
else()
    set(MINISAT_VERSION ${MINISAT_SOMAJOR}.${MINISAT_SOMINOR})
endif()
set(MINISAT_SOVERSION ${MINISAT_SOMAJOR})

#--------------------------------------------------------------------------------------------------
# Dependencies:

find_package(ZLIB)
include_directories(${ZLIB_INCLUDE_DIR})
include_directories(${minicdcl_SOURCE_DIR})

#--------------------------------------------------------------------------------------------------
# Compile flags:

add_definitions(-D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS)

#--------------------------------------------------------------------------------------------------
# Build Targets:

set(MINISAT_LIB_SOURCES
        utils/Options.cc
        utils/System.cc
        core/Solver.cc
)

add_library(minicdcl-lib-static STATIC ${MINISAT_LIB_SOURCES})
add_library(minicdcl-lib-shared SHARED ${MINISAT_LIB_SOURCES})

target_link_libraries(minicdcl-lib-shared ${ZLIB_LIBRARY})
target_link_libraries(minicdcl-lib-static ${ZLIB_LIBRARY})

add_executable(minicdcl_core core/Main.cc)

if(STATIC_BINARIES)
    target_link_libraries(minicdcl_core minicdcl-lib-static)
else()
    target_link_libraries(minicdcl_core minicdcl-lib-shared)
endif()

set_target_properties(minicdcl-lib-static PROPERTIES OUTPUT_NAME "minicdcl")
set_target_properties(minicdcl-lib-shared
        PROPERTIES
        OUTPUT_NAME "minicdcl"
        VERSION ${MINISAT_VERSION}
        SOVERSION ${MINISAT_SOVERSION})


#--------------------------------------------------------------------------------------------------
# Installation targets:

install(TARGETS minicdcl-lib-static minicdcl-lib-shared minicdcl_core
        RUNTIME DESTINATION bin
        LIBRARY DESTINATION lib
        ARCHIVE DESTINATION lib)

install(DIRECTORY mtl utils core simp
        DESTINATION include/minicdcl
        FILES_MATCHING PATTERN "*.h")