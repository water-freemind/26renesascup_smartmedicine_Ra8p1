# Please see README.md for build instructions

cmake_minimum_required(VERSION 3.16.4)

set(CMAKE_SYSTEM_NAME Generic)

set(CMAKE_SYSTEM_PROCESSOR arm)

# Use add_library() with the STATIC option to name the source file in the generated project.
# This avoids running the linker and is intended for use with cross-compiling toolchains that
# cannot link without custom flags or linker scripts
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_CONFIGURATION_TYPES Debug Release RelWithDebInfo MinSizeRel)

if(BUILD_KIT)
	message(NOTICE "Build Kit is defined as ${BUILD_KIT}")
endif()

message(NOTICE "CMAKE_CURRENT_BINARY_DIR is defined as ${CMAKE_CURRENT_BINARY_DIR}")

get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

if(isMultiConfig)
    message(NOTICE "Multi-config generator(${CMAKE_GENERATOR}) is active")
else()
    message(NOTICE "Single-config generator(${CMAKE_GENERATOR}) is active")
	if(DEFINED CMAKE_BUILD_TYPE)
	 	message(NOTICE "CMAKE_BUILD_TYPE is defined as ${CMAKE_BUILD_TYPE}")
	elseif(DEFINED BUILD_TYPE)
		set(CMAKE_BUILD_TYPE ${BUILD_TYPE})
		message(NOTICE "CMAKE_BUILD_TYPE is set to BUILD_TYPE ENV ${CMAKE_BUILD_TYPE}")
	else()
		message(FATAL_ERROR "Please specify the CMAKE_BUILD_TYPE with -DCMAKE_BUILD_TYPE=")
	endif()
    #set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE})
	message(NOTICE "CMAKE_RUNTIME_OUTPUT_DIRECTORY is defined as ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
endif()

cmake_minimum_required(VERSION 3.16.4)

# Project configuration
project(26renesascup_smartmedicine_Ra8p1
	VERSION 1.0.0
	LANGUAGES C CXX ASM)

set(CMAKE_CROSSCOMPILING "TRUE")
set(CMAKE_EXPORT_COMPILE_COMMANDS "TRUE")

include(${CMAKE_CURRENT_LIST_DIR}/Config.cmake)

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GeneratedCfg.cmake)

# If required user can amend RASC generated flags here, e.g. list(REMOVE_ITEM RASC_CMAKE_C_FLAGS "-Wall")

# 移除 RASC 默认过于激进的警告标志，消除 ra/(FSP/LVGL/FreeRTOS/CMSIS) 与 ra_gen/ 库代码的噪音警告
# 保留 -Wall -Wextra，src/ 自有代码仍保持严格检查（实测 0 警告，不受影响）
# 注意：需在 include GeneratedSrc.cmake 之前执行
foreach(flag -Wconversion -Wshadow -Waggregate-return -Wfloat-equal -Wlogical-op -Wmissing-declarations)
    list(REMOVE_ITEM RASC_CMAKE_C_FLAGS ${flag})
    list(REMOVE_ITEM RASC_CMAKE_CXX_FLAGS ${flag})
endforeach()

include(${CMAKE_CURRENT_LIST_DIR}/cmake/GeneratedSrc.cmake)

# GeneratedSrc.cmake intentionally discovers the complete FSP source tree.
# The current RASC configuration disables the LVGL D/AVE 2D backend so that
# LVGL's software matrix rotation can provide the 640x480 landscape canvas.
# Exclude the unconfigured DRW implementation; otherwise it is compiled without
# the DRW_CFG_INT_IRQ instance macros that RASC correctly stopped generating.
get_target_property(RA8P1_GENERATED_SOURCES ${PROJECT_NAME}.elf SOURCES)
list(FILTER RA8P1_GENERATED_SOURCES EXCLUDE REGEX
    "/ra/fsp/src/r_drw/|/ra/tes/dave2d/|/ra/lvgl/lvgl/src/draw/dave2d/")
set_property(TARGET ${PROJECT_NAME}.elf PROPERTY SOURCES "${RA8P1_GENERATED_SOURCES}")

