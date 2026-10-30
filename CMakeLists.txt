cmake_minimum_required(VERSION 3.20)
project(daychain C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L)

# Raven is the GUI library; tools/get_raven.sh clones and builds it.
set(RAVEN_DIR "${CMAKE_SOURCE_DIR}/build/raven" CACHE PATH "Raven source tree")
set(RAVEN_LIB "${CMAKE_SOURCE_DIR}/build/raven-build/libraven.a"
    CACHE FILEPATH "libraven.a")

if(NOT EXISTS "${RAVEN_LIB}")
    message(FATAL_ERROR
        "Raven is not built (${RAVEN_LIB} is missing). Run tools/get_raven.sh first.")
endif()

file(GLOB APP_SOURCES CONFIGURE_DEPENDS src/desktop/*.c src/core/*.c)
list(APPEND APP_SOURCES src/infra/util.c src/infra/json.c)

add_executable(daychain ${APP_SOURCES})
target_include_directories(daychain PRIVATE src "${RAVEN_DIR}/include")

# A local sysroot is used when the X11 headers are not installed system-wide.
set(SYSROOT "${CMAKE_SOURCE_DIR}/build/sysroot")
if(EXISTS "${SYSROOT}/usr/include/X11/Xlib.h")
    target_include_directories(daychain PRIVATE "${SYSROOT}/usr/include")
    target_link_directories(daychain PRIVATE "${SYSROOT}/usr/lib64")
endif()

target_link_libraries(daychain PRIVATE "${RAVEN_LIB}" X11 pthread dl m)
