#include <string.h>

#include "hardware/flash.h"
#include "hardware/sync.h"
#include "pico/multicore.h"

#include "littlefs/lfs.h"

#include "lfs_pico_flash.h"

int lfs_flash_read(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, void *buffer, lfs_size_t size)
{
	struct lfs_flash_cfg *ctx = cfg->context;
	uint32_t flash_offs = ctx->base + (block * cfg->block_size) + off;

	uint8_t *src = (uint8_t *)(XIP_BASE + flash_offs);

	memcpy(buffer, src, size);

	return 0;
}

int lfs_flash_prog(const struct lfs_config *cfg, lfs_block_t block,
        lfs_off_t off, const void *buffer, lfs_size_t size)
{
	struct lfs_flash_cfg *ctx = cfg->context;

	if (ctx->multicore) {
		multicore_lockout_start_blocking();
	}

	critical_section_enter_blocking(&ctx->lock);

	uint32_t flash_offs = ctx->base + (block * cfg->block_size) + off;
	flash_range_program(flash_offs, buffer, size);

	critical_section_exit(&ctx->lock);

	if (ctx->multicore) {
		multicore_lockout_end_blocking();
	}

	return 0;
}

int lfs_flash_erase(const struct lfs_config *cfg, lfs_block_t block)
{
	struct lfs_flash_cfg *ctx = cfg->context;

	if (ctx->multicore) {
		multicore_lockout_start_blocking();
	}

	critical_section_enter_blocking(&ctx->lock);

	uint32_t flash_offs = ctx->base + (block * cfg->block_size);
	flash_range_erase(flash_offs, cfg->block_size);

	critical_section_exit(&ctx->lock);

	if (ctx->multicore) {
		multicore_lockout_end_blocking();
	}

	return 0;
}

int lfs_flash_sync(const struct lfs_config *cfg)
{
	return 0;
}
