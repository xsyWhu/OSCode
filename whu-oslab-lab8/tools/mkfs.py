#!/usr/bin/env python3
import argparse
import math
import os
import struct
from ctypes import Structure, c_uint16, c_uint32, sizeof, addressof, string_at

BSIZE = 1024
FSMAGIC = 0x4C415637  # matches include/fs/fs.h
BPB = BSIZE * 8
LOGSIZE = 30
NINODE = 200
DIRSIZ = 14
ROOTINO = 1
ITYPE_DIR = 1

class Dinode(Structure):
    _fields_ = [
        ("type", c_uint16),
        ("major", c_uint16),
        ("minor", c_uint16),
        ("nlink", c_uint16),
        ("size", c_uint32),
        ("addrs", c_uint32 * (11 + 1)),
        ("pad", c_uint32),
    ]

IPB = BSIZE // sizeof(Dinode)
assert IPB > 0 and BSIZE % sizeof(Dinode) == 0, "BSIZE must be multiple of dinode size"


def compute_layout(total_blocks: int):
    logstart = 2
    ninode_blocks = math.ceil(NINODE / IPB)
    inodestart = logstart + LOGSIZE
    bmapstart = inodestart + ninode_blocks

    data_start = bmapstart
    while True:
        data_blocks = total_blocks - data_start
        if data_blocks < 0:
            raise ValueError("Image too small for requested layout")
        bmap_blocks = max(1, math.ceil(data_blocks / BPB))
        new_data_start = bmapstart + bmap_blocks
        if new_data_start == data_start:
            data_start = new_data_start
            break
        data_start = new_data_start

    data_blocks = max(0, total_blocks - data_start)
    layout = {
        "logstart": logstart,
        "inodestart": inodestart,
        "bmapstart": bmapstart,
        "data_start": data_start,
        "nlog": LOGSIZE,
        "ninode": NINODE,
        "nblocks": data_blocks,
        "size": total_blocks,
        "bmap_blocks": bmap_blocks,
    }
    return layout


def write_block(f, blockno: int, data: bytes):
    if len(data) != BSIZE:
        raise ValueError("block data must be exactly BSIZE bytes")
    f.seek(blockno * BSIZE)
    f.write(data)


def zero_blocks(f, start: int, count: int):
    zero = bytes(BSIZE)
    for block in range(start, start + count):
        write_block(f, block, zero)


def write_superblock(f, layout):
    sb = struct.pack(
        "<8I",
        FSMAGIC,
        layout["size"],
        layout["nblocks"],
        layout["ninode"],
        layout["nlog"],
        layout["logstart"],
        layout["inodestart"],
        layout["bmapstart"],
    )
    block = bytearray(BSIZE)
    block[: len(sb)] = sb
    write_block(f, 1, block)

    print(
        f"[mkfs] size={layout['size']} blocks data={layout['nblocks']} "
        f"inode={layout['ninode']} log={layout['nlog']}"
    )


def create_root_inode(f, layout):
    direntry_struct = struct.Struct("<H14s")
    inode_blockno = layout["inodestart"] + (ROOTINO // IPB)
    offset = (ROOTINO % IPB) * sizeof(Dinode)
    inode_block = bytearray(BSIZE)

    root_inode = Dinode()
    root_inode.type = ITYPE_DIR
    root_inode.nlink = 2
    root_inode.size = 2 * direntry_struct.size
    root_inode.addrs[0] = layout["data_start"]

    inode_bytes = string_at(addressof(root_inode), sizeof(root_inode))
    inode_block[offset : offset + sizeof(root_inode)] = inode_bytes
    write_block(f, inode_blockno, inode_block)

    data_block = bytearray(BSIZE)
    direntry_struct.pack_into(data_block, 0, ROOTINO, b".")
    direntry_struct.pack_into(data_block, direntry_struct.size, ROOTINO, b"..")
    write_block(f, layout["data_start"], data_block)


def write_bitmap(f, layout):
    bitmap_size = layout["bmap_blocks"] * BSIZE
    bitmap = bytearray(bitmap_size)

    def set_bit(blockno: int):
        byte_index = blockno // 8
        bit_index = blockno % 8
        bitmap[byte_index] |= 1 << bit_index

    for blk in range(layout["data_start"]):
        set_bit(blk)
    set_bit(layout["data_start"])

    for idx in range(layout["bmap_blocks"]):
        start = idx * BSIZE
        end = start + BSIZE
        write_block(f, layout["bmapstart"] + idx, bytes(bitmap[start:end]))


def initialize_filesystem(f, layout):
    zero_blocks(f, layout["logstart"], layout["nlog"])
    zero_blocks(f, layout["inodestart"], layout["bmapstart"] - layout["inodestart"])
    zero_blocks(f, layout["bmapstart"], layout["bmap_blocks"])
    create_root_inode(f, layout)
    write_bitmap(f, layout)


def create_image(path: str, size_mb: int):
    total_bytes = size_mb * 1024 * 1024
    if total_bytes % BSIZE != 0:
        total_bytes = ((total_bytes // BSIZE) + 1) * BSIZE

    os.makedirs(os.path.dirname(path) or ".", exist_ok=True)
    with open(path, "wb") as f:
        f.truncate(total_bytes)

    blocks = total_bytes // BSIZE
    if blocks < 10:
        raise ValueError("Image too small")

    layout = compute_layout(blocks)
    with open(path, "r+b") as f:
        write_superblock(f, layout)
        initialize_filesystem(f, layout)


def main():
    parser = argparse.ArgumentParser(description="Create lab7 filesystem image")
    parser.add_argument("output", help="output image path")
    parser.add_argument("size_mb", type=int, help="image size in MB")
    args = parser.parse_args()
    create_image(args.output, args.size_mb)


if __name__ == "__main__":
    main()
