# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico CACHE STRING "Board type")

cmake_minimum_required(VERSION 3.13)

# Use the SDK and picotool installed by the Raspberry Pi Pico extension
set(PICO_SDK_PATH "$ENV{USERPROFILE}/.pico-sdk/sdk/2.3.1")
set(picotool_DIR "$ENV{USERPROFILE}/.pico-sdk/picotool/2.3.1/picotool")
include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(Time_Capsule_Exhibit C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

add_executable(pirSensor
    pirSensor.c
    pir_read.S
)

target_link_libraries(pirSensor
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_pwm
    hardware_adc
)

# Wokwi shows UART output
pico_enable_stdio_usb(pirSensor 0)
pico_enable_stdio_uart(pirSensor 1)

pico_add_extra_outputs(pirSensor)