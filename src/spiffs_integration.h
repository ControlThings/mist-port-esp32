/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

/* 
 * File:   spiffs_integration.h
 * Author: jan
 *
 * Created on January 6, 2017, 11:00 AM
 */

#ifndef SPIFFS_INTEGRATION_H
#define SPIFFS_INTEGRATION_H

#ifdef __cplusplus
extern "C" {
#endif

#include "wish_fs.h"
    
void esp32_spiffs_mount(void);

void esp32_spiffs_unmount(void);

void esp32_spiffs_reformat(void);

void esp32_spiffs_test(void);

/* Implementations of the actual I/O functions required by wish_fs
 * module */
wish_file_t my_fs_open(const char *pathname);
int32_t my_fs_read(wish_file_t fd, void* buf, size_t count);
int32_t my_fs_write(wish_file_t fd, const void *buf, size_t count);
wish_offset_t my_fs_lseek(wish_file_t fd, wish_offset_t offset, int whence);
int32_t my_fs_close(wish_file_t fd);
int32_t my_fs_rename(const char *oldpath, const char *newpath);
int32_t my_fs_remove(const char *path);

#ifdef __cplusplus
}
#endif

#endif /* SPIFFS_INTEGRATION_H */