# ra/ 与 ra_gen/ 为 RASC/供应商库代码（FSP/LVGL/FreeRTOS/dave2d），禁止修改其源文件，
# 仅在这些库源文件的编译单元内抑制已知噪音警告；src/ 自有代码保持严格检查
file(GLOB_RECURSE RA_LIB_C_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/ra/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ra_gen/*.c)
set_source_files_properties(${RA_LIB_C_SOURCES}
    PROPERTIES COMPILE_OPTIONS "-Wno-shift-negative-value;-Wno-unused-parameter;-Wno-unused-function;-Wno-attributes;-Wno-implicit-fallthrough;-Wno-unused-variable")

# 自有代码按应用、中间件、驱动和入口分层；RASC 生成文件仍由
# cmake/GeneratedSrc.cmake 管理，禁止在此处覆盖其生成规则。
target_include_directories(${PROJECT_NAME}.elf
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/middleware/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/inc
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc/quirc-master/lib
)

file(GLOB_RECURSE PROJECT_OWN_C_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/middleware/src/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/src/driver/src/*.c)
target_sources(${PROJECT_NAME}.elf PRIVATE ${PROJECT_OWN_C_SOURCES})

# GUI Guider project management:
# - gui/RA8P1 is the single source package copied from the designer project.
# - Do not import its desktop LVGL/SDL platform because RASC already provides LVGL.
# - GUI Guider only creates the compilable C sources after "Generate Code".
#   Until then, keep the firmware build unchanged and report the missing output.
set(GUI_GUIDER_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/gui/RA8P1)
set(GUI_GUIDER_GENERATED_DIR ${GUI_GUIDER_ROOT}/generated)
option(RA8P1_GUI_ENABLE "Link and start the GUI Guider UI on the MIPI display" ON)
option(RA8P1_CAMERA_RTT_ONLY "Build an isolated OV7725 RTT preview firmware" ON)
option(RA8P1_CAMERA_SLOW_XCLK "Slow-XCLK diagnostic: 100 kHz XCLK + internal color bar to detect a dead OV7725 clock/output stage" OFF)
option(RA8P1_XCLK_KHZ "OV7725 XCLK frequency override in kHz (0 = use configuration.xml period; tuning tool)" 0)
option(RA8P1_CAMERA_VTS "OV7725 VTS override (0 = leave sensor default; frame-rate tuning, min ~264)" 0)
option(RA8P1_CAMERA_COM5 "OV7725 COM5 (0x0E) override (0 = stock 0xF5; frame-rate divider tuning)" 0)
option(RA8P1_CAMERA_AUTOTEST "Headless on-target test: simulate Pickup/Scan page enter+leave to verify on-demand capture (OFF)" OFF)

if(RA8P1_CAMERA_AUTOTEST)
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CAMERA_AUTOTEST=1)
endif()

if(RA8P1_CAMERA_COM5)
    # COM5 low nibble selects the frame-rate divider; 0xF5 is the stock
    # value.  Lower nibbles may raise FPS at the same XCLK.
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CAMERA_COM5_OVERRIDE=${RA8P1_CAMERA_COM5})
endif()

if(RA8P1_CAMERA_VTS)
    # Write VTS (0x2D/0x2E) after the sensor init table to shrink vertical
    # blanking: frame rate = PCLK/(HTS*VTS), and XCLK is capped ~13 MHz by
    # this module, so VTS is the remaining software lever.
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CAMERA_VTS_OVERRIDE=${RA8P1_CAMERA_VTS})
endif()

if(RA8P1_XCLK_KHZ)
    # Runtime GPT10 period override: PCLKD=250 MHz -> period = 250000/kHz.
    # Used to tune the highest XCLK frequency this OV7725 module will start at.
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE CAMERA_XCLK_KHZ=${RA8P1_XCLK_KHZ})
endif()

if(RA8P1_CAMERA_SLOW_XCLK)
    # Slow clock + forced color bar: the sensor is fully initialized at the
    # normal 25 MHz first (SCCB is derived from XCLK and breaks below the
    # operating range), then XCLK drops to 12 MHz - still inside the PLL
    # lock range, but slow enough that PCLK (~24 MHz), HREF (~10 kHz) and
    # VSYNC (~14 Hz) become countable by the J-Link pin-toggle counters.
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
        CAMERA_SLOW_XCLK_HZ=12000000
        CAMERA_COLOR_BAR_DIAG=1)
endif()

if(RA8P1_CAMERA_RTT_ONLY)
    # The known-good camera commit ran with D-Cache disabled.  Keep the
    # isolated CEU/DMA diagnostic on that baseline; re-enable cache only after
    # the camera stream is proven and the display path is tested separately.
    target_compile_definitions(${PROJECT_NAME}.elf PRIVATE BSP_CFG_DCACHE_ENABLED=0)
endif()

# The panel scans in portrait while the product UI is landscape.  The FSP
# LVGL port uses direct frame buffers, so LVGL matrix rotation is required in
# addition to lv_display_set_rotation().
target_compile_definitions(${PROJECT_NAME}.elf PRIVATE
    CONFIG_LV_USE_FLOAT=1
    CONFIG_LV_USE_MATRIX=1
    CONFIG_LV_DRAW_TRANSFORM_USE_MATRIX=1)

if(EXISTS ${GUI_GUIDER_GENERATED_DIR}/gg_utils.c AND
   EXISTS ${GUI_GUIDER_GENERATED_DIR}/gui_guider.h)
    file(GLOB_RECURSE GUI_GUIDER_C_SOURCES CONFIGURE_DEPENDS
        ${GUI_GUIDER_GENERATED_DIR}/*.c
        ${GUI_GUIDER_ROOT}/custom/*.c)

    # 图标像素数据已迁移到 OSPI 图标区（tools/gen_ospi_icons.py + ospi_burn_icons.py），
    # 由 src/middleware/src/ospi_icons.c 提供 lv_image_dsc_t（data 指向 OSPI mmap）。
    # 不再编译 GUI Guider 生成的 images/*.c，避免重复定义并省内部 Flash。
    list(FILTER GUI_GUIDER_C_SOURCES EXCLUDE REGEX "[/\\\\]assets[/\\\\]images[/\\\\].*\\.c$")

    target_sources(${PROJECT_NAME}.elf PRIVATE ${GUI_GUIDER_C_SOURCES})
    target_include_directories(${PROJECT_NAME}.elf PRIVATE
        ${GUI_GUIDER_GENERATED_DIR}
        ${GUI_GUIDER_GENERATED_DIR}/events
        ${GUI_GUIDER_ROOT}/custom)

    # Keep GUI-specific compatibility flags away from FSP/LVGL library files.
    # This avoids rebuilding the entire generated platform whenever the GUI
    # package changes and ensures generated files remain untouched.
    set_source_files_properties(${GUI_GUIDER_C_SOURCES} PROPERTIES
        COMPILE_DEFINITIONS "LV_CONF_INCLUDE_SIMPLE"
        COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/src/app/inc/gui_guider_lvgl_compat.h")
    # Build one coherent definition set per mode.  Re-appending definitions
    # through several set_source_files_properties() calls can leave both the
    # RTT-only and display-preview paths enabled in compile_commands.json.
    set(GUI_GUIDER_ENTRY_DEFINITIONS GUI_GUIDER_AVAILABLE=1)
    if(RA8P1_CAMERA_RTT_ONLY)
        list(APPEND GUI_GUIDER_ENTRY_DEFINITIONS
            CAMERA_RTT_PREVIEW_ENABLE=1
            CAMERA_RTT_ONLY=1
            CAMERA_PREVIEW_SCREEN_ENABLE=0)
    elseif(RA8P1_GUI_ENABLE)
        list(APPEND GUI_GUIDER_ENTRY_DEFINITIONS
            CAMERA_PREVIEW_SCREEN_ENABLE=1
            CAMERA_CAPTURE_ON_DEMAND=1)
    endif()

    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/LVGL_thread_entry.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/app/src/gui_app.c
        PROPERTIES COMPILE_DEFINITIONS "${GUI_GUIDER_ENTRY_DEFINITIONS}")

    # CST816S has passed the standalone IIC0 coordinate test.  Run the normal
    # application path now so LVGL owns the input device and the camera thread
    # is not trapped in the temporary touch-only loop.
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/src/LVGL_thread_entry.c
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Camera_thread_entry.c
        PROPERTIES COMPILE_DEFINITIONS "${GUI_GUIDER_ENTRY_DEFINITIONS}")

    if(RA8P1_CAMERA_RTT_ONLY)
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Camera_thread_entry.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/LVGL_thread_entry.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/app/src/gui_app.c
            ${CMAKE_CURRENT_SOURCE_DIR}/src/app/src/camera_app.c
            PROPERTIES COMPILE_DEFINITIONS "${GUI_GUIDER_ENTRY_DEFINITIONS}")
    endif()

    message(STATUS "GUI Guider generated sources enabled: ${GUI_GUIDER_GENERATED_DIR}")
elseif(EXISTS ${GUI_GUIDER_ROOT}/RA8P1.guiguider)
    message(STATUS "GUI Guider project imported; generated C sources are not available yet")
endif()

# quirc is a compact pure-C QR decoder. Keep it outside ra_gen so RASC
# regeneration cannot overwrite this third-party dependency.
set(QUIRC_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc/quirc-master/lib/quirc.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc/quirc-master/lib/identify.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc/quirc-master/lib/decode.c
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/quirc/quirc-master/lib/version_db.c
)
target_sources(${PROJECT_NAME}.elf PRIVATE ${QUIRC_SOURCES})

# quirc's internal QUIRC_ASSERT (flood-fill invariants) maps to C assert().
# In Debug builds assert() is active, and FSP's __assert_func executes
# __BKPT(0) + while(1): any frame that trips an invariant (blur/occlusion/
# edge cases) hard-faults the whole system.  quirc is a well-tested upstream
# library; its asserts are debug aids, so build it with NDEBUG like the
# upstream release Makefile (-O3 without asserts is the default usage).
set_source_files_properties(${QUIRC_SOURCES}
    PROPERTIES COMPILE_DEFINITIONS "NDEBUG")

