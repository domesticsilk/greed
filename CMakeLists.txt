cmake_minimum_required(VERSION 3.16)
project(greed C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(JSONC REQUIRED json-c)
pkg_check_modules(CURL REQUIRED libcurl)
find_package(X11 REQUIRED)
find_package(Threads REQUIRED)

# CUI is big; compile it once, without our warning flags.
add_library(cui_vendor STATIC vendor/cui/cui.c)
target_include_directories(cui_vendor PUBLIC vendor/cui)
target_compile_options(cui_vendor PRIVATE -w)
target_link_libraries(cui_vendor PUBLIC ${X11_LIBRARIES} Xext ${CMAKE_DL_LIBS} m Threads::Threads)

# The marketplace fallback ships inside the binary.
file(READ "${CMAKE_SOURCE_DIR}/marketplace/catalog.json" GREED_CATALOG_JSON)
string(REPLACE "\\" "\\\\" GREED_CATALOG_JSON "${GREED_CATALOG_JSON}")
string(REPLACE "\"" "\\\"" GREED_CATALOG_JSON "${GREED_CATALOG_JSON}")
string(REPLACE "\n" "\\n" GREED_CATALOG_JSON "${GREED_CATALOG_JSON}")
file(WRITE "${CMAKE_BINARY_DIR}/catalog_data.h"
     "static const char *fallback_catalog = \"${GREED_CATALOG_JSON}\";\n")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             "${CMAKE_SOURCE_DIR}/marketplace/catalog.json")

add_executable(greed
    src/main.c
    src/ui.c
    src/cli.c
    src/model.c
    src/rpc.c
    src/config.c
    src/checksum.c
    src/theme.c
    src/market.c
    src/ytdlp.c
    src/update.c
    src/x11.c
    src/links.c
    src/single.c
)
target_include_directories(greed PRIVATE src vendor/cui ${CMAKE_BINARY_DIR})
target_compile_options(greed PRIVATE -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(greed PRIVATE
    cui_vendor
    ${JSONC_LIBRARIES}
    ${CURL_LIBRARIES}
    ${X11_LIBRARIES}
    m
    dl
    Threads::Threads
)
target_include_directories(greed PRIVATE ${JSONC_INCLUDE_DIRS} ${CURL_INCLUDE_DIRS})

enable_testing()
add_executable(greed-contrast-check tests/contrast.c)
target_include_directories(greed-contrast-check PRIVATE src vendor/cui ${JSONC_INCLUDE_DIRS})
target_compile_definitions(greed-contrast-check PRIVATE
                           GREED_MARKETPLACE_DIR="${CMAKE_SOURCE_DIR}/marketplace")
target_link_libraries(greed-contrast-check PRIVATE cui_vendor ${JSONC_LIBRARIES} m)
add_test(NAME contrast COMMAND greed-contrast-check)

add_executable(greed-links-check tests/links.c)
target_include_directories(greed-links-check PRIVATE src)
add_test(NAME links COMMAND greed-links-check)

install(TARGETS greed RUNTIME DESTINATION bin)
