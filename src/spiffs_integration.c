/**
 * Copyright (C) 2020, ControlThings Oy Ab
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * @license Apache-2.0
 */
/* spiffs filesystem integration code for ESP32 */

/* See https://github.com/pellepl/spiffs/wiki/Integrate-spiffs for spiffs integraion instructions */

#include "spiffs.h"
#include "spiffs_nucleus.h"
#include "esp_spi_flash.h"
#include "esp_partition.h"
#include "wish_fs.h"

#include "port_log.h"

#define TAG "spiffs_integration"

#define SPIFFS_HAL_DEBUG(...) PORT_LOGINFO(TAG, __VA_ARGS__)

/* The SPI HAL layer functions */
static int32_t my_spi_read(uint32_t addr, uint32_t size, uint8_t *dst);
static int32_t my_spi_write(uint32_t addr, uint32_t size, uint8_t *src);
static int32_t my_spi_erase(uint32_t addr, uint32_t size);

static spiffs fs;
static spiffs_config cfg;

#define LOG_PAGE_SIZE       256
#define MAX_OPEN_FD    8    /* The maximum amount of files that can be open at the same time */

static u8_t spiffs_work_buf[LOG_PAGE_SIZE * 2];
static u8_t spiffs_fds[sizeof(spiffs_fd) * MAX_OPEN_FD];
static u8_t spiffs_cache_buf[(LOG_PAGE_SIZE + 32)*4];

/* Mount function for spiffs partition */

static esp_partition_t* get_spiffs_partition(void) {
    esp_partition_iterator_t partition_iter = esp_partition_find(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "spiffs");
    esp_partition_t *partition = (esp_partition_t *) esp_partition_get(partition_iter);
    if (partition == NULL) {
        PORT_LOGERR(TAG, "Parition named spiffs not found!");
        while(1);
    }
    return partition;
}

void esp32_spiffs_mount() {
    memset(&fs, 0, sizeof(fs));
    memset(&cfg, 0, sizeof(cfg));

    //cfg.phys_size = 64 * 1024;  /* Must conform with partition size defined in partitions.csv */
    //cfg.phys_addr = 0x110000;   /* start spiffs at start of spi flash. Must conform with partition start addr in partitions.csv */
    esp_partition_t* spiffs_part = get_spiffs_partition();
    cfg.phys_size = spiffs_part->size;
    cfg.phys_addr = spiffs_part->address;   /* start spiffs at start of spi flash. Must conform with partition start addr in partitions.csv */
    cfg.phys_erase_block = 4*1024; // according to datasheet
    cfg.log_block_size = 4*1024; // let us not complicate things
    cfg.log_page_size = LOG_PAGE_SIZE; // as we said

    cfg.hal_read_f = my_spi_read;
    cfg.hal_write_f = my_spi_write;
    cfg.hal_erase_f = my_spi_erase;

    int res = SPIFFS_mount(&fs,
            &cfg,
            spiffs_work_buf,
            spiffs_fds,
            sizeof (spiffs_fds),
            spiffs_cache_buf,
            sizeof (spiffs_cache_buf),
            0);
    PORT_LOGINFO(TAG, "spiffs mount res: %i, partition addr %i size %i\n", res, cfg.phys_addr, cfg.phys_size);
}

void esp32_spiffs_unmount() {
    SPIFFS_unmount(&fs);
}

void esp32_spiffs_reformat() {
    /* First, make sure that the fs structure is correct - see usage of SPIFFS_format() */
    esp32_spiffs_mount();
    esp32_spiffs_unmount();
    int res = SPIFFS_format(&fs);
    if (res != 0) {
        PORT_LOGERR(TAG, "SPIFFS_format error return %i", res);
    }
}


/* Test function for testing the spiffs file system integration */

#define TEST_STR "...pour s'adapter à un monde en mutation"
/* Have a '/' in front of the filename, just because mkspiffs likes to
 * add one when creating filesystems */
#define TEST_FILENAME "/my_file"

