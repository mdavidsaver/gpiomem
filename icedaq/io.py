import logging
_log = logging.getLogger(__name__)

import time

from gpiomem import GPIOMEM, SPI, ALT0, ALT4, IN, OUT

# Main SPI
SPI0_SCLK=11 # 23
SPI0_MOSI=10 # 19
SPI0_MISO=9  # 21
SPI0_SS=8    # 24  CE0

# load/debug SPI
SPI1_SCLK=21
SPI1_MOSI=20
SPI1_MISO=19
SPI1_SS=16  # CE2

CDONE=24  # 0 - configuring, 1 ready
CRST=25   # 0 - in reset   , 1 normal

GPCLK0=4  # N/C
GPCLK1=5  # fpga pin 34
GPCLK2=6  # fpga pin 33

class ICEIO(object):
    def __init__(self):
        self.io = GPIOMEM()
        self.pins_std()
        self.spi0 = SPI(0,0) # CE0
        self.spi0.mode = 3 # arbitrary

        # iCE40 configuration needs
        # SCLK idle high
        # setup on 1->0, sample on 0->1
        #  CPOL=1  CPHA=1  aka Mode3
        #
        # as of Linux 4.4, SPI1 is messed up, (MOSI is shifted by one bit)
        # so we bit bang for now...
        # 
        #self.spi1 = SPI(1,2) # CE2
        #self.spi1.mode = 3

    def pins_std(self):
        IO = self.io

        # leave SPI0 at defaults (ALT0 for SCLK/MOSI/MISO, OUT for SS)
#        IO.setalt([SPI0_SCLK, SPI0_MOSI, SPI0_MISO, SPI0_SS],
#                  [OUT     , OUT     , IN     , OUT])
#                  [ALT0     , ALT0     , ALT0     , ALT0])

        IO.setalt([SPI1_SCLK, SPI1_MOSI, SPI1_MISO, SPI1_SS],
                [OUT     , OUT     , IN     , OUT])

        IO.setalt([CDONE, CRST],
                [IN   , OUT])

        IO.setalt([GPCLK0, GPCLK1, GPCLK2],
                [ALT0, ALT0, ALT0])

        IO.output([SPI0_SCLK], [1]) # idles high
        IO.output([SPI1_SCLK], [1]) # idles high

    def pins_manual(self):
        pins = [
#            SPI0_SCLK, SPI0_MOSI, SPI0_MISO, SPI0_SS,
            SPI1_SCLK, SPI1_MOSI, SPI1_MISO, SPI1_SS,
            CRST,
            GPCLK0, GPCLK1, GPCLK2,
        ]
        self.io.setalt(pins, [OUT]*len(pins))
        pins = [
            CDONE,
        ]
        self.io.setalt(pins, [IN]*len(pins))

    def pins_off(self):
        pins = [
#            SPI0_SCLK, SPI0_MOSI, SPI0_MISO, SPI0_SS,
            SPI1_SCLK, SPI1_MOSI, SPI1_MISO, SPI1_SS,
            CDONE, CRST,
            GPCLK0, GPCLK1, GPCLK2,
        ]
        self.io.setalt(pins, [IN]*len(pins))

    def ready(self):
        done, reset = self.io.input([CDONE, CRST])
        _log.info("Ready done=%s reset=%s", done, reset)
        return reset and done

    def _spi1(self, data):
        # assume SCLK=1
        ret = 0
        for V in data:
            for i in range(7,-1,-1):
                # 1 -> 0  setup
                self.io.output([SPI1_SCLK, SPI1_MOSI], [0, (ord(V)>>i)&1])
                # 0 -> 1 sample
                _S, B = self.io.output([SPI1_SCLK, SPI1_MISO], [1, None])
                ret = (ret<<1) | B
        # leave SCLK=1
        return ret

    def spi(self, port=0, data=None):
        if port==0:
            self.spi0.xfer(data=data)
        elif port==1:
            try:
                self.io.output([SPI1_SS], [0])
                ret = list(map(self._spi1, data))
            finally:
                self.io.output([SPI1_SS], [1])
            return ret
        else:
            raise ValueError("Unknown SPI%s"%port)

    def load(self, bitfile=None, bitstream=None):
        if bitstream is None:
            with open(bitfile, 'rb') as F:
                bitstream = F.read()

        # required to send 49 "dummy" bits before transition to user logic.
        # unfortunatly not a multiple of 8...
        # so we send 56 bits (7 bytes) and accept that user logic sees some junk.
        # we ensure to de-select before returning
        bitstream = bitstream+(b'\0'*7)

        _log.info("Load bitstream %s from %s", len(bitstream), bitfile)

        try:

            # ensure not in reset and select
            self.io.output([CRST, SPI1_SS, SPI1_SCLK], [1, 0, 1])
            time.sleep(0.001) # arbitrary to ensure CRST=1 is seen

            # reset
            self.io.output([CRST], [0])
            time.sleep(0.001) # must wait at least 200ns

            # clear reset
            self.io.output([CRST], [1])
            time.sleep(0.001) # must wait at least 800us

            if self.ready():
                raise RuntimeError("Failed to initiate reset")

            self._spi1(data=bitstream)

            if not self.ready():
                raise RuntimeError("Failed to complete configuration")

        finally:
            # ensure not in reset and de-select
            self.io.output([CRST, SPI1_SCLK, SPI1_SS], [1, 1, 1])

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('bitfile')
    return P.parse_args()

def main(args):
    I = ICEIO()
    I.load(args.bitfile)

if __name__=='__main__':
    args = getargs()
    logging.basicConfig(level=logging.DEBUG)
    main(args)
