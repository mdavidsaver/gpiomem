#!/usr/bin/env python

from distutils.core import setup, Extension
from Cython.Build import cythonize

setup(
  name = "gpiomem",
  version = "0.1",
  description = "Access for RPi/BCM GPIO registers",
  py_modules = ['gpiomem', 'gpiomem.test'],
  ext_modules = cythonize([
      Extension('gpiomem.ext', ['gpiomem/ext.pyx'], include_dirs=['gpiomem'],),
  ]),
)
