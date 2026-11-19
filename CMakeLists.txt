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
target_link_libraries(cui_vendor PUBLIC ${X11_LIBRARIES} ${CMAKE_DL_LIBS} m Threads::Threads)

add_executable(greed
    src/main.c
    src/ui.c
    src/cli.c
    src/model.c
    src/rpc.c
    src/config.c
    src/x11.c
)
target_include_directories(greed PRIVATE src vendor/cui)
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

install(TARGETS greed RUNTIME DESTINATION bin)
