#ifndef __ERROR_DISK_H__
#define __ERROR_DISK_H__

#ifdef __cplusplus
 extern "C" {
#endif

#include "fat_ramdisk.h"

// Initialise 'disk' with a FAT filesystem, with a single file containing "error"
void init_error_filesystem(const struct fat_ramdisk *disk, const char *error);

#ifdef __cplusplus
 }
#endif

#endif /* __ERROR_DISK_H__ */
