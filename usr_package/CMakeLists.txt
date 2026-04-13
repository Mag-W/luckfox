# 要求 cmake的版本最少为3.10，项目名称为main，使用C语言
cmake_minimum_required(VERSION 3.10)
project(main C)

# ============================================
# Parse platform buildroot.config
# ============================================
# 查找buildroot_defconfig文件，路径为上级目录的config文件夹,并且不使用默认路径，如果找到则保存到BUILDROOT_CONFIG变量中
find_file(BUILDROOT_CONFIG buildroot_defconfig PATHS ${CMAKE_SOURCE_DIR}/../config NO_DEFAULT_PATH)

set(CONFIG_MODULE_LIST "")

if(BUILDROOT_CONFIG)
    file(READ "${BUILDROOT_CONFIG}" CONFIG_FILE)
    
    # 解析 CONFIG_MODULE_LIST， 假设格式为 CONFIG_MODULE_LIST="module_a module_b module_c"， 赋值给 CONFIG_MODULE_LIST 变量
    if(CONFIG_FILE MATCHES "CONFIG_MODULE_LIST=\"([^\"]+)\"")
        set(CONFIG_MODULE_LIST "${CMAKE_MATCH_1}")
        message(STATUS "✓ Module list: ${CONFIG_MODULE_LIST}")
    endif()
    
    # 解析其他配置,例如 CONFIG_DEBUG_MODE，这样源代码中就可以用 #ifdef CONFIG_DEBUG_MODE 来判断
    if(CONFIG_FILE MATCHES "CONFIG_DEBUG_MODE=y")
        add_compile_definitions(CONFIG_DEBUG_MODE)
        message(STATUS "✓ Debug mode enabled")
    endif()
    
    message(STATUS "✓ Loaded buildroot.config from: ${BUILDROOT_CONFIG}")
else()
    message(WARNING "buildroot.config not found")
endif()

# ============================================
# Compiler settings
# ============================================
# CMAKE_C_FLAGS：C 编译器标志， -Wall 开启所有警告， -Wextra 开启额外的警告， -fPIC 生成位置无关代码

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -fPIC")

if(CONFIG_DEBUG_MODE)
# 添加基础库调试标志 -g，关闭优化 -O0，这样更容易调试
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O0")
else()
# 发布版本优化标志 -O2，开启一些优化，但不影响调试信息的生成
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O2")
endif()

# ============================================
# Build lib
# ============================================
# 进入lib子仓库，执行其中的CMakeLists.txt，编译生成静态库libusr_lib.a
add_subdirectory(lib)

# ============================================
# Module sources - 按 CONFIG_MODULE_LIST 顺序添加
# ============================================
set(MODULE_SOURCES "")

string(REPLACE " " ";" MODULE_LIST "${CONFIG_MODULE_LIST}")

foreach(MOD ${MODULE_LIST})
    # 构造模块主文件路径：modules/module_name/module_name.c
    set(MOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/modules/${MOD}")
    set(MOD_MAIN_FILE "${MOD_DIR}/${MOD}.c")
    
    # 检查模块主文件是否存在,如果存在则添加到 MODULE_SOURCES 列表中，例如module_a/module_a.c， 否则报错。
    if(EXISTS ${MOD_MAIN_FILE})
        list(APPEND MODULE_SOURCES ${MOD_MAIN_FILE})
        message(STATUS "✓ Module '${MOD}' loaded: ${MOD_MAIN_FILE}")
    else()
        message(FATAL_ERROR "✗ Module '${MOD}' main file not found: ${MOD_MAIN_FILE}")
    endif()
endforeach()

# ============================================
# Main executable
# ============================================
# 告诉CMake要编译一个可执行文件，名字为main，源文件包括src/main.c, src/module_manager.c, 以及之前解析的模块源文件列表 MODULE_SOURCES
# 编译 src/main.c        → main.c.o
# 编译 module_a/module_a.c → module_a.c.o
# 编译 module_b/module_b.c → module_b.c.o  ↓
# 链接 (*.o + libs)  → main (可执行文件)

add_executable(main
    src/main.c
    ${MODULE_SOURCES}
)

# 包含头文件路径，首先是当前目录下的include文件夹，然后是lib/include，最后是modules文件夹，这样在源代码中就可以用 #include <module.h> 来包含模块的头文件
target_include_directories(main PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/modules
)

target_link_libraries(main
    usr_lib
    pthread
)

# 编译
#gcc -c -I/usr_package/lib/include -I/usr_package/modules \
#    src/main.c -o main.c.o
# gcc -c -I/usr_package/lib/include -I/usr_package/modules \
#    modules/module_a/module_a.c -o module_a.c.o
# gcc -c -I/usr_package/lib/include -I/usr_package/modules \
#    modules/module_b/module_b.c -o module_b.c.o

# 链接
# gcc main.c.o module_a.c.o module_b.c.o \
#    -L/usr_package/lib -lusr_lib -lpthread \
#    -o main

# ============================================
# Installation
# ============================================
install(TARGETS main DESTINATION bin)