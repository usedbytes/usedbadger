cmake_minimum_required(VERSION 3.12)

# Change your executable name to something creative!
set(NAME usedbadger) # <-- Name your project/executable here!

include(pimoroni_pico_import.cmake)
include(pico_sdk_import.cmake)

# Gooey boilerplate
project(${NAME} C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the SDK
pico_sdk_init()

# Add your source files
add_executable(${NAME}
    main.c # <-- Add source files here!

    ${CMAKE_CURRENT_LIST_DIR}/badger.cpp

    ${CMAKE_CURRENT_LIST_DIR}/usb.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_descriptors.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_msc.c
    ${CMAKE_CURRENT_LIST_DIR}/usb_filesystem.c

    ${CMAKE_CURRENT_LIST_DIR}/usb_stdio.c
    ${CMAKE_CURRENT_LIST_DIR}/reset_interface.c

    ${CMAKE_CURRENT_LIST_DIR}/lfs_pico_flash.c

    ${CMAKE_CURRENT_LIST_DIR}/screen_page.c

    ${CMAKE_CURRENT_LIST_DIR}/error_disk.c
    ${CMAKE_CURRENT_LIST_DIR}/fat_ramdisk.c
    ${CMAKE_CURRENT_LIST_DIR}/fatfs/ff.c
    ${CMAKE_CURRENT_LIST_DIR}/fatfs/ffunicode.c

    ${CMAKE_CURRENT_LIST_DIR}/littlefs/bd/lfs_rambd.c
    ${CMAKE_CURRENT_LIST_DIR}/littlefs/lfs.c
    ${CMAKE_CURRENT_LIST_DIR}/littlefs/lfs.h
    ${CMAKE_CURRENT_LIST_DIR}/littlefs/lfs_util.c
    ${CMAKE_CURRENT_LIST_DIR}/littlefs/lfs_util.h
)

# Make sure TinyUSB can find tusb_config.h
target_include_directories(usedbadger PUBLIC
        ${CMAKE_CURRENT_LIST_DIR})

target_include_directories(usedbadger PUBLIC
        ${CMAKE_CURRENT_LIST_DIR}/littlefs)

# Include required libraries
# This assumes `pimoroni-pico` is stored alongside your project
include(drivers/uc8151/uc8151)
include(libraries/hershey_fonts/hershey_fonts)
include(libraries/badger2040/badger2040)
include(libraries/bitmap_fonts/bitmap_fonts)

# Don't forget to link the libraries you need!
target_link_libraries(${NAME}
    badger2040
    bitmap_fonts
    hardware_spi
    hershey_fonts
    pico_multicore
    pico_unique_id
    pico_usb_reset_interface_headers
    pico_util
    pico_stdlib
    tinyusb_board
    tinyusb_device
    uc8151
)

# create map/bin/hex file etc.
pico_add_extra_outputs(${NAME})

# Set up files for the release packages
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${NAME}.uf2
    ${CMAKE_CURRENT_LIST_DIR}/README.md
    DESTINATION .
)

set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_GENERATOR "ZIP" "TGZ")
include(CPack)
