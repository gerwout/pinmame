#include "vfat.h"
#include <stdio.h>
#include <string.h>

static int fails;
#define CHECK(c) do { if (!(c)) { printf("VFAT FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static const struct { const char *name; uint32_t size; } files[] = {
	{ "DOM_V006.PRG", 8 }, { "DMD/_DA/AA0.VID", 40000 }, { "DMD/_DA/aa1.vid", 512 }, { "DMD/_DZ/ZMS.spr", 1000 },
	{ "DMD/_DZ/EMPTY.FNT", 0 }, { "DMD/_DQ/", 0 }, { "SFX/_FA/A01.wav", 70000 },
	{ "SFX/_FA/TOOLONGNAME.wav", 10 }, { "SFX/_FA/two.dots.wav", 10 }, { "OTHER/X.BIN", 10 }
};

static const char *src_name(void *ctx, int i) { (void)ctx; return files[i].name; }
static uint32_t src_size(void *ctx, int i) { (void)ctx; return files[i].size; }
static int src_read(void *ctx, int i, uint32_t off, uint8_t *buf, uint32_t len)
{
	uint32_t k;
	(void)ctx;
	if (off + len > files[i].size) return -1;
	for (k = 0; k < len; k++) buf[k] = (uint8_t)(i * 31 + off + k);
	return 0;
}

static uint32_t le16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t le32(const uint8_t *p) { return le16(p) | le16(p + 2) << 16; }

static vfat v;

static uint32_t cl_lba(uint32_t cl) { return v.part_start + v.data_start + (cl - 2) * VFAT_SPC; }

static const uint8_t *entry(const uint8_t *dir, const char *name)
{
	int k;
	for (k = 0; k < 512 / 32; k++)
		if (!memcmp(dir + 32 * k, name, 11)) return dir + 32 * k;
	return NULL;
}

static uint32_t fat_entry(uint32_t cl)
{
	uint8_t b[512];
	vfat_read(&v, v.part_start + 32 + cl / 128, b);
	return le32(b + 4 * (cl % 128));
}

int main(void)
{
	vfat_source src;
	uint8_t b[512], root[512], dmd[512], dz[512], fa[512];
	const uint8_t *e;
	uint32_t cl;
	int k, ok;

	src.ctx = NULL;
	src.count = (int)(sizeof(files) / sizeof(files[0]));
	src.name = src_name;
	src.size = src_size;
	src.read = src_read;
	CHECK(vfat_init(&v, &src) == 0);
	CHECK(v.skipped == 2);
	CHECK(v.clusters >= 65525);

	CHECK(vfat_read(&v, 0, b) == 0);
	CHECK(b[446 + 4] == 0x0C && le32(b + 446 + 8) == 8192 && le32(b + 446 + 12) == v.vol_sectors && b[510] == 0x55 && b[511] == 0xAA);
	CHECK(vfat_read(&v, 8192, b) == 0);
	CHECK(le16(b + 11) == 512 && b[13] == 64 && le32(b + 32) == v.vol_sectors && le32(b + 36) == v.fat_sectors && le32(b + 44) == 2);
	CHECK(!memcmp(b + 82, "FAT32   ", 8) && b[510] == 0x55);
	CHECK(vfat_read(&v, 8193, b) == 0);
	CHECK(le32(b) == 0x41615252u && le32(b + 484) == 0x61417272u && le32(b + 488) == v.clusters - v.used);

	CHECK(vfat_read(&v, cl_lba(2), root) == 0);
	CHECK(root[11] == 0x08);
	CHECK(entry(root, "DMD        ") && entry(root, "SFX        "));
	CHECK(entry(root, "DOM_V006PRG") && !entry(root, "OTHER      "));
	e = entry(root, "DMD        ");
	CHECK(e && e[11] == 0x10);
	cl = le16(e + 26) | le16(e + 20) << 16;
	CHECK(vfat_read(&v, cl_lba(cl), dmd) == 0);
	CHECK(!memcmp(dmd, ".          ", 11) && !memcmp(dmd + 32, "..         ", 11) && le16(dmd + 32 + 26) == 0);
	CHECK(!memcmp(dmd + 64, "_DA        ", 11) && !memcmp(dmd + 96, "_DQ        ", 11) && !memcmp(dmd + 128, "_DZ        ", 11));

	e = entry(dmd, "_DZ        ");
	CHECK(e != NULL);
	CHECK(vfat_read(&v, cl_lba(le16(e + 26)), dz) == 0);
	e = entry(dz, "ZMS     SPR");
	CHECK(e && e[11] == 0x20 && le32(e + 28) == 1000);
	cl = e ? le16(e + 26) : 2;
	CHECK(vfat_read(&v, cl_lba(cl) + 1, b) == 0);
	for (k = 0, ok = 1; k < 512; k++) ok &= b[k] == (k < 488 ? (uint8_t)(3 * 31 + 512 + k) : 0);
	CHECK(ok);
	e = entry(dz, "EMPTY   FNT");
	CHECK(e && le16(e + 26) == 0 && le32(e + 28) == 0);

	e = entry(root, "SFX        ");
	CHECK(e && vfat_read(&v, cl_lba(le16(e + 26)), b) == 0);
	e = entry(b, "_FA        ");
	CHECK(e && vfat_read(&v, cl_lba(le16(e + 26)), fa) == 0);
	CHECK(!entry(fa, "TOOLONGNWAV"));
	e = entry(fa, "A01     WAV");
	CHECK(e && le32(e + 28) == 70000);
	cl = e ? le16(e + 26) : 2;
	CHECK(fat_entry(cl) == cl + 1 && fat_entry(cl + 1) == cl + 2 && fat_entry(cl + 2) == 0x0FFFFFFFu);
	CHECK(fat_entry(0) == 0x0FFFFFF8u && fat_entry(1) == 0x0FFFFFFFu && fat_entry(2 + v.used) == 0);

	CHECK(vfat_read(&v, vfat_sectors(&v), b) != 0);
	vfat_free(&v);
	printf("vfat: %s\n", fails ? "FAIL" : "ok");
	return fails != 0;
}
