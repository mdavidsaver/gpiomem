#!/usr/bin/env

from __future__ import print_function

import logging
import struct

from icedaq.io import ICEIO

_log = logging.getLogger(__name__)

class SPITEST(ICEIO):
    # port 1 - ID ROM
    # port 2 - ADC 1
    # port 3 - ADC 2
    # port 4 - DAC 1
    # port 5 - DAC 2

    def dac(self, port, value, pd=0):
        assert pd>=0 and pd<=3, pd
        assert value>=0 and value<2**12, value
        # -> {2'b00, pd[1:0], value[11:0], 2'b00}
        data = pd<<12
        data |= value
        data <<= 2
        _log.debug("DAC @%d %x,%d", port, value, pd)
        self.port(port, data=struct.pack('!H', data))

    def adc(self, port, chan=0):
        # -> {2'b00, channel[2:0], 3'b000, 8'h00}
        # <- {4'h0, value[11:0]}
        assert chan in (0, 1), chan
        data = chan<<11
        val, = struct.unpack('!H', self.port(port, data=struct.pack('!H', data)))
        _log.debug("ADC @%d.%d -> %x", port, chan, val)
        return val
        

def assertEqual(actual, expect):
    if actual!=expect:
        print("not ok %s == %s"%(actual, expect))
    else:
        print("ok %s == %s"%(actual, expect))


if __name__=='__main__':
    logging.basicConfig(level=logging.DEBUG)
    IO=SPITEST()

    # read ID ROM
    assertEqual(IO.port(1, 12), b'hello world!')

    for val in range(0, 2**12, 2**9):
        print("DAC %x"%val)

        IO.dac(4, val)
        IO.dac(5, val)

        print("ADC1 @0 %x"%IO.adc(2, chan=1))
        print("ADC2 @0 %x"%IO.adc(3, chan=1))
