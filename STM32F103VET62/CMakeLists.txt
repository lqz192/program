cmake_minimum_required(VERSION 3.22)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME STM32F103VET62)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME}
        User/Drivers/encoder.c
        User/Drivers/encoder.h
        User/Drivers/tof.c
        User/Drivers/tof.h
        User/Bsp/debug_uart.c
        User/Bsp/debug_uart.h
        User/App/app_encoder.c
        User/App/app_encoder.h
        User/App/app_tof.c
        User/App/app_tof.h)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    ThirdParty/vl53l0x/vl53l0x_api.c
    ThirdParty/vl53l0x/vl53l0x_api.h
    ThirdParty/vl53l0x/vl53l0x_api_calibration.c
    ThirdParty/vl53l0x/vl53l0x_api_calibration.h
    ThirdParty/vl53l0x/vl53l0x_api_core.c
    ThirdParty/vl53l0x/vl53l0x_api_core.h
    ThirdParty/vl53l0x/vl53l0x_api_ranging.c
    ThirdParty/vl53l0x/vl53l0x_api_ranging.h
    ThirdParty/vl53l0x/vl53l0x_api_strings.c
    ThirdParty/vl53l0x/vl53l0x_api_strings.h
    ThirdParty/vl53l0x/vl53l0x_def.h
    ThirdParty/vl53l0x/vl53l0x_device.h
    ThirdParty/vl53l0x/vl53l0x_interrupt_threshold_settings.h
    ThirdParty/vl53l0x/vl53l0x_platform.c
    ThirdParty/vl53l0x/vl53l0x_platform.h
    ThirdParty/vl53l0x/vl53l0x_platform_log.c
    ThirdParty/vl53l0x/vl53l0x_platform_log.h
    ThirdParty/vl53l0x/vl53l0x_tuning.h
    ThirdParty/vl53l0x/vl53l0x_types.h
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    User/Drivers
    User/Bsp
    User/App
    ThirdParty/vl53l0x
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx

    # Add user defined libraries
)
