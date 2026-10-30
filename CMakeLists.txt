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

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS src/*.c)

add_executable(habits ${APP_SOURCES} ${WEB_BUNDLE})
target_include_directories(habits PRIVATE src)
