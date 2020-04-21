#include <ctype.h>
#include <disk/disk_access.h>
#include <drivers/flash.h>
#include <errno.h>
#include <ff.h>
#include <stddef.h>

#include "bootutil/bootutil_log.h"
#include "bootutil/image.h"
#include "bootutil/sha256.h"
#include "flash_map_backend/flash_map_backend.h"
#include "sd_update.h"

MCUBOOT_LOG_MODULE_REGISTER(sd_update);

#define PATH_SEPARATOR "/"
#define UPDATE_DIRECTORY CONFIG_SD_UPDATE_MOUNT_POINT PATH_SEPARATOR CONFIG_SD_UPDATE_DIRECTORY_NAME
#define UPDATE_FILE UPDATE_DIRECTORY PATH_SEPARATOR CONFIG_SD_UPDATE_IMAGE_FILE_NAME
#define BACKUP_FILE UPDATE_DIRECTORY PATH_SEPARATOR CONFIG_SD_UPDATE_BACKUP_FILE_NAME

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
    .type = FS_FATFS,
    .fs_data = &fat_fs,
};

int init_sd() {
    static const char *disk_pdrv = "SD";
    int err = disk_access_init(disk_pdrv);

    if (err) {
        BOOT_LOG_ERR("Failed to initialize SD card (%d)", err);
        return err;
    }

    mp.mnt_point = CONFIG_SD_UPDATE_MOUNT_POINT;
    err = fs_mount(&mp);
    if (err == FR_OK) {
        BOOT_LOG_INF("SD Card mounted");
    } else {
        BOOT_LOG_ERR("Failed to mount SD card (%d)", err);
    }

    return err;
}

int strcasecmp(const char *s1, const char *s2)
{
	const unsigned char *us1 = (const unsigned char *)s1;
	const unsigned char *us2 = (const unsigned char *)s2;

	while (tolower(*us1) == tolower(*us2++))
		if (*us1++ == '\0')
			return 0;
	return tolower(*us1) - tolower(*--us2);
}

int check_sd_update(struct sd_update *update) {
    if (!update) {
        return -EINVAL;
    }

    int res;
    struct fs_dir_t dirp;
    static struct fs_dirent entry;
    bool has_update = false;

    res = fs_opendir(&dirp, UPDATE_DIRECTORY);
    if (res) {
        BOOT_LOG_ERR("Error opening dir %s [%d]\n", UPDATE_DIRECTORY, res);
        return res;
    }

    for (;;) {
        res = fs_readdir(&dirp, &entry);

        if (res || entry.name[0] == 0) {
            break;
        }

        if (entry.type == FS_DIR_ENTRY_FILE && strcasecmp(entry.name, CONFIG_SD_UPDATE_IMAGE_FILE_NAME) == 0) {
            has_update = true;
            break;
        }
    }

    fs_closedir(&dirp);

    if (!has_update) {
        BOOT_LOG_INF("No update file found on the SD card");
        update->update_file.filep = NULL;
        return -ENOENT;
    }

    res = fs_open(&update->update_file, UPDATE_FILE);
    if (res) {
        BOOT_LOG_ERR("Failed to open the update image (%d)", res);
        return res;
    }

    res = fs_read(&update->update_file, &update->header, sizeof(update->header));
    if (res < sizeof(&update->header)) {
        BOOT_LOG_ERR("Failed to read update header (%d)", res);
        fs_close(&update->update_file);
        return res;
    }

    if (update->header.ih_magic != IMAGE_MAGIC) {
        BOOT_LOG_ERR("The update does not contain a valid image");
        fs_close(&update->update_file);
        return res;
    }

    fs_seek(&update->update_file, 0, FS_SEEK_SET);

    return 0;
}

static int get_update_hash(struct sd_update *update, uint8_t *hash_result) {
    bootutil_sha256_context sha256_ctx;

    bootutil_sha256_init(&sha256_ctx);

    const size_t tmp_buf_sz = 256;
    uint8_t tmpbuf[tmp_buf_sz];

    int res;
    size_t remains = update->header.ih_hdr_size;
    remains += update->header.ih_img_size;
    remains += update->header.ih_protect_tlv_size;

    while (remains > 0) {
        res = fs_read(&update->update_file, tmpbuf, tmp_buf_sz < remains ? tmp_buf_sz : remains);
        if (res < 0) {
            return res;
        }
        bootutil_sha256_update(&sha256_ctx, tmpbuf, res);
        remains -= res;
    }

    bootutil_sha256_finish(&sha256_ctx, hash_result);

    return 0;
}

