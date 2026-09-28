#include "zipsrc.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	zipsrc z;
	vfat v;
	uint8_t b[512];
	uint32_t lba, end;
	FILE *f;

	if (argc != 4) { fprintf(stderr, "usage: vfatimg romset.zip card|volume out.img\n"); return 2; }
	if (zipsrc_open(&z, argv[1], 64u << 20)) { fprintf(stderr, "vfatimg: cannot open %s\n", argv[1]); return 2; }
	if (vfat_init(&v, zipsrc_source(&z))) { fprintf(stderr, "vfatimg: vfat_init failed\n"); return 2; }
	f = fopen(argv[3], "wb");
	if (!f) { perror(argv[3]); return 2; }
	lba = argv[2][0] == 'v' ? v.part_start : 0;
	end = vfat_sectors(&v);
	for (; lba < end; lba++) {
		if (vfat_read(&v, lba, b)) { fprintf(stderr, "vfatimg: read error at lba %u\n", (unsigned)lba); return 1; }
		if (fwrite(b, 1, 512, f) != 512) { perror(argv[3]); return 1; }
	}
	fclose(f);
	printf("vfatimg: %d nodes, %d skipped, %u clusters used of %u, %u card sectors\n",
	       v.nnodes, v.skipped, (unsigned)v.used, (unsigned)v.clusters, (unsigned)vfat_sectors(&v));
	vfat_free(&v);
	zipsrc_close(&z);
	return 0;
}
