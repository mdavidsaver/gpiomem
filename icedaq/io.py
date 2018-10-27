import logging
_log = logging.getLogger(__name__)

import time
import struct

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

        # idle SPI1 and ensure not in reset
        self.io.output(SPI1_SS, 1)
        self.io.output(SPI1_SCLK, 1)
        self.io.output(CRST, 1)

        self.io.output(SPI0_SS, 1)

        self.io.configure([
            # leave SPI0 at defaults
            (SPI0_SCLK, ALT0),
            (SPI0_MOSI, ALT0),
            (SPI0_SS  , OUT),
            (SPI0_MISO, ALT0),
            (SPI1_SCLK, OUT),
            (SPI1_MOSI, OUT),
            (SPI1_SS  , OUT),
            (SPI1_MISO, IN),
            (CDONE    , IN),
            (CRST     , OUT),
            (GPCLK1   , ALT0),
            (GPCLK2   , ALT0),
        ])

        self.spi0 = SPI(0,0) # CE0
        self.spi0.mode = 3 # arbitrary
        self.spi0.speed=50000

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

    def ready(self):
        done, reset = map(self.io.input, [CDONE, CRST])
        _log.info("Ready done=%s reset=%s", done, reset)
        return reset and done

    def spi(self, port=0, data=None):
        if port==0:
            self.io.output(SPI0_SS, 0)
            with self.io.cleanup([(SPI0_SS, 1)]):
                return self.spi0.xfer(data)

        elif port==1:
            self.io.output(SPI1_SS, 0)
            with self.io.cleanup([(SPI1_SS, 1), (SPI1_SCLK, 1)]):
                return self.io.spi3(data, sclk=SPI1_SCLK, mosi=SPI1_MOSI, miso=SPI1_MISO)

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

        with self.io.cleanup([(SPI1_SS, 1), (SPI1_SCLK, 1), (CRST, 1)]):

            # ensure not in reset and select
            self.io.output(SPI1_SS, 0)
            self.io.output(SPI1_SCLK, 1)
            self.io.output(CRST, 1)
            time.sleep(0.001) # arbitrary to ensure CRST=1 is seen

            # reset
            self.io.output(CRST, 0)
            time.sleep(0.001) # must wait at least 200ns

            # clear reset
            self.io.output(CRST, 1)
            time.sleep(0.001) # must wait at least 800us

            if self.ready():
                raise RuntimeError("Failed to initiate reset")

            self.spi(port=1, data=bitstream)

            if not self.ready():
                raise RuntimeError("Failed to complete configuration")

    def port(self, port, cnt=None, data=None):
        assert port>0, port
        if data is None:
            data = b'\0'*cnt
        data=struct.pack('B', port)+data
        reply = self.spi(port=0, data=data)
        _log.debug("PORT%d %s -> %s", port, repr(data), repr(reply))
        return reply[1:]

def getargs():
    from argparse import ArgumentParser
    P = ArgumentParser()
    P.add_argument('bitfile')
    P.add_argument('--show', action='store_true', help='Show IO configuration')
    P.add_argument('-v','--verbose', action='store_const', const=logging.DEBUG, default=logging.INFO)
    return P.parse_args()

def main(args):
    I = ICEIO()
    if args.show:
        I.io.show()
        return
    I.load(args.bitfile)

if __name__=='__main__':
    args = getargs()
    logging.basicConfig(level=args.verbose)
    main(args)
