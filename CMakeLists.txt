

cmake_minimum_required(VERSION 3.13)

# Haal de Pico SDK binnen via de import-file
include(pico_sdk_import.cmake)

project(pdi_dude_test C CXX ASM)

# Initialiseer de Raspberry Pi Pico SDK
pico_sdk_init()

add_executable(pdi_dude
    main.c
)

# Schakel USB-serial (CDC) in en zet UART uit
pico_enable_stdio_usb(pdi_dude 1)
pico_enable_stdio_uart(pdi_dude 0)

# Genereer extra uitvoerbestanden zoals .uf2
pico_add_extra_outputs(pdi_dude)

# Koppel de standaard Pico bibliotheken
target_link_libraries(pdi_dude 
    pico_stdlib
)
