#ifndef __FAT_RAMDISK_H__
#define __FAT_RAMDISK_H__

#ifdef __cplusplus
 extern "C" {
#endif

struct fat_ramdisk {
	uint16_t block_size;
	uint16_t num_blocks;
	uint8_t *data;
};

void fat_ramdisk_init(struct fat_ramdisk *disk_data, unsigned int n_disks);

#ifdef __cplusplus
 }
#endif

#endif /* __FAT_RAMDISK_H__ */
