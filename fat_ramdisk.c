#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "fat_ramdisk.h"

static const struct fat_ramdisk *__disk;

FRESULT fat_ramdisk_init(const struct fat_ramdisk *disk)
{
	static FATFS fs = { 0 };
	MKFS_PARM opt = {
		.fmt = FM_FAT | FM_SFD,
		.n_fat = 0,
		.align = 0,
		.n_root = 0,
		.au_size = 0,
	};

	__disk = disk;
	memset(__disk->data, 0, __disk->num_sectors * __disk->sector_size);

	FRESULT res = f_mkfs("", &opt, NULL, 512);
	if (res) {
		return res;
	}

	res = f_mount(&fs, "", 1);
	if (res) {
		return res;
	}

	res = f_setlabel(__disk->label);
	if (res) {
		f_unmount("");
		return res;
	}

	return 0;
}

DSTATUS disk_initialize (BYTE pdrv)
{
	if (pdrv != 0) {
		return STA_NOINIT;
	}

	return 0;
}

DSTATUS disk_status (BYTE pdrv)
{
	if (pdrv != 0) {
		return STA_NODISK;
	}

	return 0;
}

DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv != 0) {
		return RES_PARERR;
	}

	if (!__disk) {
		return RES_NOTRDY;
	}

	if (sector >= __disk->num_sectors) {
		return RES_PARERR;
	}

	memcpy(buff, &__disk->data[sector * __disk->sector_size], count * __disk->sector_size);

	return RES_OK;
}

DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv != 0) {
		return RES_PARERR;
	}

	if (!__disk) {
		return RES_NOTRDY;
	}

	if (sector >= __disk->num_sectors) {
		return RES_PARERR;
	}

	memcpy(&__disk->data[sector * __disk->sector_size], buff, count * __disk->sector_size);

	return RES_OK;
}

static DRESULT get_sector_count(const struct fat_ramdisk *__disk, LBA_t *out)
{
	*out = __disk->num_sectors;

	return RES_OK;
}

static DRESULT get_sector_size(const struct fat_ramdisk *__disk, WORD *out)
{
	*out = __disk->sector_size;

	return RES_OK;
}

static DRESULT get_block_size(const struct fat_ramdisk *__disk, DWORD *out)
{
	*out = 1;

	return RES_OK;
}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff)
{
	if (pdrv != 0) {
		return RES_PARERR;
	}

	if (!__disk) {
		return RES_NOTRDY;
	}

	switch (cmd) {
	case CTRL_SYNC:
		// Nothing to do
		return RES_OK;
	case GET_SECTOR_COUNT:
		return get_sector_count(__disk, (LBA_t *)buff);
	case GET_SECTOR_SIZE:
		return get_sector_size(__disk, (WORD *)buff);
	case GET_BLOCK_SIZE:
		return get_block_size(__disk, (DWORD *)buff);
	default:
		return RES_ERROR;
	}
}

void* ff_memalloc (UINT msize)
{
	return malloc(msize);
}

void ff_memfree (void* mblock)
{
	free(mblock);
}
