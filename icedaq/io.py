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

GCLK0=6  # GPCLK2
GCLK1=5  # GPCLK1

class ICEIO(object):
    def __init__(self):
        self.io = GPIOMEM()
        self.pins_std()
        self.spi0 = SPI(0,0) # CE0
        self.spi0.mode = 2 # arbitrary

        # iCE40 configuration needs
        #  CPOL=1  CPHA=0  aka Mode2
        self.spi1 = SPI(1,2) # CE2
        self.spi1.mode = 2

    def pins_std(self):
        IO = self.io

        IO.setalt([SPI0_SCLK, SPI0_MOSI, SPI0_MISO, SPI0_SS],
                [ALT0     , ALT0     , ALT0     , ALT0])

        IO.setalt([SPI1_SCLK, SPI1_MOSI, SPI1_MISO, SPI1_SS],
                [ALT4     , ALT4     , ALT4     , ALT4])

        IO.setalt([CDONE, CRST],
                [IN   , OUT])

        IO.setalt([GCLK0, GCLK1],
                [IN, IN]) #[ALT0, ALT0])

    def pins_manual(self):
        pins = [
            SPI0_SCLK, SPI0_MOSI, SPI0_MISO, SPI0_SS,
            SPI1_SCLK, SPI1_MOSI, SPI1_MISO, SPI1_SS,
            CRST,
        ]
        self.io.setalt(pins, [OUT]*len(pins))
        pins = [
            CDONE,
            GCLK0, GCLK1,
        ]
        self.io.setalt(pins, [OUT]*len(pins))

    def pins_off(self):
        pins = [
            SPI0_SCLK, SPI0_MOSI, SPI0_MISO, SPI0_SS,
            SPI1_SCLK, SPI1_MOSI, SPI1_MISO, SPI1_SS,
            CDONE, CRST,
            GCLK0, GCLK1,
        ]
        self.io.setalt(pins, [IN]*len(pins))

    def ready(self):
        done, reset = self.io.input([CDONE, CRST])
        _log.info("Ready done=%s reset=%s", done, reset)
        return reset and done

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

        ssmode, = self.io.getalt([SPI1_SS])
        _log.debug("SPI1_SS mode was %s", ssmode)
        try:
            # we take special control of SPI1 SS
            self.io.setalt([SPI1_SS], [OUT])

            # ensure not in reset and select
            self.io.output([CRST, SPI1_SS], [1, 0])
            time.sleep(0.001) # arbitrary to ensure CRST=1 is seen

            # reset
            self.io.output([CRST], [0])
            time.sleep(0.001) # must wait at least 200ns

            # clear reset
            self.io.output([CRST], [1])
            time.sleep(0.001) # must wait at least 800us

            if self.ready():
                raise RuntimeError("Failed to initiate reset")

            self.spi1.xfer(data=bitstream)

            if not self.ready():
                raise RuntimeError("Failed to complete configuration")

        finally:
            # ensure not in reset and de-select
            self.io.output([CRST, SPI1_SS], [1, 1])

            # restore automatic SS control
            self.io.setalt([SPI1_SS], [ssmode])

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
