# Copyright 2026 NXP
# NXP Proprietary. This software is owned or controlled by NXP and may only be used strictly in
# accordance with the applicable license terms. By expressly accepting such terms or by downloading, installing,
# activating and/or otherwise using the software, you are agreeing that you have read, and that you agree to
# comply with and are bound by, such license terms.  If you do not agree to be bound by the applicable license
# terms, then you may not retain, install, activate or otherwise use the software.

cmake_minimum_required(VERSION 3.13.5)

project(simulator)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-fmessage-length=0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)

# SDL2 configuration
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(SDL2_INCLUDE_DIRS "${PROJECT_SOURCE_DIR}/modules/SDL2/include")
  set(SDL2_LIB_DIR "${PROJECT_SOURCE_DIR}/modules/SDL2/lib")
  set(SDL2_LIBRARIES "${SDL2_LIB_DIR}/libSDL2.dll.a")
  
  # Use add_custom_command to copy at build time instead of configure time
  add_custom_command(
    OUTPUT ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/SDL2.dll
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${SDL2_LIB_DIR}/SDL2.dll"
      ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/SDL2.dll
    DEPENDS "${SDL2_LIB_DIR}/SDL2.dll"
    COMMENT "Copying SDL2.dll to output directory"
  )
  add_custom_target(copy_sdl2_dll ALL DEPENDS ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/SDL2.dll)
else()
  # On macOS, prefer Homebrew's SDL2 dylib over /Library/Frameworks/SDL2.framework.
  # "library load disallowed by system policy" at runtime.
  if(APPLE)
    set(CMAKE_FIND_FRAMEWORK LAST)
    list(APPEND CMAKE_PREFIX_PATH "/opt/homebrew" "/usr/local")
  endif()
  find_package(SDL2 REQUIRED)
endif()

# LVGL configuration options
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "Build LVGL demos" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "Build LVGL examples" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "Use internal ThorVG" FORCE)

# Find LVGL library
set(LVGL_PATHS
  "../lvgl"
  "../armgcc/middleware/lvgl"
  "../iar/middleware/lvgl"
  "../keil/middleware/lvgl"
  "../mcuxpresso/lvgl"
  "../vscode/middleware/lvgl"
)

set(LVGL_FOUND FALSE)
foreach(LVGL_PATH ${LVGL_PATHS})
  cmake_path(ABSOLUTE_PATH LVGL_PATH BASE_DIRECTORY ${PROJECT_SOURCE_DIR} OUTPUT_VARIABLE FULL_LVGL_PATH)
  if(EXISTS ${FULL_LVGL_PATH})
    add_subdirectory(${FULL_LVGL_PATH} lvgl)
    message(STATUS "Found LVGL at: ${FULL_LVGL_PATH}")
    set(LVGL_FOUND TRUE)
    break()
  endif()
endforeach()

if(NOT LVGL_FOUND)
  message(FATAL_ERROR "LVGL library not found in any of the expected locations")
endif()

# LVGL include directories
target_include_directories(lvgl PUBLIC
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/../../custom
  ${SDL2_INCLUDE_DIRS}
)

# UI source files
file(GLOB_RECURSE UI_SOURCES
  ${PROJECT_SOURCE_DIR}/../../generated/*.c
  ${PROJECT_SOURCE_DIR}/../../custom/*.c
)

# Exclude board-only sources (real_time_edge) from simulator build
list(FILTER UI_SOURCES EXCLUDE REGEX ".*/custom/real_time_edge/.*")

# Generated library
add_library(generated STATIC ${UI_SOURCES})
target_compile_definitions(generated PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_include_directories(generated PUBLIC
  ${FULL_LVGL_PATH}
  ${FULL_LVGL_PATH}/src/misc
  ${PROJECT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/../../generated
  ${PROJECT_SOURCE_DIR}/../../generated/events
  ${PROJECT_SOURCE_DIR}/../../custom
)

# Common configuration interface library
add_library(common_configs INTERFACE)
target_include_directories(common_configs INTERFACE
  ${PROJECT_SOURCE_DIR}/..
  ${PROJECT_SOURCE_DIR}/../../generated
)
target_compile_definitions(common_configs INTERFACE LV_CONF_INCLUDE_SIMPLE)

target_link_libraries(common_configs INTERFACE
  lvgl
  generated
  ${SDL2_LIBRARIES}
)

# Main executable
set(SOURCES main.c simulator_icon.c ${UI_SOURCES})
add_executable(simulator ${SOURCES})
target_link_libraries(simulator PRIVATE common_configs)

# Windows specific configuration
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  add_dependencies(simulator generated copy_sdl2_dll)
  
  # Run target
  add_custom_target(run 
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/simulator.exe
    DEPENDS simulator
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Running simulator"
  )
else()
  add_dependencies(simulator generated)
  
  add_custom_target(run 
    COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/simulator
    DEPENDS simulator
    WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}
    COMMENT "Running simulator"
  )
endif()