void esp32_spiffs_test(void) {
    uint8_t buf[strlen(TEST_STR)] __attribute__((aligned(4)));
    spiffs_file fd __attribute__((aligned(4))) = { 0 };
    int ret = 0;
#if 1   /* Set to zero to disable writing of the file first */
    memcpy(buf, TEST_STR, strlen(TEST_STR));

    // Surely, I've mounted spiffs before entering here

    fd = SPIFFS_open(&fs, TEST_FILENAME, SPIFFS_CREAT | SPIFFS_TRUNC | SPIFFS_RDWR, 0);
    if (fd < 0) {
        PORT_LOGINFO(TAG, "Bad fd (1) %d", SPIFFS_errno(&fs));
    }
    ret = SPIFFS_write(&fs, fd, buf, strlen(TEST_STR)); 
    if (ret < 0) {
        PORT_LOGINFO(TAG, "write errno %d", SPIFFS_errno(&fs));
    }
    PORT_LOGINFO(TAG, "write ret %d", ret);

    SPIFFS_close(&fs, fd); 
#endif
    fd = SPIFFS_open(&fs, TEST_FILENAME, SPIFFS_RDWR, 0);
    if (fd < 0) {
        PORT_LOGINFO(TAG, "Bad fd (1)");
    }

    memset(buf, 0, strlen(TEST_STR));
    ret = SPIFFS_lseek(&fs, fd, strlen(TEST_STR) - 12 , SPIFFS_SEEK_SET);
    //ret = SPIFFS_lseek(&fs, fd, 10 , SPIFFS_SEEK_SET);
    if (ret < 0) {
        PORT_LOGINFO(TAG, "seek errno %d", SPIFFS_errno(&fs));
    }
    PORT_LOGINFO(TAG, "seek ret %d", ret);
 
    ret = SPIFFS_read(&fs, fd, buf, strlen(TEST_STR));
    if (ret < 0) {
        PORT_LOGINFO(TAG, "read errno %d", SPIFFS_errno(&fs));
    }
    PORT_LOGINFO(TAG, "read ret %d", ret);
    SPIFFS_close(&fs, fd);

    PORT_LOGINFO(TAG, "--> %s <--", buf);
}   

/* These are the implementations of the wish_fs I/O operations */

wish_file_t my_fs_open(const char *pathname) {
    spiffs_file fd = 0;
    fd = SPIFFS_open(&fs, pathname, SPIFFS_CREAT |  SPIFFS_RDWR, 0);
    if (fd < 0) {
        SPIFFS_HAL_DEBUG("Could not open file %s: error %d\n\r", pathname, SPIFFS_errno(&fs));
    }
    return fd;
}

int32_t my_fs_read(wish_file_t fd, void* buf, size_t count) {
    int32_t ret = SPIFFS_read(&fs, fd, buf, count);
    if (ret < 0) {
        if (ret == SPIFFS_ERR_END_OF_OBJECT) {
            //SPIFFS_HAL_DEBUG("EOF encountered?\n\r");
            ret = 0;
        }
        else {
            SPIFFS_HAL_DEBUG("read errno %d\n\r", SPIFFS_errno(&fs));
        }
    }
    return ret;
}

int32_t my_fs_write(wish_file_t fd, const void *buf, size_t count) {
    int32_t ret = SPIFFS_write(&fs, fd, (void *)buf, count); 
    if (ret < 0) {
        SPIFFS_HAL_DEBUG("write errno %d\n", SPIFFS_errno(&fs));
    }
    return ret; 
}

wish_offset_t my_fs_lseek(wish_file_t fd, wish_offset_t offset, int whence) {
    int32_t ret = SPIFFS_lseek(&fs, fd, offset, whence);
    if (ret < 0) {
        SPIFFS_HAL_DEBUG("seek errno %d\n", SPIFFS_errno(&fs));
    }
    return ret;
}

int32_t my_fs_close(wish_file_t fd) {
    int32_t ret = SPIFFS_close(&fs, fd);
    return ret;
}

int32_t my_fs_rename(const char *oldpath, const char *newpath) {
    return SPIFFS_rename(&fs, oldpath, newpath);
}


int32_t my_fs_remove(const char *path) {
    return SPIFFS_remove(&fs, path);
}

/* Low-level SPI flash I/O functions */

