"""Read a completed guest file from Delugemu's FAT32 image without mounting it.

Read-only: never writes the live image or changes the guest filesystem.
"""
import struct


def read_sd_file(image, relative):
    with open(image, 'rb') as disk:
        boot = disk.read(512)
        base = 0
        if boot[82:90] != b'FAT32   ':
            base = struct.unpack_from('<I', boot, 454)[0] * 512
            disk.seek(base)
            boot = disk.read(512)
        assert boot[82:90] == b'FAT32   ', 'Expected FAT32'
        sector = struct.unpack_from('<H', boot, 11)[0]
        cluster_bytes = sector * boot[13]
        reserved = struct.unpack_from('<H', boot, 14)[0]
        fat_sectors = struct.unpack_from('<I', boot, 36)[0]
        fat = base + reserved * sector
        data = fat + boot[16] * fat_sectors * sector
        root = struct.unpack_from('<I', boot, 44)[0]

        def read_chain(cluster):
            chunks, seen = [], set()
            while 2 <= cluster < 0x0ffffff8:
                assert cluster not in seen, 'FAT cycle'
                seen.add(cluster)
                disk.seek(data + (cluster - 2) * cluster_bytes)
                chunks.append(disk.read(cluster_bytes))
                disk.seek(fat + cluster * 4)
                cluster = struct.unpack('<I', disk.read(4))[0] & 0x0fffffff
            return b''.join(chunks)

        directory = root
        parts = relative.strip('/').split('/')
        for index, part in enumerate(parts):
            records = read_chain(directory)
            long_name = []
            for offset in range(0, len(records), 32):
                entry = records[offset:offset+32]
                if entry[0] == 0:
                    break
                if entry[0] == 0xe5:
                    long_name = []
                    continue
                if entry[11] == 0x0f:
                    raw = entry[1:11] + entry[14:26] + entry[28:32]
                    long_name.insert(0, raw.decode('utf-16-le').rstrip('\x00\uffff'))
                    continue
                short = entry[:8].decode('ascii', errors='replace').rstrip()
                ext = entry[8:11].decode('ascii', errors='replace').rstrip()
                short += '.' + ext if ext else ''
                name = ''.join(long_name) or short
                long_name = []
                if part.casefold() not in (name.casefold(), short.casefold()):
                    continue
                directory = (struct.unpack_from('<H', entry, 20)[0] << 16) | struct.unpack_from('<H', entry, 26)[0]
                if index == len(parts) - 1:
                    return read_chain(directory)[:struct.unpack_from('<I', entry, 28)[0]]
                assert entry[11] & 0x10, 'Expected directory'
                break
            else:
                raise FileNotFoundError(relative)
        raise FileNotFoundError(relative)
