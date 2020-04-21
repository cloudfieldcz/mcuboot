#ifndef H_SD_UPDATE_SD
#define H_SD_UPDATE_SD

#include <fs/fs.h>

#include "bootutil/image.h"

struct sd_update {
    struct image_header header;
    struct fs_file_t update_file;
};

int init_sd();

int check_sd_update(struct sd_update *update);

int validate_update_image(struct sd_update *update);

int backup_firmware();

int write_update(struct sd_update *update);

int revert_update();

int cleanup_update(struct sd_update *update, bool removeUpdate);

bool do_sd_update();

#endif
