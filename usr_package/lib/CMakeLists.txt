# ============================================
# Library CMakeLists.txt
# ============================================

add_library(usr_lib STATIC
    src/module_manager/module.c
    src/module_manager/module_manager.c
    src/log/log.c
    src/cJSON/cJSON.c
)

# ✅ 为库内部的源文件配置头文件搜索路径
target_include_directories(usr_lib PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cJSON # ✅ 添加这行，让库内部也能找到 cJSON 的头文件
)

# ✅ 为外部使用者（main、模块）配置公共头文件路径
target_include_directories(usr_lib PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/cJSON    # ✅ 添加这行，让外部可以使用 cJSON
)

target_link_libraries(usr_lib
    pthread
)