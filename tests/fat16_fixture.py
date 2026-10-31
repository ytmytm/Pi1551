"""Small disposable FAT16 card for the bare-metal QEMU smoke test (8.3 names)."""
import struct


def write_card(path, root_files, drive_files):
    sector = 512
    fat_sectors = 128
    root_sector = 1 + 2 * fat_sectors
    data_sector = root_sector + 32
    cluster_bytes = 2 * sector
    image = bytearray(32 * 1024 * 1024)
    image[:3] = b"\xeb\x3c\x90"
    image[3:11] = b"MSDOS5.0"
    struct.pack_into("<HBHBHHBHHHII", image, 11,
                     sector, 2, 1, 2, 512, 0, 0xf8, fat_sectors, 32, 64, 0, 65536)
    image[38] = 0x29
    image[43:54] = b"PI1551 TEST"
    image[54:62] = b"FAT16   "
    image[510:512] = b"\x55\xaa"
    fat = bytearray(fat_sectors * sector)
    struct.pack_into("<HH", fat, 0, 0xfff8, 0xffff)
    next_cluster = 2

    def allocate(data):
        nonlocal next_cluster
        count = max(1, (len(data) + cluster_bytes - 1) // cluster_bytes)
        first = next_cluster
        next_cluster += count
        if next_cluster * 2 > len(fat):
            raise ValueError("FAT16 fixture is full")
        for c in range(first, next_cluster):
            struct.pack_into("<H", fat, c * 2, c + 1 if c + 1 < next_cluster else 0xffff)
        offset = data_sector * sector + (first - 2) * cluster_bytes
        image[offset:offset + len(data)] = data
        return first

    def entry(name, cluster, size, directory=False):
        record = bytearray(32)
        stem, _, extension = name.partition(".")
        if name in (".", ".."):
            stem, extension = name, ""
        if len(stem) > 8 or len(extension) > 3:
            raise ValueError("Fixture names must be 8.3: " + name)
        record[:11] = (stem.upper().ljust(8) + extension.upper().ljust(3)).encode("ascii")
        record[11] = 0x10 if directory else 0x20
        struct.pack_into("<HI", record, 26, cluster, size)
        return record

    drive_cluster = allocate(b"")
    drive_dir = bytearray(entry(".", drive_cluster, 0, True) + entry("..", 0, 0, True))
    for name, data in drive_files.items():
        drive_dir += entry(name, allocate(data), len(data))
    if len(drive_dir) + 32 > cluster_bytes:
        raise ValueError("Too many drive files for fixture directory")
    offset = data_sector * sector + (drive_cluster - 2) * cluster_bytes
    image[offset:offset + len(drive_dir)] = drive_dir
    root = bytearray(entry("1551", drive_cluster, 0, True))
    for name, data in root_files.items():
        root += entry(name, allocate(data), len(data))
    image[root_sector * sector:root_sector * sector + len(root)] = root
    for i in range(2):
        start = (1 + i * fat_sectors) * sector
        image[start:start + len(fat)] = fat
    path.write_bytes(image)
