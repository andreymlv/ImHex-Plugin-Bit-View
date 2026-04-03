cmake_minimum_required(VERSION 3.16)
set(CMAKE_CXX_STANDARD 23)

project(bit_view)
set(IMHEX_VERSION "1.38.1")

# Include ImHex cmake module #
set(IMHEX_BASE_FOLDER ${CMAKE_CURRENT_SOURCE_DIR}/ImHex)
set(CMAKE_MODULE_PATH "${IMHEX_BASE_FOLDER}/cmake/modules")
include("${IMHEX_BASE_FOLDER}/cmake/build_helpers.cmake")

# Include ImHex
add_subdirectory(${IMHEX_BASE_FOLDER})

include(ImHexPlugin)

add_imhex_plugin(
    NAME
        bit_view
    SOURCES
        source/plugin_bit_view.cpp
        source/content/views/view_bit_viewer.cpp
    INCLUDES
        include
    LIBRARIES
        fonts
)
