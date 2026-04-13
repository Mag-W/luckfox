#!/bin/bash

set -e

BUILD_DIR="build"
INSTALL_DIR="install"

echo "╔════════════════════════════════════════╗"
echo "║  Building for ARM (Luckfox RV1106)    ║"
echo "║  Toolchain: arm-rockchip830-linux     ║"
echo "╚════════════════════════════════════════╝"
echo ""

# 获取脚本所在目录
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"

TOOLCHAIN_FILE="toolchain-arm-rv1106.cmake"

# 检查工具链文件是否存在
if [ ! -f "$SCRIPT_DIR/$TOOLCHAIN_FILE" ]; then
    echo "❌ 错误：找不到工具链文件: $SCRIPT_DIR/$TOOLCHAIN_FILE"
    exit 1
fi

# 清理旧编译
rm -rf ${BUILD_DIR} ${INSTALL_DIR}
mkdir -p ${BUILD_DIR} ${INSTALL_DIR}

cd ${BUILD_DIR}

# CMake 配置（使用工具链）
echo "配置 CMake..."
cmake -DCMAKE_TOOLCHAIN_FILE=$SCRIPT_DIR/${TOOLCHAIN_FILE} \
      -DCMAKE_INSTALL_PREFIX=../${INSTALL_DIR} \
      ..

# 编译
echo ""
echo "开始编译..."
make -j$(nproc)

# 安装
echo ""
echo "安装..."
make install

cd ..

echo ""
echo "╔════════════════════════════════════════╗"
echo "║    Build Completed Successfully       ║"
echo "║  Binary: ${INSTALL_DIR}/bin/main      ║"
echo "╚════════════════════════════════════════╝"
echo ""

# 显示文件信息
echo "文件信息："
file ${INSTALL_DIR}/bin/main
echo ""