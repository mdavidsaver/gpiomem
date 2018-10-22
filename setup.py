#!/usr/bin/env python

from distutils.core import setup, Extension
from Cython.Build import cythonize

setup(
  name = "gpiomem",
  version = "0.1",
  description = "Access for RPi/BCM GPIO registers",
  modules = ['gpiomem', 'gpiomem.test'],
  ext_modules = [
    Extension("gpiomem._lspi",
              sources = ['gpiomem/lspi.c']),
  ] + cythonize([
      Extension('gpiomem.mmio', ['gpiomem/mmio.pyx'], include_dirs=['gpiomem'],)
  ]),
)
