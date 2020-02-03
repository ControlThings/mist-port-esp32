/*
 * To change this license header, choose License Headers in Project Properties.
 * To change this template file, choose Tools | Templates
 * and open the template in the editor.
 */

#include <stdint.h>
#include <string.h>

#include "esp_system.h"

/* Wish & Mist includes */
#include "mist_app.h"
#include "mist_model.h"
#include "mist_model.h"
#include "mist_handler.h"
#include "bson.h"
#include "bson_visit.h"
/* Mist app includes */
#include "mist_config.h"
#include "wifi_utils.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"
#include "esp_ota_ops.h"
#include "mist_api.h"

#include "wish_identity.h"
#include "wish_connection.h"
#include "port_net.h"

#include "driver/uart.h"
#include "ota_update.h"

#include "mbedtls/sha256.h"

esp_ota_handle_t ota_handle;
esp_partition_t *partition;

static enum mist_error ota_invoke(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args);

static mist_ep ota_ep = { .id = "ota", .label ="Over the air update", .type = MIST_TYPE_INVOKE, .invoke = ota_invoke };

void ota_update_init(mist_model* model) {
    mist_ep_add(model, NULL, &ota_ep);
}

enum ota_state ota_state = OTA_INITIAL;

enum ota_state ota_update_get_state(void) {
    return ota_state;
}

int current_offset;
#define OTA_OP_MAX_LEN 16
#define HASH_LEN 32
#define BLOCK_SIZE 1024 /* This has to be contrasted with WISH_PORT_RX_RB_SZ */

mbedtls_sha256_context sha256_ctx;
uint8_t expected_hash[HASH_LEN];

static void ota_build_next(bson* bs, int next_offset, int wanted_size, uint8_t* hash, int image_size) {
    bson_append_string(bs, "op", "ota_next");
    bson_append_int(bs, "next_offset", next_offset);
    bson_append_int(bs, "block_size", image_size-next_offset>BLOCK_SIZE ? BLOCK_SIZE : image_size-next_offset);
    bson_append_binary(bs, "hash", hash, HASH_LEN);
}

static enum mist_error ota_invoke(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args) {
    static int curr_offset;
    static int expected_size;
    
    
    bson_visit("ota_invoke args:", bson_data(args));
    
    char *op = NULL;
    /* First, parse out the elements that we always use: op */
    bson_iterator it;
    bson_find(&it, args, "args");
    bson_iterator sit;
    bson_iterator_subiterator(&it, &sit);
    bson_find_fieldpath_value("op", &sit);
    if (bson_iterator_type(&sit) == BSON_STRING) {
        op = (char *) bson_iterator_string(&sit);
    } else {
        WISHDEBUG(LOG_CRITICAL, "element op is missing or not a string");
        return MIST_ERROR;
    }

    if (strncmp(op, "ota_reboot", OTA_OP_MAX_LEN) == 0) {
        WISHDEBUG(LOG_CRITICAL, "OTA rebooting!");
        esp_restart();
        /* Not reached! */
    }
     