struct tlv_iterator {
    struct fs_file_t *update_file;
    off_t end;
    off_t offset;
};

static int tlv_iter_begin(struct sd_update *update, struct tlv_iterator *it) {
    off_t offset = update->header.ih_hdr_size + update->header.ih_img_size;
    struct image_tlv_info info;
    int res = fs_seek(&update->update_file, offset, FS_SEEK_SET);
    if (res) {
        return res;
    }
    res = fs_read(&update->update_file, &info, sizeof(info));
    if (res < sizeof(info)) {
        return -EIO;
    }

    if (info.it_magic == IMAGE_TLV_PROT_INFO_MAGIC) {
        if (update->header.ih_protect_tlv_size != info.it_tlv_tot) {
            return -1;
        }

        offset += info.it_tlv_tot;
        res = fs_seek(&update->update_file, offset, FS_SEEK_SET);
        if (res) {
            return res;
        }
        res = fs_read(&update->update_file, &info, sizeof(info));
        if (res < sizeof(info)) {
            return -EIO;
        }
    } else if (update->header.ih_protect_tlv_size != 0) {
        return -1;
    }

    if (info.it_magic != IMAGE_TLV_INFO_MAGIC) {
        return -1;
    }

    it->update_file = &update->update_file;
    it->offset = offset + sizeof(info);
    it->end = offset + info.it_tlv_tot;

    return 0;
}

static int tlv_iter_next(struct tlv_iterator *it, off_t *offset, uint16_t *type, uint16_t *len) {
    struct image_tlv tlv;

    if (it == NULL || it->update_file == NULL) {
        return -1;
    }

    while (it->offset < it->end) {
        int res = fs_seek(it->update_file, it->offset, FS_SEEK_SET);
        if (res) {
            return res;
        }
        res = fs_read(it->update_file, &tlv, sizeof(tlv));
        if (res < sizeof(tlv)) {
            return -EIO;
        }

        *type = tlv.it_type;
        *len = tlv.it_len;
        *offset = it->offset + sizeof(tlv);
        it->offset += sizeof(tlv) + tlv.it_len;
        return 0;
    }

    return 1;
}

int validate_update_image(struct sd_update *update)
{
    uint8_t hash[32];
    uint8_t buf[32];
    int res;

    BOOT_LOG_INF("Found update image, validating...");

    res = get_update_hash(update, hash);
    if (res) {
        BOOT_LOG_ERR("Failed to compute image hash (%d)", res);
        return res;
    }

    struct tlv_iterator it;
    res = tlv_iter_begin(update, &it);
    if (res) {
        BOOT_LOG_ERR("Failed to read update image TLVs (%d)", res);
        return res;
    }

    while (true) {
        off_t offset;
        uint16_t type;
        uint16_t len;
        res = tlv_iter_next(&it, &offset, &type, &len);
        if (res < 0) {
            BOOT_LOG_ERR("Failed to read update image TLV (%d)", res);
            return res;
        } else if (res > 0) {
            break;
        }
        if (type == IMAGE_TLV_SHA256) {
            if (len != sizeof(hash)) {
                BOOT_LOG_ERR("Incorrect update hash size");
                return -1;
            }
            res = fs_read(&update->update_file, buf, sizeof(hash));
            if (res < sizeof(hash)) {
                BOOT_LOG_ERR("Failed to read update hash (%d)", res);
                return res;
            }
            if (memcmp(hash, buf, sizeof(hash))) {
                BOOT_LOG_ERR("Incorrect update hash");
                return -1;
            }
            BOOT_LOG_INF("Update image is valid");
            return 0;
        }
    }

    BOOT_LOG_ERR("Failed to find update hash");
    return -1;
}

