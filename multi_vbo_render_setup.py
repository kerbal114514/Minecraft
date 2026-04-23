from setuptools import setup
import sys
from pybind11.setup_helpers import Pybind11Extension, build_ext

ext_modules = [
    Pybind11Extension(
        "multi_vbo_render",
        ["multi_vbo_render.cpp"],
        cxx_std=20,
        # 1. 添加头文件包含目录
        include_dirs=[],
        # 2. 添加库文件搜索目录
        library_dirs=[],
        # 3. 指定要链接的库名 (不需要加 lib 前缀和 .a/.lib 后缀)
        libraries=["GL", "GLEW"],
    ),
]

setup(
    name="multi_vbo_render",
    version="0.1",
    ext_modules=ext_modules,
    cmdclass={"build_ext": build_ext},
)