    /** The response will be formulated to this BSON object */
    bson bs;
    bson_init(&bs);
    bson_append_start_object(&bs, "data");
    
ota_restart_parsing:    
    switch (ota_state) {
        case OTA_INITIAL:
            /* Expect this kind of packet: { op: "ota_begin", filename: "image.bin", size: <image-size>, hash: sha256(image) }*/
            if (strncmp(op, "ota_begin", OTA_OP_MAX_LEN) == 0) {
                
                bson_iterator_subiterator(&it, &sit);
                bson_find_fieldpath_value("size", &sit);
                if (bson_iterator_type(&sit) == BSON_INT) {
                    expected_size = bson_iterator_int(&sit);
                }
                else {
                    WISHDEBUG(LOG_CRITICAL, "ota_begin: Size expected!");
                    break;
                }
                
                /* Sub-iterator must be re-set in order to guarantee that the order in which we take out the elems do not depend on the order of elems in 'args' document! */
                bson_iterator_subiterator(&it, &sit);
                bson_find_fieldpath_value("hash", &sit);
                if (bson_iterator_type(&sit) == BSON_BINDATA) {
                    if (bson_iterator_bin_len(&sit) == HASH_LEN) {
                        memcpy(expected_hash, bson_iterator_bin_data(&sit), HASH_LEN);
                    }
                    else {
                        WISHDEBUG(LOG_CRITICAL, "Bad hash len");
                        bson_append_string(&bs, "op", "ota_err");
                        bson_append_string(&bs, "msg", "Bad hash len");
                        bson_append_int(&bs, "status", -1);
                        break;
                    }
                } else {
                    WISHDEBUG(LOG_CRITICAL, "element hash is missing or not a buffer");
                    bson_append_string(&bs, "op", "ota_err");
                    bson_append_string(&bs, "msg", "element hash is missing or not a buffer");
                    bson_append_int(&bs, "status", -1);
                    break;
                }
                
                WISHDEBUG(LOG_CRITICAL, "OTA begins, expected size: %i", expected_size);
                
                partition = (esp_partition_t*) esp_ota_get_next_update_partition(NULL);
                if (partition == NULL) {
                    WISHDEBUG(LOG_CRITICAL, "partition is null!");
                    bson_append_string(&bs, "op", "ota_err");
                    bson_append_string(&bs, "msg", "partition is null!");
                    bson_append_int(&bs, "status", -1);
                    break;
                }
                esp_err_t err = esp_ota_begin(partition,  OTA_SIZE_UNKNOWN, &ota_handle);
                if (err == ESP_OK) {
                    ota_build_next(&bs, curr_offset, BLOCK_SIZE, expected_hash, expected_size);
                    ota_state = OTA_ONGOING;
                }
                else {
                    WISHDEBUG(LOG_CRITICAL, "esp_ota_begin error: %i", err);
                    bson_append_string(&bs, "op", "ota_err");
                    bson_append_string(&bs, "msg", "esp_ota_begin() error");
                    bson_append_int(&bs, "status", -1);
                    break;
                }
                
                mbedtls_sha256_init(&sha256_ctx);
                mbedtls_sha256_starts(&sha256_ctx, 0); 
            }
            break;
        case OTA_ONGOING: {
            int data_size = 0;
            if (strncmp(op, "ota_push", OTA_OP_MAX_LEN) == 0) {
                bson_iterator_subiterator(&it, &sit);
                bson_find_fieldpath_value("offset", &sit);
                if (bson_iterator_type(&sit) == BSON_INT) {
                    int offset = bson_iterator_int(&sit);
                    if (curr_offset != offset) {
                        WISHDEBUG(LOG_CRITICAL, "Unexpected offset %i, expected %i", offset, curr_offset);
                        ota_build_next(&bs, curr_offset, BLOCK_SIZE, expected_hash, expected_size);
                        break;
                    }
                }
                bson_iterator_subiterator(&it, &sit);
                bson_find_fieldpath_value("data", &sit);
                if (bson_iterator_type(&sit) == BSON_BINDATA) {
                    data_size = bson_iterator_bin_len(&sit);
                    WISHDEBUG(LOG_CRITICAL, "OTA saving data, data size: %i", data_size);
                    /* Extract and save data here */
                    const char* data = bson_iterator_bin_data(&sit);
                    esp_err_t err = esp_ota_write(ota_handle, data, data_size);
                    if (err != ESP_OK) {
                        WISHDEBUG(LOG_CRITICAL, "esp_ota_write error: %i", err);
                        bson_append_string(&bs, "op", "ota_err");
                        bson_append_string(&bs, "msg", "esp_ota_write() err");
                        bson_append_int(&bs, "status", err);
                        break;
                    }
                    mbedtls_sha256_update(&sha256_ctx, (const unsigned char *) data, data_size); 
                }
                else {
                    WISHDEBUG(LOG_CRITICAL, "ota_next: data expected!");
                    ota_build_next(&bs, curr_offset, BLOCK_SIZE, expected_hash, expected_size);
                    break;
                }
                
                curr_offset+=data_size;
                if (curr_offset < expected_size) {
                    ota_build_next(&bs, curr_offset, BLOCK_SIZE, expected_hash, expected_size);
                }
                else {
                    /* The upload is finished! */
                    int status = 0;
                    WISHDEBUG(LOG_CRITICAL, "OTA is finished!");
                    
                    status = esp_ota_end(ota_handle);
                    if (status == ESP_OK) {
                        
                        /* Verify checksum! */
                        unsigned char calculated_hash[HASH_LEN];
                        mbedtls_sha256_finish(&sha256_ctx, calculated_hash);
                        mbedtls_sha256_free(&sha256_ctx);
                        
                        if (memcmp(calculated_hash, expected_hash, HASH_LEN) != 0) {
                            WISHDEBUG(LOG_CRITICAL, "Checksum mismatch");
                            bson_append_string(&bs, "op", "ota_finish_err");
                            bson_append_string(&bs, "msg", "checksum mismatch err");
                            status = -1;
                            bson_append_int(&bs, "status", status);
                        }
                        else {
                            WISHDEBUG(LOG_CRITICAL, "Checksum OK");
                            status = esp_ota_set_boot_partition(partition);
                            if (status == ESP_OK) {
                                bson_append_string(&bs, "op", "ota_finish");
                                bson_append_int(&bs, "status", status);
                            }
                            else {
                                bson_append_string(&bs, "op", "ota_finish_err");
                                bson_append_string(&bs, "msg", "esp_ota_set_boot_partition() err");
                                bson_append_int(&bs, "status", status);
                            }
                        }
                    }
                    else {
                        bson_append_string(&bs, "op", "ota_finish_err");
                        bson_append_string(&bs, "msg", "esp_ota_end() err");
                        bson_append_int(&bs, "status", status);
                    }
                    
                    
                    ota_state = OTA_FINISHED;
                }
                
            }
            else if (strncmp(op, "ota_begin", OTA_OP_MAX_LEN) == 0) {
                WISHDEBUG(LOG_CRITICAL, "OTA begin while in uploading state, reseting state");
                ota_state = OTA_INITIAL;
                curr_offset = 0;
                mbedtls_sha256_free(&sha256_ctx);
                goto ota_restart_parsing;
                break;
            }
            else if (strncmp(op, "ota_resync", OTA_OP_MAX_LEN) == 0) {
                WISHDEBUG(LOG_CRITICAL, "OTA resync, curr offset %i", curr_offset);
                ota_build_next(&bs, curr_offset, BLOCK_SIZE, expected_hash, expected_size);
            }
            break;
        }
        case OTA_FINISHED:
            /* Note: OTA reboot is possible in all states, it is handled as a special case */
            if (strncmp(op, "ota_begin", OTA_OP_MAX_LEN) == 0) {
                WISHDEBUG(LOG_CRITICAL, "OTA begin while in finished state, reseting state");
                ota_state = OTA_INITIAL;
                curr_offset = 0;
                mbedtls_sha256_free(&sha256_ctx);
                goto ota_restart_parsing;
                break;
            }
            break;
    }

    
    bson_append_finish_object(&bs);
    
    bson_finish(&bs);
    bson_visit("ota_invoke return", bson_data(&bs));
    if (bs.err) {
        WISHDEBUG(LOG_CRITICAL, "There was an BSON error");
        return MIST_ERROR;
    } else {
        /* Send away the control.invoke response */
        mist_invoke_response(ep->model->mist_app, ep->id, request_id, &bs);
    }
    bson_destroy(&bs);
    
    return MIST_NO_ERROR;
}