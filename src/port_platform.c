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
/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */


#include "wish_fs.h"
#include "wish_platform.h"
#include "wish_identity.h"
#include "wish_debug.h"
#include "wish_core.h"
#include "bson_visit.h"
#include "spiffs_integration.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "esp_system.h"
#include "esp_wifi.h"

#include "port_platform.h"

#include "mist_port.h"

#include "port_log.h"

#define TAG "port_platform"

static long esp_random_wrapper(void) {
    return (long) esp_random();
}

void port_platform_deps(void) {
    wish_platform_set_malloc(malloc);
    wish_platform_set_realloc(realloc);
    wish_platform_set_free(free);
    wish_platform_set_rng(esp_random_wrapper);
    wish_platform_set_vprintf(vprintf);
    wish_platform_set_vsprintf(vsprintf);

    wish_fs_set_open(my_fs_open);
    wish_fs_set_read(my_fs_read);
    wish_fs_set_write(my_fs_write);
    wish_fs_set_lseek(my_fs_lseek);
    wish_fs_set_close(my_fs_close);
    wish_fs_set_rename(my_fs_rename);
    wish_fs_set_remove(my_fs_remove);
}


void port_platform_load_ensure_identities(wish_core_t *core, char* default_alias) {
    
    wish_core_update_identities(core);
    if (core->num_ids == 0) {
        /* Create new identity */
        wish_identity_t id;
        
        PORT_LOGWARN(TAG, "Creating new identity, %s.", default_alias);
        wish_create_local_identity(core, &id, default_alias);
        wish_save_identity_entry(&id);
        wish_core_update_identities(core);
        if (core->num_ids == 1) {
            PORT_LOGINFO(TAG, "Created new identity, we are all set.");
        }
        else {
            PORT_LOGERR(TAG, "After attempting identity creating, unexpected number of identities: %i", core->num_ids);
        }
    }
}

void mist_port_wifi_join(mist_api_t* mist_api, const char* ssid, const char* password) {
    // Dummy
}