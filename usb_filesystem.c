#include <stdio.h>
#include <string.h>

#include "error_disk.h"
#include "fat_ramdisk.h"
#include "fatfs/ff.h"
#include "littlefs/lfs.h"
#include "usb.h"

#define FATFS_SECTOR_SIZE 512
#define FATFS_NUM_SECTORS 128

// Don't initialise because this is only used when USB connected
static uint8_t __attribute__ ((section ("noinit"))) fatfs_ramdisk_data[FATFS_SECTOR_SIZE * FATFS_NUM_SECTORS];
static const struct fat_ramdisk fat_ramdisk = {
	.label = "usedbadger",
	.sector_size = FATFS_SECTOR_SIZE,
	.num_sectors = FATFS_NUM_SECTORS,
	.data = fatfs_ramdisk_data,
};

int copy_file_flash_to_fat(lfs_t *lfs, lfs_file_t *lfp, FIL *ffp, uint32_t size)
{
	char buf[128];
	int res, nread, nwrote;

	while (size) {
		res = lfs_file_read(lfs, lfp, buf, sizeof(buf));
		printf("file_read: %d\n", res);
		if (res < 0) {
			return res;
		}
		nread = res;

		// Short read
		if (nread == 0) {
			res = -1;
			break;
		}

		res = f_write(ffp, buf, nread, &nwrote);
		printf("f_write: %d\n", res);
		if (res < 0) {
			return res;
		} else if (nwrote != nread) {
			return -1;
		}

		size -= nread;
	}

	return 0;
}

int copy_file_fat_to_flash(lfs_t *lfs, lfs_file_t *dst, FIL *src, uint32_t size)
{
	char buf[128];
	int res, nread;

	while (size) {
		res = f_read(src, buf, sizeof(buf), &nread);
		if (res) {
			return res;
		}

		// Short read
		if (nread == 0) {
			res = -1;
			break;
		}

		res = lfs_file_write(lfs, dst, buf, nread);
		if (res < 0) {
			return res;
		} else if (res != nread) {
			return -1;
		}

		size -= nread;
	}

	return 0;
}

static int compare_files(lfs_t *lfs, lfs_file_t *lfp, FIL *ffp, uint32_t size)
{
	char abuf[128];
	char bbuf[128];
	int res, aread, bread;

	while (size) {
		res = lfs_file_read(lfs, lfp, abuf, sizeof(abuf));
		printf("file_read: %d\n", res);
		if (res < 0) {
			return res;
		}
		aread = res;

		// Short read
		if (aread == 0) {
			return -1;
		}

		res = f_read(ffp, bbuf, sizeof(bbuf), &bread);
		if (res) {
			return -res;
		}

		if (aread != bread) {
			// Can't handle this currently, just assume they're different
			// (it could just be that one FS decides to chunk reads differently)
			return 1;
		}

		if (memcmp(abuf, bbuf, aread)) {
			return 1;
		}

		size -= aread;
	}

	return 0;
}

static int copy_flash_to_fat(lfs_t *lfs)
{
	lfs_dir_t dir;
	struct lfs_info dirent;
	FIL fp = { 0 };
	int res;

	res = lfs_dir_open(lfs, &dir, "");
	printf("dir_open: %d\n", res);
	if (res < 0) {
		return res;
	}

	int dir_res;
	while ((dir_res = lfs_dir_read(lfs, &dir, &dirent)) > 0) {
		char tmp_buf[128];

		if (dirent.type == LFS_TYPE_DIR) {
			continue;
		}

		lfs_file_t lfp;
		res = lfs_file_open(lfs, &lfp, dirent.name, LFS_O_RDONLY);
		printf("file_open: %s, %d\n", dirent.name, res);
		if (res < 0) {
			break;
		}

		res = f_open(&fp, dirent.name, FA_WRITE | FA_CREATE_ALWAYS);
		printf("f_open: %d\n", res);
		if (res) {
			lfs_file_close(lfs, &lfp);
			break;
		}

		res = copy_file_flash_to_fat(lfs, &lfp, &fp, dirent.size);
		printf("copy_file: %d\n", res);
		if (res) {
			lfs_file_close(lfs, &lfp);
			f_close(&fp);
			break;
		}

		res = lfs_file_close(lfs, &lfp);
		printf("file_close: %d\n", res);
		if (res < 0) {
			f_close(&fp);
			break;
		}

		res = f_close(&fp);
		printf("f_close: %d\n", res);
		if (res) {
			break;
		}
	}

	lfs_dir_close(lfs, &dir);

	printf("dir_res: %d\n", dir_res);
	if (dir_res != 0) {
		return -1;
	}

	printf("res: %d\n", res);
	if (res) {
		return -1;
	}

	return 0;
}

