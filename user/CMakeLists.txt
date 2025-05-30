file(GLOB user_sources CONFIGURE_DEPENDS "*.S")

add_library(user STATIC ${user_sources})

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_COMPILER ${aarch64_gcc})
set(CMAKE_ASM_COMPILER ${aarch64_gcc})

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../musl/obj/include)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../musl/arch/aarch64)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../musl/arch/generic)

set(LIBC_SPEC_OUT ${CMAKE_CURRENT_BINARY_DIR}/../../musl-gcc.specs)
set(CMAKE_C_FLAGS "-specs ${LIBC_SPEC_OUT} -std=gnu99  -MMD -MP -static -fno-plt -fno-pic -fpie -z max-page-size=4096 -s")
set(CMAKE_EXE_LINKER_FLAGS "")

# Add targets here if needed
# Note: you need to add the new executable name to boot/CMakeLists.txt too! Check that
set(bin_list cat echo init ls sh mkdir usertests mkfs mmaptest)

add_custom_target(user_bin
    DEPENDS ${bin_list})
foreach(bin ${bin_list})
    add_executable(${bin} ${CMAKE_CURRENT_SOURCE_DIR}/${bin}/main.c)
endforeach(bin)
