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
#include <stdint.h>
#include <string.h>

#include "mist_app.h"
#include "mist_model.h"
#include "mist_model.h"
#include "mist_handler.h"
#include "bson.h"
#include "bson_visit.h"
#include "mist_api.h"

#include "wish_fs.h"

#include "claim_core.h"


#define MIST_CONFIG_FILE "mist_config.bin"
#define MIST_CONFIG_FILE_MAX_SZ 128

static enum mist_error claim_core(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args);

static mist_ep claim_core_ep = { .id = "claimCore", .label ="Claim core", .type = MIST_TYPE_INVOKE, .invoke = claim_core };

static bool core_claimed = false;

static struct {
    uint8_t wsid[WISH_WSID_LEN];
    char name[WISH_APP_NAME_MAX_LEN];
} preferred_service;

bool claim_core_is_claimed(void) {
    return core_claimed;
}

static mist_app_t *mist_app;

static bool load_config_file(void) {
    wish_file_t fd = wish_fs_open(MIST_CONFIG_FILE);
    if (fd <= 0) {
        WISHDEBUG(LOG_CRITICAL, "Error opening file! Configuration could not be loaded! " MIST_CONFIG_FILE);
        return false;
    }
    
    wish_fs_lseek(fd, 0, WISH_FS_SEEK_SET);

    /* Size of the BSON structure in the file */
    int size = 0;
   
    /* Peek data size of BSON structure from file */
    int read_ret = wish_fs_read(fd, (void*) &size, 4);

    if (read_ret == 0 || (size <= 4 || size >= MIST_CONFIG_FILE_MAX_SZ)) {
        WISHDEBUG(LOG_CRITICAL, "Empty file, or file corrupted, creating " MIST_CONFIG_FILE);
        core_claimed = false;
        
        size_t buffer_len = MIST_CONFIG_FILE_MAX_SZ;
        uint8_t buffer[buffer_len];
        bson bs;
        bson_init_buffer(&bs, buffer, buffer_len);
        bson_append_bool(&bs, "core_claimed", core_claimed);
        bson_finish(&bs);
        
        wish_fs_lseek(fd, 0, WISH_FS_SEEK_SET);
        wish_fs_write(fd, bson_data(&bs), bson_size(&bs));
        wish_fs_close(fd);
        return true;
    } 
    
    bson bs;
    bson_init_size(&bs, size);
    
    /* Go back to start and read the whole file to bson buffer */
    wish_fs_lseek(fd, 0, WISH_FS_SEEK_SET);
    read_ret = wish_fs_read(fd, ((void*)bs.data), size);
    
    if (read_ret != size) {
        WISHDEBUG(LOG_CRITICAL, "Configuration failed to read %i bytes, got %i.", size, read_ret);
        return false;
    }
    
    wish_fs_close(fd);
    
    bson_iterator it;
    bson_find_from_buffer(&it, bs.data, "core_claimed");
    
    if (bson_iterator_type(&it) != BSON_EOO) {
        core_claimed = bson_iterator_bool(&it);
    }
    WISHDEBUG(LOG_CRITICAL, "Core claimed = %i", core_claimed);
    
    bson_destroy(&bs);
    
    return true;
}

static bool save_config_file(void) {
    wish_file_t fd = wish_fs_open(MIST_CONFIG_FILE);
    if (fd <= 0) {
        WISHDEBUG(LOG_CRITICAL, "Error opening file! Configuration could not be loaded! " MIST_CONFIG_FILE);
        return false;
    }

    size_t buffer_len = MIST_CONFIG_FILE_MAX_SZ;
    uint8_t buffer[buffer_len];
    bson bs;
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_bool(&bs, "core_claimed", core_claimed);
    bson_finish(&bs);

    wish_fs_lseek(fd, 0, WISH_FS_SEEK_SET);
    wish_fs_write(fd, bson_data(&bs), bson_size(&bs));
    wish_fs_close(fd);
    return true;
}

static void skip_connection_acl_cb(rpc_client_req* req, void* ctx, const uint8_t* payload, size_t payload_len) {
    bson_visit("skip_connection_acl_cb:", payload);
}

static void set_core_skip_connection_acl(bool skip) {
    bson b;
    size_t op_buf_len = 128;
    uint8_t op_buf[op_buf_len];
    bson_init_buffer(&b, op_buf, op_buf_len);
    
    bson_append_string(&b, "op", "host.skipConnectionAcl");
    bson_append_start_array(&b, "args");
    bson_append_bool(&b, "0", skip);
    bson_append_finish_array(&b);
    bson_append_int(&b, "id", 0);
    bson_finish(&b);
    
    wish_app_request(mist_app->app, &b, skip_connection_acl_cb, NULL);
}

void claim_core_init(mist_model* model) {
    mist_ep_add(model, NULL, &claim_core_ep);
    mist_app = model->mist_app;
    
    /* Load mist config's config file, and find the property "claimed". 
     * If there is no config file, create one with claimed: false.
     * 
     * If core is not claimed, then use wish core RPC to set the core to claimable state: host.skipConnectionACL(false)
     * The host can then be claimed via the "claimCore" invoke. When invoked, it sets permissions { core.owner: true } on the invoking contact (peer->ruid).
     * 
     * 
     *  */
    if (!load_config_file()) {
        WISHDEBUG(LOG_CRITICAL, "MistConfig file load error");
        return;
    }
}

