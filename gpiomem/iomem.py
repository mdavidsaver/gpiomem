
from .mmio import MMIO

__all__ = [
    'GPIOMEM',
]

IN  =0
OUT =1
ALT0=4
ALT1=5
ALT2=6
ALT3=7
ALT4=3
ALT5=2

class GPIOMEM:
    def __init__(self, name=None):
        self._io = MMIO(name or "/dev/gpiomem", 0x100)
        self._io.width = 32

    @property
    def npins(self):
        return 54

    def getalt(self, pins):
        ret = []

        for pin in pins:
            if pin<0 or pin>=54:
                raise ValueError(pin)

            V = self._io[4*(pin/10)]
            V >>= (pin%10)*3
            ret.append(V&7)

        return ret

    def setalt(self, pins, vals):
        prog = []
        for pin, val in zip(pins, vals):
            off = 3*(pin%10)
            prog.append(
                (4*(pin/10), 32, 7<<off, val<<off)
            )

        self._io.io(prog)

    def output(self, pins, vals):
        set, clr = [0,0], [0,0]

        for pin, val in zip(pins, vals):
            N, P = divmod(pin, 32)
            P = 1<<P

            if val:
                set[N] |= P
            else:
                clr[N] |= P

        self._io.io([
            (0x1c, 32, set[0], set[0]),
            (0x20, 32, set[1], set[1]),
            (0x28, 32, clr[0], clr[0]),
            (0x2c, 32, clr[1], clr[1]),
        ])

    def input(self, pins):
        val = self._io.io([
            (0x34, 32, 0, 0),
            (0x38, 32, 0, 0),
        ])
        ret = []

        for pin in pins:
            N, P = divmod(pin, 32)
            P = 1<<P

            ret.append(True if val[N]&P else False)

        return ret
