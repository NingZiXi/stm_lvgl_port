cmake_minimum_required(VERSION 3.22)
project(stm_lvgl_port C)
# 复用应用 LVGL 9；仅在缺少 target 时获取固定版本，配置仍由应用提供。
if(NOT TARGET lvgl)
    option(STM_LVGL_PORT_FETCH_LVGL "Download missing LVGL 9 dependency" ON)
    set(STM_LVGL_PORT_LVGL_SOURCE_DIR "" CACHE PATH "Offline LVGL 9 source directory")
    set(STM_LVGL_PORT_LVGL_GIT_REPOSITORY "https://github.com/lvgl/lvgl.git" CACHE STRING "LVGL repository or trusted mirror")

    if(NOT STM_LVGL_PORT_LVGL_SOURCE_DIR AND NOT FETCHCONTENT_SOURCE_DIR_LVGL AND NOT STM_LVGL_PORT_FETCH_LVGL)
        message(FATAL_ERROR "LVGL is missing. Provide an LVGL 9 target named lvgl, set STM_LVGL_PORT_LVGL_SOURCE_DIR/FETCHCONTENT_SOURCE_DIR_LVGL, or enable STM_LVGL_PORT_FETCH_LVGL.")
    endif()

    # 嵌入式默认不构建示例、演示或 ThorVG；尊重应用预先设置，不使用 FORCE。
    option(CONFIG_LV_BUILD_DEMOS "Build LVGL demos" OFF)
    option(CONFIG_LV_BUILD_EXAMPLES "Build LVGL examples" OFF)
    option(CONFIG_LV_USE_THORVG_INTERNAL "Build LVGL internal ThorVG" OFF)

    if(STM_LVGL_PORT_LVGL_SOURCE_DIR)
        if(NOT EXISTS "${STM_LVGL_PORT_LVGL_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR "Invalid STM_LVGL_PORT_LVGL_SOURCE_DIR: ${STM_LVGL_PORT_LVGL_SOURCE_DIR} (missing CMakeLists.txt)")
        endif()
        add_subdirectory("${STM_LVGL_PORT_LVGL_SOURCE_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/lvgl")
    else()
        include(FetchContent)
        FetchContent_Declare(lvgl
            GIT_REPOSITORY "${STM_LVGL_PORT_LVGL_GIT_REPOSITORY}"
            GIT_TAG c033a98afddd65aaafeebea625382a94020fe4a7 # v9.3.0
            GIT_SUBMODULES "")
        FetchContent_MakeAvailable(lvgl)
    endif()
endif()
if(NOT TARGET lvgl)
    message(FATAL_ERROR "LVGL dependency did not provide the lvgl target (LVGL 9 required)")
endif()
add_library(stm_lvgl_port STATIC src/stm_lvgl_port.c)
target_include_directories(stm_lvgl_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(stm_lvgl_port PUBLIC lvgl)
target_compile_features(stm_lvgl_port PUBLIC c_std_11)

# 优先复用应用 target 或同级源码，独立接入固定 stm_common v1.0.0。
if(NOT TARGET stm_common)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../stm_common/CMakeLists.txt")
        add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../stm_common" "${CMAKE_CURRENT_BINARY_DIR}/stm_common")
    else()
        option(STM_COMMON_FETCH "Download missing stm_common dependency" ON)
        set(STM_COMMON_GIT_REPOSITORY "https://github.com/NingZiXi/stm_common.git" CACHE STRING "stm_common repository or mirror")
        if(NOT STM_COMMON_FETCH)
            message(FATAL_ERROR "stm_common is missing. Provide its target or sibling directory, or enable STM_COMMON_FETCH.")
        endif()
        include(FetchContent)
        FetchContent_Declare(stm_common
            GIT_REPOSITORY "${STM_COMMON_GIT_REPOSITORY}"
            GIT_TAG ce3d186dde2d374a8e9c7b9068a7b88f97d57dc1)
        FetchContent_MakeAvailable(stm_common)
    endif()
endif()
if(NOT TARGET stm_common)
    message(FATAL_ERROR "stm_common dependency did not provide the stm_common target")
endif()
target_link_libraries(stm_lvgl_port PUBLIC stm_common)
