#ifndef __FAT_RAMDISK_H__
#define __FAT_RAMDISK_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include "fatfs/ff.h"

struct fat_ramdisk {
	const char *label;
	uint16_t sector_size;
	uint16_t num_sectors;
	uint8_t *data;
};

// Initialise and mount a FAT filesystem in disk
// Returns 0 on success
FRESULT fat_ramdisk_init(const struct fat_ramdisk *disk);

#ifdef __cplusplus
 }
#endif

#endif /* __FAT_RAMDISK_H__ */
