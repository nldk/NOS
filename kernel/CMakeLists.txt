cmake_minimum_required(VERSION 3.16)
project(kernel C ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB_RECURSE KERNEL_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.S"
)

if(NOT KERNEL_SOURCES)
    message(FATAL_ERROR "No C source files found in src/")
endif()

add_executable(kernel.elf ${KERNEL_SOURCES})

target_compile_options(kernel.elf PRIVATE
    -m64
    -ffreestanding
    -fno-stack-protector
    -fno-pic
    -fno-pie
    -fno-builtin
    -nostdlib
    -mno-red-zone
    -Wall
    -Wextra
    -Os
    -mno-sse
    -mno-sse2
    -mno-mmx
    -mno-80387
    -msoft-float
)

target_link_options(kernel.elf PRIVATE
    LINKER:-m,elf_x86_64
    LINKER:-T,${CMAKE_CURRENT_SOURCE_DIR}/linker.ld
    -nostdlib
    -no-pie
)

find_program(OBJCOPY_TOOL NAMES ${CMAKE_OBJCOPY} objcopy llvm-objcopy REQUIRED)

add_custom_command(
    TARGET kernel.elf
    POST_BUILD
    COMMAND ${OBJCOPY_TOOL} -O binary $<TARGET_FILE:kernel.elf> ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/kernel.bin
    COMMENT "Generating kernel.bin"
)
