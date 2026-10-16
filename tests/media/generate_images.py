#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
"""Generate owned, deterministic image fixtures; no downloaded media."""
import argparse
import json
from pathlib import Path
import random
import struct
import zlib

def png(width, height, pixels, extra=(), depth=8):
    def chunk(name, data):
        return struct.pack('>I', len(data))+name+data+struct.pack('>I', zlib.crc32(name+data))
    stride = width*4*(depth//8)
    rows = b''.join(b'\0'+pixels[y*stride:(y+1)*stride] for y in range(height))
    return b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, depth, 6, 0, 0, 0))+b''.join(chunk(*item) for item in extra)+chunk(b'IDAT', zlib.compress(rows))+chunk(b'IEND', b'')

def generate(directory):
    directory.mkdir(parents=True, exist_ok=True)
    width, height = 257, 131
    pixels = bytes(value for y in range(height) for x in range(width) for value in (x*255//(width-1),y*255//(height-1),255 if (x//7+y//11)%2 else 0,y*255//(height-1)))
    first = png(width, height, pixels)
    second_pixels = bytes(255-value if index%4 == 0 else value for index,value in enumerate(pixels))
    fixtures = {'first.png':first,'second.png':png(width,height,second_pixels),
        'declared.png':png(width,height,pixels,[(b'sRGB',b'\0'),(b'gAMA',struct.pack('>I',45455))]),
        'linear.png':png(2,2,bytes([128,128,128,255])*4,[(b'gAMA',struct.pack('>I',100000))]),
        'unsupported-gamma.png':png(2,2,bytes([128,128,128,255])*4,[(b'gAMA',struct.pack('>I',50000))]),
        'sixteen.png':png(2,2,bytes([128,0,128,0,128,0,255,255])*4,depth=16),
        'wide.png':png(16385,1,bytes([0,0,0,255])*16385),
        'corrupt.png':first[:len(first)//2],
        'noise.png':png(512,256,random.Random(123).randbytes(512*256*4)),
        'éclairage_雪.png':first}
    import hashlib
    for name,data in fixtures.items(): (directory/name).write_bytes(data)
    # Long native path remains relative in authored state; the adapter supplies
    # the Win32 extended-path syntax privately.
    long = Path(*(['segment-'+('x'*38)]*7))/'image.png'
    (directory/long).parent.mkdir(parents=True,exist_ok=True);(directory/long).write_bytes(first)
    metadata = {name:{'sha256':hashlib.sha256(data).hexdigest(),'bytes':len(data)} for name,data in fixtures.items()}
    metadata['longPath'] = long.as_posix()
    (directory/'fixtures.json').write_text(json.dumps(metadata,indent=2)+'\n',encoding='utf-8')

if __name__ == '__main__':
    parser=argparse.ArgumentParser();parser.add_argument('directory',type=Path)
    generate(parser.parse_args().directory)
