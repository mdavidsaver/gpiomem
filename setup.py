#!/usr/bin/env python

from distutils.core import setup, Extension

setup(
  name = "gpiomem",
  version = "0.1",
  description = "Access for RPi/BCM GPIO registers",
  ext_modules = [
    Extension("gpiomem._gpiomem",
              sources = ['gpiomem/gpiomem.c']),
    Extension("gpiomem._lspi",
              sources = ['gpiomem/lspi.c']),
  ]
)
