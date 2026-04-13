# Luckfox RV1106 交叉编译工具链配置
# 使用 Rockchip 提供的 uClibc 工具链

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

# 获取脚本所在目录（usr_package）
get_filename_component(TOOLCHAIN_DIR "${CMAKE_CURRENT_LIST_FILE}" DIRECTORY)

# 从 usr_package 向上两级到 luckfox-pico 根目录
get_filename_component(LUCKFOX_ROOT "${TOOLCHAIN_DIR}/.." ABSOLUTE)

# 工具链路径
set(TOOLCHAIN_PATH "${LUCKFOX_ROOT}/tools/linux/toolchain/arm-rockchip830-linux-uclibcgnueabihf/bin")

# 验证工具链是否存在
if(NOT EXISTS "${TOOLCHAIN_PATH}/arm-rockchip830-linux-uclibcgnueabihf-gcc")
    message(FATAL_ERROR "❌ 工具链不存在: ${TOOLCHAIN_PATH}")
endif()

message(STATUS "✓ 工具链路径: ${TOOLCHAIN_PATH}")

# 指定编译器
set(CMAKE_C_COMPILER ${TOOLCHAIN_PATH}/arm-rockchip830-linux-uclibcgnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PATH}/arm-rockchip830-linux-uclibcgnueabihf-g++)
set(CMAKE_AR ${TOOLCHAIN_PATH}/arm-rockchip830-linux-uclibcgnueabihf-ar)
set(CMAKE_RANLIB ${TOOLCHAIN_PATH}/arm-rockchip830-linux-uclibcgnueabihf-ranlib)

# 编译标志
set(CMAKE_C_FLAGS "-march=armv7-a -mfpu=neon -mfloat-abi=hard -O2")

# 跳过编译测试
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# 不搜索系统路径
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)