void claim_core_post_init(void) {
    if (core_claimed == false) {
        set_core_skip_connection_acl(true);
    }
}

static void core_claim_permissions_cb(rpc_client_req* req, void* ctx, const uint8_t* payload, size_t payload_len) {
    bson_visit("in handle_manage_claim_cb", payload);
    
    /* Grab id of original invoke to claim_core */
    int invoke_id = *((int*) req->cb_context);
    if (req->cb_context != NULL) {
        wish_platform_free(req->cb_context);
    }
    WISHDEBUG(LOG_CRITICAL, "core_claim_permissions_cb, referred id %i", invoke_id);

    bson_iterator it;
    bson_find_from_buffer(&it, payload, "err");
     
    if (bson_iterator_type(&it) == BSON_INT) {
        // Message contains err: <id>; Error occured for identity.permission 
        bson_iterator_from_buffer(&it, payload);
        bson_find_fieldpath_value("data.code", &it);
        if (bson_iterator_type(&it) == BSON_INT) {
            int code = bson_iterator_int(&it);
            mist_invoke_error(mist_app, "claimCore", invoke_id, 1000+code, "Unexpected error from identity.permission");
        }
        else {
            WISHDEBUG(LOG_CRITICAL, "Malformed error, cb to identity.permissions");
            mist_invoke_error(mist_app, "claimCore", invoke_id, 1000, "Unexpected error from identity.permission, malformed error");
        }
        return;
    }

    bson bs;
    int buffer_len = 1024;
    uint8_t buffer[buffer_len]; 
    bson_init_buffer(&bs, buffer, buffer_len);
    bson_append_start_array(&bs, "data");
    
    /* Here, return a list of the services that this node exposes.
     * 
     * [
     * {rsid: [bytes...],
     * name: "esp32-241"
     * }
     * ]
     * 
     */
    bson_append_start_object(&bs, "0");
    bson_append_binary(&bs, "rsid", preferred_service.wsid, WISH_WSID_LEN);
    bson_append_string(&bs, "name", preferred_service.name);
    bson_append_finish_object(&bs);
    bson_append_finish_array(&bs);
    bson_finish(&bs);
    
    mist_invoke_response(mist_app, "claimCore", invoke_id, &bs);
    
    core_claimed = true;
    save_config_file();
    set_core_skip_connection_acl(false);
}


static enum mist_error claim_core(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args) {
    
    if (core_claimed) {
        /* Core is already claimed! */
        
        bson bs;
        int buffer_len = 1024;
        uint8_t buffer[buffer_len]; 
        bson_init_buffer(&bs, buffer, buffer_len);
        bson_append_start_array(&bs, "data");

        /* Here, return a list of the services that this node exposes.
         * 
         * [
         * {rsid: [],
         * name: "esp32-241"
         * }
         * ]
         * 
         */
        bson_append_start_object(&bs, "0");
        bson_append_binary(&bs, "rsid", preferred_service.wsid, WISH_WSID_LEN);
        bson_append_string(&bs, "name", preferred_service.name);
        bson_append_finish_object(&bs);
        bson_append_finish_array(&bs);
        bson_finish(&bs);

        mist_invoke_response(mist_app, "claimCore", request_id, &bs);
        
        return MIST_NO_ERROR;
    }
    
    /* Make identity.permissions to add { core.owner: true } */
    
    bson b;
    size_t op_buf_len = 128;
    uint8_t op_buf[op_buf_len];
    bson_init_buffer(&b, op_buf, op_buf_len);
    
    bson_append_string(&b, "op", "identity.permissions");
    bson_append_start_array(&b, "args");
    bson_append_binary(&b, "0", peer->ruid, WISH_ID_LEN);
    bson_append_start_object(&b, "1");
    bson_append_start_object(&b, "core");
    bson_append_bool(&b, "owner", true);
    bson_append_finish_object(&b);
    bson_append_finish_object(&b);
    bson_append_finish_array(&b);
    bson_append_int(&b, "id", 0);
    bson_finish(&b);
    
    WISHDEBUG(LOG_CRITICAL, "Performing identity.permissions, referred id %i", request_id);
    int *req_ptr = wish_platform_malloc(sizeof(int));
    if (req_ptr == NULL) {
        WISHDEBUG(LOG_CRITICAL, "Could not allocate memory for storing invoke id!");
        return MIST_MALLOC_FAIL;
    }
    *req_ptr = request_id;
    wish_app_request(mist_app->app, &b, core_claim_permissions_cb, req_ptr);
    
    return MIST_NO_ERROR;
}

void claim_core_set_preferred_service(uint8_t *wsid, char *service_name) {
    memcpy(preferred_service.wsid, wsid, WISH_WSID_LEN);
    strncpy(preferred_service.name, service_name, WISH_APP_NAME_MAX_LEN);
} 

char *claim_core_get_preferred_service_name(void) {
    return preferred_service.name;
}