/** This function will perform a SPI flash read on arbitrary address while observing the alignment restrictions set by ESP32 */
static int32_t my_spi_read(uint32_t addr, uint32_t size, uint8_t *dst) {
    uint32_t result = SPIFFS_OK;
    /* The address that is the next aligned one after addr */
    uint32_t aligned_begin = (addr + 3) & (~3);
    /* The address that is the next aligned one after addr + size */
    uint32_t aligned_end = (addr + size) & (~3);
    if (aligned_end < aligned_begin) {
        aligned_end = aligned_begin;
    }

    /* Read the odd bytes that are immediately before the next aligned
     * address after start addr */
    if (addr < aligned_begin) {
        uint32_t nb = aligned_begin - addr;
        uint32_t tmp;
        if (spi_flash_read(aligned_begin - 4, &tmp, 4) != ESP_OK) {
            SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
            return SPIFFS_ERR_INTERNAL;
        }
        memcpy(dst, &tmp + 4 - nb, nb);
    }

    /* Read the bytes which are between the correctly aligned start and
     * end addresses */
    if (aligned_end != aligned_begin) {
        if (spi_flash_read(aligned_begin, (uint32_t*) (dst + aligned_begin - addr),
                aligned_end - aligned_begin) != ESP_OK) {
            SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
            return SPIFFS_ERR_INTERNAL;
        }
    }

    /* Read the odd bytes tat are immediately after the aligned address
     * before 'addr + size' */
    if (addr + size > aligned_end) {
        uint32_t nb = addr + size - aligned_end;
        uint32_t tmp;
        if (spi_flash_read(aligned_end, &tmp, 4) != ESP_OK) {
            SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
            return SPIFFS_ERR_INTERNAL;
        }

        memcpy(dst + size - nb, &tmp, nb);
    }

    return result;
}

/* Don't set this smaller than 256 as it will corrupt things? */
static const int UNALIGNED_WRITE_BUFFER_SIZE = 256;

/** This function will perform a SPI flash write on arbitrary address while observing the alignment restrictions set by ESP32 */
static int32_t my_spi_write(uint32_t addr, uint32_t size, uint8_t *src) {

    uint32_t aligned_begin = (addr + 3) & (~3);
    uint32_t aligned_end = (addr + size) & (~3);
    if (aligned_end < aligned_begin) {
        aligned_end = aligned_begin;
    }

    if (addr < aligned_begin) {
        uint32_t ofs = aligned_begin - addr;
        uint32_t nb = (size < ofs) ? size : ofs;
        uint8_t tmp[4] __attribute__((aligned(4))) = {0xff, 0xff, 0xff, 0xff};
        memcpy(tmp + 4 - ofs, src, nb);
        if (spi_flash_write(aligned_begin - 4, (uint32_t*) tmp, 4) != ESP_OK) {
            SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
            return SPIFFS_ERR_INTERNAL;
        }
    }

    if (aligned_end != aligned_begin) {
        uint32_t* srcLeftover = (uint32_t*) (src + aligned_begin - addr);
        uint32_t srcAlign = ((uint32_t) srcLeftover) & 3;
        if (!srcAlign) {
            if (spi_flash_write(aligned_begin, (uint32_t*) srcLeftover,
                    aligned_end - aligned_begin) != ESP_OK) {
                SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
                return SPIFFS_ERR_INTERNAL;
            }
        }
        else {
            uint8_t buf[UNALIGNED_WRITE_BUFFER_SIZE];
            uint32_t sizeLeft = 0;
            for (sizeLeft = aligned_end - aligned_begin; sizeLeft; ) {
                size_t willCopy;
                if (sizeLeft < sizeof(buf)) {
                    willCopy = sizeLeft;
                }
                else {
                    willCopy = sizeof(buf);
                }
                memcpy(buf, srcLeftover, willCopy);

                if (spi_flash_write(aligned_begin, (uint32_t*) buf, willCopy) != ESP_OK) {
                    SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", 
                        __LINE__);
                    return SPIFFS_ERR_INTERNAL;
                }

                sizeLeft -= willCopy;
                srcLeftover += willCopy;
                aligned_begin += willCopy;
            }
        }
    }

    if (addr + size > aligned_end) {
        uint32_t nb = addr + size - aligned_end;
        uint32_t tmp = 0xffffffff;
        memcpy(&tmp, src + size - nb, nb);

        if (spi_flash_write(aligned_end, &tmp, 4) != ESP_OK) {
            SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
            return SPIFFS_ERR_INTERNAL;
        }
    }

    return SPIFFS_OK;
}

static int32_t my_spi_erase(uint32_t addr, uint32_t size) {
    if ((size & (SPI_FLASH_SEC_SIZE - 1)) != 0 ||
        (addr & (SPI_FLASH_SEC_SIZE - 1)) != 0) {
        SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
    }
    const uint32_t sector = addr / SPI_FLASH_SEC_SIZE;
    const uint32_t sector_count = size / SPI_FLASH_SEC_SIZE;
    uint32_t i = 0;
    for (i = 0; i < sector_count; ++i) {
        if (spi_flash_erase_sector(sector + i) != ESP_OK) {
            SPIFFS_HAL_DEBUG("Flash operation fail line %d\n\r", __LINE__);
            return SPIFFS_ERR_INTERNAL;
        }
    }
    return SPIFFS_OK;
}
