cmake_minimum_required(VERSION 3.20)
project(habits C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra -Wpedantic -D_POSIX_C_SOURCE=200809L)

file(GLOB WEB_FILES CONFIGURE_DEPENDS
    web/*.html web/*.css web/*.js web/i18n/*.js)
set(WEB_BUNDLE ${CMAKE_BINARY_DIR}/generated/web_bundle.c)

add_custom_command(
    OUTPUT ${WEB_BUNDLE}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/generated
    COMMAND sh ${CMAKE_SOURCE_DIR}/tools/embed_web.sh ${WEB_BUNDLE} ${WEB_FILES}
    DEPENDS ${WEB_FILES} ${CMAKE_SOURCE_DIR}/tools/embed_web.sh
    COMMENT "Embedding web assets")

# The web build is always available and has no dependencies.
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.c)
list(FILTER APP_SOURCES EXCLUDE REGEX "/src/desktop/")

add_executable(habits ${APP_SOURCES} ${WEB_BUNDLE})
target_include_directories(habits PRIVATE src)

# ---------------------------------------------------------------------------
# Optional native client on Raven: -DHABITS_DESKTOP=ON
#
# Needs a Raven build (tools/get_raven.sh) and the X11 development files.
# When those are not installed system-wide, a local sysroot under build/sysroot
# (created by the project's own setup) is picked up automatically.
# ---------------------------------------------------------------------------
option(HABITS_DESKTOP "Build the native Raven client" OFF)

if(HABITS_DESKTOP)
    set(RAVEN_DIR "${CMAKE_SOURCE_DIR}/build/raven" CACHE PATH "Raven source tree")
    set(RAVEN_LIB "${CMAKE_SOURCE_DIR}/build/raven-build/libraven.a"
        CACHE FILEPATH "libraven.a")

    if(NOT EXISTS "${RAVEN_LIB}")
        message(FATAL_ERROR
            "Raven is not built (${RAVEN_LIB} is missing). Run tools/get_raven.sh first.")
    endif()

    file(GLOB DESKTOP_SOURCES CONFIGURE_DEPENDS src/desktop/*.c)
    file(GLOB CORE_SOURCES CONFIGURE_DEPENDS src/core/*.c)

    add_executable(habits-desktop
        ${DESKTOP_SOURCES}
        ${CORE_SOURCES}
        src/infra/util.c
        src/infra/json.c)
    target_include_directories(habits-desktop PRIVATE src "${RAVEN_DIR}/include")

    set(SYSROOT "${CMAKE_SOURCE_DIR}/build/sysroot")
    if(EXISTS "${SYSROOT}/usr/include/X11/Xlib.h")
        target_include_directories(habits-desktop PRIVATE "${SYSROOT}/usr/include")
        target_link_directories(habits-desktop PRIVATE "${SYSROOT}/usr/lib64")
    endif()

    target_link_libraries(habits-desktop PRIVATE "${RAVEN_LIB}" X11 pthread dl m)
endif()
