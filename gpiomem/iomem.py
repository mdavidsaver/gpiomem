
from __future__ import print_function

import logging
import re

from .ext import MMIO

_log = logging.getLogger(__name__)

__all__ = [
    'GPIOMEM',
    'IN',
    'OUT',
    'ALT0',
    'ALT1',
    'ALT2',
    'ALT3',
    'ALT4',
    'ALT5',
]

IN  =0
OUT =1
ALT0=4
ALT1=5
ALT2=6
ALT3=7
ALT4=3
ALT5=2

Modes = {
    IN:'IN',
    OUT:'OUT',
    ALT0:'ALT0',
    ALT1:'ALT1',
    ALT2:'ALT2',
    ALT3:'ALT3',
    ALT4:'ALT4',
    ALT5:'ALT5',
}

RModes = dict([(V,K) for K,V in Modes.items()])

class GPIOMEM(MMIO):
    __slots__ = () # prevent accidental attribute creation

    def __init__(self, name=None):
        super(GPIOMEM, self).__init__(name or "/dev/gpiomem", 0x100)

    def show(self):
        pins = list(range(self.npins))

        for pin, mode in zip(pins, map(self.getalt, pins)):
            print("GPIO%u"%pin, Modes[mode])

    def configure(self, pairs):
        for pin, new in pairs:
            _log.info("Configure pin%u -> %s", pin, Modes[new])
            self.setalt(pin, new)
