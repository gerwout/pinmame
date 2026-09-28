# SD card storage tests

`./check.sh` builds and runs everything. Set `DOMINOS_ZIP` to a real romset zip to add it to the volume checks. Needs `zlib`, `fsck.vfat` (dosfstools), `mcopy` (mtools) and Python 3.

- `zipsrc_test.c` checks the romset zip reader against a zip written by Python's `zipfile` (`mkzip.py`): stored and deflated entries, directory entries, range errors, and the LRU cache, including an entry larger than the whole cache.
- `vfat_test.c` checks the synthesised FAT32 volume structure by structure: MBR, boot sector (32 KB clusters), FSInfo, directory entries, 8.3 names, exclusions, FAT chains and zero padding.
- `vfatimg.c` writes the volume to a file. `fsck.vfat -n` must accept it, and `mcopy` (an independent FAT reader) must extract every eligible zip entry byte-identical (`cmpzip.py`).
- `sd_test.c` drives the SD card model through a bit-banged SPI host: init handshake, SDHC and byte addressing, CSD, single and multi-block reads with CRC16, writes, error tokens, and chip-select asserted while SCLK is high.
