#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include "pico/stdlib.h"

#include "fatfs/ff.h"
#include "fatfs/diskio.h"
#include "fat_ramdisk.h"

static struct fat_ramdisk *disks;
static unsigned int num_disks;

void fat_ramdisk_init(struct fat_ramdisk *disk_data, unsigned int n_disks)
{
	disks = disk_data;
	num_disks = n_disks;
}

DSTATUS disk_initialize (BYTE pdrv)
{
	if (pdrv >= num_disks) {
		return STA_NOINIT;
	}
}

DSTATUS disk_status (BYTE pdrv)
{
	if (pdrv >= num_disks) {
		return STA_NODISK;
	}

	return 0;
}

DRESULT disk_read (BYTE pdrv, BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv >= num_disks) {
		return RES_PARERR;
	}

	struct fat_ramdisk *disk = &disks[pdrv];

	if (sector >= disk->num_blocks) {
		return RES_PARERR;
	}

	memcpy(buff, &disk->data[sector * disk->block_size], count * disk->block_size);

	return RES_OK;
}

DRESULT disk_write (BYTE pdrv, const BYTE* buff, LBA_t sector, UINT count)
{
	if (pdrv >= num_disks) {
		return RES_PARERR;
	}

	struct fat_ramdisk *disk = &disks[pdrv];

	if (sector >= disk->num_blocks) {
		return RES_PARERR;
	}

	memcpy(&disk->data[sector * disk->block_size], buff, count * disk->block_size);

	return RES_OK;
}

static DRESULT get_sector_count(struct fat_ramdisk *disk, LBA_t *out)
{
	*out = disk->num_blocks;

	return RES_OK;
}

static DRESULT get_sector_size(struct fat_ramdisk *disk, WORD *out)
{
	*out = disk->block_size;

	return RES_OK;
}

static DRESULT get_block_size(struct fat_ramdisk *disk, DWORD *out)
{
	*out = 1;

	return RES_OK;
}

DRESULT disk_ioctl (BYTE pdrv, BYTE cmd, void* buff)
{
	if (pdrv >= num_disks) {
		return RES_PARERR;
	}

	struct fat_ramdisk *disk = &disks[pdrv];

	switch (cmd) {
	case CTRL_SYNC:
		// Nothing to do
		return RES_OK;
	case GET_SECTOR_COUNT:
		return get_sector_count(disk, (LBA_t *)buff);
	case GET_SECTOR_SIZE:
		return get_sector_size(disk, (WORD *)buff);
	case GET_BLOCK_SIZE:
		return get_block_size(disk, (DWORD *)buff);
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
