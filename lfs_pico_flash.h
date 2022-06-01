#ifndef __LFS_PICO_FLASH_H__
#define __LFS_PICO_FLASH_H__

#include "littlefs/lfs.h"

#include "hardware/sync.h"

struct lfs_flash_cfg {
	bool multicore;
	critical_section_t lock;
	uint32_t base;
};

int lfs_flash_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size);
int lfs_flash_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size);
int lfs_flash_erase(const struct lfs_config *cfg, lfs_block_t block);
int lfs_flash_sync(const struct lfs_config *cfg);

#endif /* __LFS_PICO_FLASH_H__ */
