add_executable(my_terminal
	main.c
	tmds_encode_font_2bpp.S
	tmds_encode_font_2bpp.h
)

target_include_directories(my_terminal PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})


# Enable USB for stdio
pico_enable_stdio_usb(my_terminal 1)
# Optionally disable UART if not needed
pico_enable_stdio_uart(my_terminal 0)

target_compile_definitions(my_terminal PRIVATE
	DVI_VERTICAL_REPEAT=1
	)

# We have a lot in SRAM4 (particularly TMDS LUT) but don't need much stack on
# core 1. Probably even 256 bytes would be fine.
target_compile_definitions(my_terminal PRIVATE
	DVI_DEFAULT_SERIAL_CONFIG=${DVI_DEFAULT_SERIAL_CONFIG}
	PICO_CORE1_STACK_SIZE=0x200
	)

target_link_libraries(my_terminal
	pico_stdlib
	pico_multicore
	libdvi
    hardware_uart
    hardware_pio
)

# create map/bin/hex file etc.
pico_add_extra_outputs(my_terminal)
