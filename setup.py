from setuptools import setup
import sys
from pybind11.setup_helpers import Pybind11Extension, build_ext

# 定义路径（请根据你的实际存放位置调整）
FASTNOISE2_INCLUDE = "FastNoise2/include"
FASTNOISE2_LIB_DIR = "FastNoise2/build/lib"
FASTSIMD_INCLUDE = "FastNoise2/build/_deps/fastsimd-src/include"

ext_modules = [
    Pybind11Extension(
        "MCworld",
        ["MCworld.cpp"],
        cxx_std=20,
        # 1. 添加头文件包含目录
        include_dirs=[FASTNOISE2_INCLUDE, FASTSIMD_INCLUDE],
        # 2. 添加库文件搜索目录
        library_dirs=[FASTNOISE2_LIB_DIR],
        # 3. 指定要链接的库名 (不需要加 lib 前缀和 .a/.lib 后缀)
        libraries=["FastNoise", "GL", "GLEW"],
        # 4. 静态链接宏定义
        define_macros=[('FASTNOISE_STATIC_LIB', None)],
    ),
]

setup(
    name="MCworld",
    version="0.1",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