static int copy_fat_to_flash(lfs_t *lfs)
{
	DIR dir = { 0 };
	FILINFO dirent = { 0 };
	FIL fp = { 0 };
	int res;

	res = f_opendir(&dir, "");
	if (res != 0) {
		return res;
	}

	do {
		res = f_readdir(&dir, &dirent);
		if (res) {
			break;
		} else if (strlen(dirent.fname) == 0) {
			break;
		} else if (dirent.fattrib & AM_DIR) {
			continue;
		}

		res = f_open(&fp, dirent.fname, FA_READ | FA_OPEN_EXISTING);
		printf("f_open: '%s' s%d %d\n", dirent.fname, dirent.fsize, res);
		if (res) {
			break;
		}

		lfs_file_t lfp;

		// Check if the files are the same
		res = lfs_file_open(lfs, &lfp, dirent.fname, LFS_O_RDONLY);
		printf("file_open read: %s, %d\n", dirent.fname, res);
		if (res == 0) {
			res = compare_files(lfs, &lfp, &fp, dirent.fsize);

			// Always close the read-only flash version
			lfs_file_close(lfs, &lfp);

			if (res < 0) {
				// Error
				printf("compare_files: %d\n", res);
				f_close(&fp);
				break;
			} else if (res == 0) {
				// The same
				printf("files are the same\n");
				f_close(&fp);
				continue;
			} else {
				// Different
				res = f_rewind(&fp);
				if (res) {
					printf("f_rewind: %d\n", res);
					f_close(&fp);
					break;
				}
			}
		}

		res = lfs_file_open(lfs, &lfp, dirent.fname, LFS_O_CREAT | LFS_O_TRUNC | LFS_O_RDWR);
		printf("file_open: %s, %d\n", dirent.fname, res);
		if (res < 0) {
			f_close(&fp);
			break;
		}

		res = copy_file_fat_to_flash(lfs, &lfp, &fp, dirent.fsize);
		printf("copy_file: %d\n", res);
		if (res) {
			lfs_file_close(lfs, &lfp);
			f_close(&fp);
			break;
		}

		res = lfs_file_close(lfs, &lfp);
		printf("file_close: %d\n", res);
		if (res < 0) {
			f_close(&fp);
			break;
		}

		res = f_close(&fp);
		printf("f_close: %d\n", res);
		if (res) {
			break;
		}
	} while (1);

	f_closedir(&dir);

	printf("res: %d\n", res);
	if (res) {
		return -1;
	}

	return 0;
}

int do_flash_update(lfs_t *lfs)
{
	FATFS fat = { 0 };

	int res = f_mount(&fat, "", 1);
	if (res) {
		printf("failed to mount fat: %d", res);
		return res;
	}

	res = copy_fat_to_flash(lfs);
	if (res) {
		printf("failed to copy fat to flash: %d", res);
		goto err_unmount;
	}

	res = f_unmount("");
	if (res) {
		printf("failed to unmount fat: %d", res);
		return res;
	}

	return 0;

err_unmount:
	f_unmount("");
	return res;
}

void prepare_usb_filesystem(lfs_t *lfs, struct usb_msc_disk *msc_disk)
{
	int res;
	char error_buf[64];

	// Set-up the FAT singleton - this leaves the filesystem mounted
	res = fat_ramdisk_init(&fat_ramdisk);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "FatFS initialisation failed: %d", res);
		goto done;
	}

	res = copy_flash_to_fat(lfs);
	if (res) {
		snprintf(error_buf, sizeof(error_buf), "copy flash to MSC failed: %d", res);
	}

fatfs_unmount:
	f_unmount("");
done:
	if (res) {
		init_error_filesystem(&fat_ramdisk, error_buf);
	}
	msc_disk->block_size = fat_ramdisk.sector_size;
	msc_disk->num_blocks = fat_ramdisk.num_sectors;
	msc_disk->data = fat_ramdisk.data;
	msc_disk->read_only = (res != 0);
	return;
}