int backup_firmware() {
    const struct flash_area *fap;
    size_t buf_size = 256;
    uint8_t buf[buf_size];
    struct fs_file_t backup;
    int area_id;
    int res;

    BOOT_LOG_INF("Backing up current firmware...");

    fs_unlink(BACKUP_FILE);

    area_id = flash_area_id_from_image_slot(0);
    res = flash_area_open(area_id, &fap);
    if (res) {
        BOOT_LOG_ERR("Failed to open the primary slot (%d)", res);
        goto done2;
    }

    res = fs_open(&backup, BACKUP_FILE);
    if (res) {
        BOOT_LOG_ERR("Failed to create backup file on SD (%d)", res);
        goto done2;
    }

    size_t remains = fap->fa_size;
    off_t offset = 0;
    while (remains > 0) {
        size_t num_bytes = buf_size > remains ? remains : buf_size;
        res = flash_area_read(fap, offset, buf, num_bytes);
        if (res) {
            BOOT_LOG_ERR("Failed to read flash data (%d)", res);
            goto done;
        }
        res = fs_write(&backup, buf, num_bytes);
        if (res != num_bytes) {
            BOOT_LOG_ERR("Failed to write backup data (%d)", res);
            goto done;
        }

        remains -= num_bytes;
        offset += num_bytes;
    }
    res = 0;
    BOOT_LOG_INF("Backup complete");

done:
    fs_close(&backup);
done2:
    flash_area_close(fap);
    return res;
}

static int write_image(struct fs_file_t *file) {
    const struct flash_area *fap;
    size_t buf_size = 256;
    uint8_t buf[buf_size];
    int area_id;
    int res;

    BOOT_LOG_INF("Writing image to flash...");

    area_id = flash_area_id_from_image_slot(0);
    res = flash_area_open(area_id, &fap);
    if (res) {
        BOOT_LOG_ERR("Failed to open the primary slot (%d)", res);
        return res;
    }

    res = flash_area_erase(fap, 0, fap->fa_size);
    if (res) {
        BOOT_LOG_ERR("Failed to erase flash memory (%d)", res);
        goto done;
    }

    off_t offset = 0;
    size_t read = 0;
    do {
        read = fs_read(file, buf, buf_size);
        if (read < 0) {
            BOOT_LOG_ERR("Failed to read file data (%d)", res);
            goto done;
        }
        if (read < buf_size) {
            memset(buf + read, 0xFF, buf_size - read);
        }
        res = flash_area_write(fap, offset, buf, buf_size);
        if (res) {
            BOOT_LOG_ERR("Failed to write flash data (%d)", res);
            goto done;
        }

        offset += read;
    } while (read == buf_size);
    res = 0;
    BOOT_LOG_INF("Image written successfully");

done:
    flash_area_close(fap);
    return res;
}

int write_update(struct sd_update *update) {
    int res = fs_seek(&update->update_file, 0, FS_SEEK_SET);
    if (res) {
        BOOT_LOG_ERR("Failed to seek at the beggining of the update file (%d)", res);
        return res;
    }

    return write_image(&update->update_file);
}

int revert_update() {
    struct fs_file_t backup;
    int res = fs_open(&backup, BACKUP_FILE);
    if (res) {
        BOOT_LOG_ERR("Failed to open the backup file (%d)", res);
        return res;
    }

    res = write_image(&backup);
    fs_close(&backup);

    return res;
}

int cleanup_update(struct sd_update *update, bool removeUpdate) {
    if (update->update_file.filep != NULL) {
        fs_close(&update->update_file);
    }
    if (removeUpdate) {
        fs_unlink(UPDATE_FILE);
    }
    return fs_unmount(&mp);
}

bool do_sd_update() {
    BOOT_LOG_INF("Starting SD update...");
    int res;
    struct sd_update update;
    bool updated = false;

    res = init_sd();
    if (res) {
        return updated;
    }

    res = check_sd_update(&update);
    if (res) {
        goto cleanup;
    }

    res = validate_update_image(&update);
    if (res) {
        BOOT_LOG_ERR("Failed update image validation (%d)", res);
        goto cleanup;
    }

    res = backup_firmware();
    if (res) {
        BOOT_LOG_ERR("Could not backup current firmware, update won't continue. (%d)", res);
        goto cleanup;
    }

    res = write_update(&update);
    if (res) {
        BOOT_LOG_WRN("Failed to write update, attempting revert...");
        res = revert_update();
        if (res) {
            BOOT_LOG_ERR("Revert failed");
        } else {
            BOOT_LOG_INF("Revert successful, update has not been done");
        }
    } else {
        updated = true;
    }

cleanup:
    cleanup_update(&update, updated);
    BOOT_LOG_INF("SD update finished");
    return updated;
}
