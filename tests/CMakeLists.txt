cmake_minimum_required(VERSION 3.22)
project(stm_lvgl_port_test LANGUAGES C CXX)
enable_testing()
add_compile_options(-Wall -Wextra -Werror -UNDEBUG)
add_library(lvgl INTERFACE)
target_include_directories(lvgl INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(.. component)
# 只在测试构建替换 libc 分配符号；生产组件没有分配器选项。
target_compile_definitions(stm_lvgl_port PRIVATE calloc=test_calloc free=test_free)
add_executable(test_component test_lvgl.c test_allocator.c)
target_link_libraries(test_component PRIVATE stm_lvgl_port)
add_library(header_consumers OBJECT headers.c headers.cpp)
target_compile_features(header_consumers PRIVATE c_std_11 cxx_std_17)
target_link_libraries(header_consumers PRIVATE stm_lvgl_port)
add_test(NAME contracts COMMAND test_component)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_target_property(common_source stm_common SOURCE_DIR)
add_test(NAME dependency_resolution COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/test_dependency.py
    --common-source ${common_source}
    --c-compiler ${CMAKE_C_COMPILER} --cxx-compiler ${CMAKE_CXX_COMPILER})
