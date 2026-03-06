#!/bin/bash
# PCA9535 扩展 IO 模块构建脚本

set -e

cd "$(dirname "$0")"

echo "构建 PCA9535 扩展 IO 模块..."

# 创建 build 目录
mkdir -p build
cd build

# CMake 配置
cmake .. \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_TESTS=ON \
    -DBUILD_PYTHON_BINDINGS=ON

# 编译
make -j4

echo "构建完成！"
echo "静态库位置: $(pwd)/libdoly_pca9535.a"
if [ -f "pca9535*.so" ]; then
    echo "Python 绑定: $(pwd)/pca9535*.so"
fi
