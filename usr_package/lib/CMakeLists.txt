# ============================================
# Library CMakeLists.txt
# ============================================

add_library(usr_lib STATIC
    src/module_manager/module.c
    src/module_manager/module_manager.c
    src/log/log.c
    src/cJSON/cJSON.c
    src/mpu6500/mpu6500.c
)

# 设置 mbedtls 库的位置
set(MBEDTLS_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls")
set(MBEDTLS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/src/mbedtls/include")

# ✅ 为库内部的源文件配置头文件搜索路径
target_include_directories(usr_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cJSON # ✅ 添加这行，让库内部也能找到 cJSON 的头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mpu6500
    ${MBEDTLS_INCLUDE_DIR}    # ✅ 添加 mbedtls 头文件路径
)

# ✅ 为外部使用者（main、模块）配置公共头文件路径
target_include_directories(usr_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cJSON    # ✅ 添加这行，让外部可以使用 cJSON
    ${CMAKE_CURRENT_SOURCE_DIR}/src/mpu6500
    ${MBEDTLS_INCLUDE_DIR}    # ✅ 添加 mbedtls 头文件路径
)

target_link_libraries(usr_lib
    pthread
    ${MBEDTLS_LIB_DIR}/libmbedtls.a        # ✅ 链接 mbedtls 库
    ${MBEDTLS_LIB_DIR}/libmbedcrypto.a     # ✅ 链接加密库
    ${MBEDTLS_LIB_DIR}/libmbedx509.a      # ✅ 链接证书库
)