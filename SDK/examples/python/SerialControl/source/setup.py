from setuptools import setup, Extension
from setuptools.command.build_ext import build_ext
import sys
import setuptools
import os

class CMakeExtension(Extension):
    def __init__(self, name, sourcedir=""):
        Extension.__init__(self, name, sources=[])
        self.sourcedir = os.path.abspath(sourcedir)

class CMakeBuild(build_ext):
    def run(self):
        for ext in self.extensions:
            self.build_extension(ext)

    def build_extension(self, ext):
        extdir = os.path.abspath(os.path.dirname(self.get_ext_fullpath(ext.name)))
        cmake_args = [
            f"-DCMAKE_LIBRARY_OUTPUT_DIRECTORY={extdir}",
            f"-DPYTHON_EXECUTABLE={sys.executable}"
        ]

        if not os.path.exists(self.build_temp):
            os.makedirs(self.build_temp)

        import subprocess
        subprocess.check_call(["cmake", ext.sourcedir] + cmake_args, cwd=self.build_temp)
        subprocess.check_call(["cmake", "--build", "."], cwd=self.build_temp)

setup(
    name="SerialControl",
    version="1.0.0",
    ext_modules=[CMakeExtension("SerialControl")],
    cmdclass={"build_ext": CMakeBuild},
    zip_safe=False,
)
