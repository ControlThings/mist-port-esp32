/* C lib and other "standard" includes */
#include <stdint.h>
#include <string.h>

#include "esp_system.h"

/* Wish & Mist includes */
#include "mist_app.h"
#include "mist_model.h"
#include "mist_handler.h"
#include "bson.h"
#include "bson_visit.h"
/* Mist app includes */
#include "mist_config.h"
#include "wifi_utils.h"
#include "esp_wifi.h"
#include "esp_wifi_types.h"

#include "wish_identity.h"
#include "wish_connection.h"
#include "port_net.h"

#include "driver/uart.h"
 
#include "ota_update.h"
#include "claim_core.h"
#include "wifi_control.h"

#include "spiffs_integration.h"
//#include "modbus_app.h"
#include "utlist.h"

#ifdef MIST_PORT_WITH_CLOUD
#include "cloud.h"
#endif

#ifdef WITHOUT_MIST_CONFIG_APP
#pragma message "WITHOUT_MIST_CONFIG_APP defined, not building the mist config app."
#endif

#define MIST_APP_NAME "MistConfig"

static enum mist_error wifi_read(mist_ep* ep, wish_protocol_peer_t* peer, int request_id);
static enum mist_error wifi_invoke(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args);

static mist_app_t* mist_app;
static wish_app_t *app;

static mist_ep mist_super_ep = {.id = "mist", .label = "", .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep mist_name_ep = {.id = "name", .label = "Name", .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep mist_type_ep = {.id = "type", .label = "Type", .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep mist_class_ep = {.id = "class", .label = "Class", .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep mist_version_ep = {.id = "version", .label = "Version", .type = MIST_TYPE_STRING, .read = wifi_read};
static mist_ep mist_list_available_ep = { .id = "wifiListAvailable", .label = "wifiListAvailable", .type = MIST_TYPE_INVOKE, .read = NULL, .write = NULL, .invoke = wifi_invoke};
static mist_ep mist_commissioning_ep = { .id = "wifiCommissioning", .label = "wifiCommissioning", .type = MIST_TYPE_INVOKE, .read = NULL, .write = NULL, .invoke = wifi_invoke};
//static mist_ep mist_reset_commissioning_ep = { .id = "commissionReset", .label = "Commission reset", .type = MIST_TYPE_INVOKE, .read = NULL, .write = NULL, .invoke = wifi_invoke};

static mist_ep uptime_ep = {.id = "uptime", .label = "Uptime", .type = MIST_TYPE_INT, .read = wifi_read};
static mist_ep port_version_ep = { .id = "portVersion", .label = "Port version", .type = MIST_TYPE_STRING, .read = wifi_read };

static int uptime_minutes;

static enum mist_error wifi_read(mist_ep* ep, wish_protocol_peer_t* peer, int request_id) {

    char full_epid[MIST_EPID_LEN];
    mist_ep_full_epid(ep, full_epid);
    
    size_t result_max_len = 100;
    uint8_t result[result_max_len];
    bson bs;
    bson_init_buffer(&bs, result, result_max_len);
    
    if (ep == &mist_type_ep) {
        bson_append_string(&bs, "data", "WifiCommissioning");
    }
    else if (ep == &mist_version_ep) {
        bson_append_string(&bs, "data", "1.0.1");
    }
    else if (ep == &mist_super_ep) {
        bson_append_string(&bs, "data", "");
    }
    else if (ep == &mist_name_ep) {
        bson_append_string(&bs, "data", MIST_APP_NAME);
    }
    else if (ep == &mist_class_ep) {
        bson_append_string(&bs, "data", "fi.controlthings.mistconfig");
    }
    else if (ep == &uptime_ep) {
        bson_append_int(&bs, "data", uptime_minutes);
    }
    else if (ep == &port_version_ep) {
        bson_append_string(&bs, "data", PORT_VERSION);
    }
    
    bson_finish(&bs);
    mist_read_response(ep->model->mist_app, full_epid, request_id, &bs);
    
    return MIST_NO_ERROR;
}

/* The RPC id of an on-going wifi scan is saved here. Note: Only one peer may do a wifi scan at a time. To improve in this, there should be a list of requests instead. */
static int wifi_scan_rpc_id = 0;

void wifi_scan_complete_send_results(void) {
    bson bs;
    bson_init(&bs);
   
    bson_append_start_object(&bs, "data");

    wifi_ap_record_t* ap_records = get_ap_records();
    int num_found_ap = get_ap_records_num();

    WISHDEBUG(LOG_CRITICAL, "in wifi_scan_complete_send_results, num_found_ap %i", num_found_ap);
    int i = 0;
    for (i = 0; i < num_found_ap; i++) {
        /* FIXME terrible array index hack */
        const int arr_index_max_len = 10;
        char arr_index[arr_index_max_len];
        snprintf(arr_index, arr_index_max_len, "%i", i);
        //WISHDEBUG(LOG_CRITICAL, "encoding %s %d", (char*) ap_records[i].ssid, ap_records[i].rssi);
        bson_append_start_object(&bs, arr_index);
        bson_append_string(&bs, "ssid", (char*) ap_records[i].ssid);
        bson_append_int(&bs, "rssi", ap_records[i].rssi);
        bson_append_finish_object(&bs);
        if (bs.err) {
            WISHDEBUG(LOG_CRITICAL, "BSON error while adding ssid/rssi");
            return;
        }
    }
    //WISHDEBUG(LOG_CRITICAL, "finished appending in async scan resp");
    bson_append_finish_object(&bs);
    bson_finish(&bs);
    
    if (bs.err) {
        WISHDEBUG(LOG_CRITICAL, "BSON error while adding ssid/rssi");
        return;
    }
    
    mist_invoke_response(mist_app, mist_list_available_ep.id, wifi_scan_rpc_id, &bs);
    wifi_scan_rpc_id = 0;
    bson_destroy(&bs);
    //WISHDEBUG(LOG_CRITICAL, "Exiting wifi_scan_complete_cb");
}

/**
 * Handler for endpoints mist.wifiListAvaialble and mist.wifiCommissioning
 * 
 * To get the list of Wifi networks available, in MistCLI:
 * 
 * mist.control.invoke(peers[45], "mist.wifiListAvailable")
 * 
 * Endpoint mist.wifiListAvailable returns the wifi networks that are available:
 * 
 * { '0': { ssid: 'Buffalo-G-12DA', rssi: -43 },
 * '1': { ssid: 'filter-wld', rssi: -44 },
 * '2': { ssid: 'TW-EAV510-BR2BEA', rssi: -52 },
 * '3': { ssid: 'DIRECT-D3BEB626', rssi: -56 },
 * '4': { ssid: 'Debora wi-fi', rssi: -60 },
 * '5': { ssid: 'duoporv', rssi: -70 },
 * '6': { ssid: 'Temporent', rssi: -74 },
 * '7': { ssid: 'TW-WLAN-BR', rssi: -84 },
 * '8': { ssid: 'dlink', rssi: -87 } }
 * 
 * To commission the module to a certain wifi network, endpoint mist.wifiCommissioning expects as argument a BSON document:
 * { ssid: "SSID name", password: "network password", type: "station"|"access-point"}
 * 
 * mist.control.invoke(peers[45], "mist.wifiCommissioning", { ssid: "Buffalo-G-12DA", password: "19025995", type: "station"})
 *
 */
static enum mist_error wifi_invoke(mist_ep* ep, wish_protocol_peer_t* peer, int request_id, bson* args) {
    /* The stuff that comes in to this function in args.base has following structure: 
     
        epid: 'mistWifiCommissioning'           <---- added by handle_control_model on local side
        id: 8                                   <---- idem  
        args: {                                 <---- this is arguments from remote side
            ssid: 'Buffalo-G-12DA'
            wifi_Credentials: '19025995'
        }

     */
   
    char *ep_id = ep->id;

    WISHDEBUG(LOG_CRITICAL, "in wifi_invoke ep: %s %p", ep_id, args);
    
    //bson_visit("in wifi_invoke, BSON:", bson_data(args));

    //WISHDEBUG(LOG_DEBUG, "Control.invoke rpc %d", request_id);
    
    if (ep == &mist_list_available_ep) {
        
        if (wifi_control_get_mode() == WIFI_SETUP_MODE_UNCOMMISSIONED || wifi_control_get_mode() == WIFI_SETUP_MODE_STANDALONE_AP) {
            /* When the unit is in AP mode, in other words when it is uncommissioned or in standalone mode, the wifi scan cannot be done "on demand", 
             * but rather we must rely on the wifi scan results that we obtained when unit started. */
            wifi_scan_rpc_id = request_id;
            wifi_scan_complete_send_results();
            return MIST_NO_ERROR;
        }
        else {
            /* Now we know that we are in (or are configured to use) station mode. */
            if (wifi_scan_rpc_id != 0) {
                /* An other scanning is on-going. */
                mist_invoke_error(mist_app, ep->id, request_id, 100000, "There is already a wifi scan ongoing");

                return MIST_ERROR;
            }
            else {
                wifi_scan_rpc_id = request_id;
                wifi_control_ap_scan_start();
                /* The BSON reply will be formed once the wifi scan completes. */
                return MIST_NO_ERROR;
            }
        }
    }
    else if (ep == &mist_commissioning_ep) {   
        /* Note that invokes to mist.wifiCommissioning do not return any BSON results currently. */
        char* ssid = NULL;
        char* password = NULL;
        bson_iterator it;
        bson_find(&it, args, "args");
        bson_iterator sit;
        
        bson_iterator_subiterator(&it, &sit);
        bson_find_fieldpath_value("password", &sit);
        if (bson_iterator_type(&sit) == BSON_STRING) {
            password = (char *) bson_iterator_string(&sit);
        } else {
            WISHDEBUG(LOG_CRITICAL, "element password is not given.");
        }
         /* Sub-iterator must be re-set in order to guarantee that the order in which we take out the elems do not depend on the order of elems in 'args' document! */
        bson_iterator_subiterator(&it, &sit);
        bson_find_fieldpath_value("ssid", &sit);
        if (bson_iterator_type(&sit) == BSON_STRING) {
            ssid = (char *) bson_iterator_string(&sit);
        } else {
            WISHDEBUG(LOG_CRITICAL, "element ssid is missing or not a string");
            return MIST_ERROR;
        }
        
        bson_iterator_subiterator(&it, &sit);
        bson_find_fieldpath_value("type", &sit);
        if (bson_iterator_type(&sit) == BSON_STRING) {
            char *type = (char *) bson_iterator_string(&sit);
            if (strcmp(type, "station") == 0) {
                ESP_ERROR_CHECK( esp_wifi_stop() );
                user_set_station_config(ssid, password);
                WISHDEBUG(LOG_CRITICAL, "Will switch to SSID: %s password %s", ssid, password);
                wifi_control_save_mode(WIFI_SETUP_MODE_STATION_FIRST_TIME);
            }
            else if (strcmp(type, "access-point") == 0) {
                 user_set_ap_config(ssid, password);
                 wifi_control_save_mode(WIFI_SETUP_MODE_STANDALONE_AP);
            }
            else {
                WISHDEBUG(LOG_CRITICAL, "wifi configuration type %s not supported yet", type);
                return MIST_ERROR;
            }
        }
           
        esp32_spiffs_unmount();
        uart_driver_delete(UART_NUM_1);
        esp_restart();
        
        /* Not reached */
             
        /* FIXME Find a way to make connections work *without* needing a
         * reboot */

        /* Schedule a reboot via timer. This is because we would like to
         * give the TCP stack possibility to send the "ack" message of
         * this invoke function */

    }
    
    return MIST_NO_ERROR;
}

static void friend_request_accept_cb(rpc_client_req* req, void* ctx, const uint8_t* data, size_t data_len) {
    WISHDEBUG(LOG_CRITICAL, "Friend request accept cb, data_len = %i", data_len);
    bson_visit("Friend request accept cb", data);
}

static void friend_request_list_cb(rpc_client_req* req, void* ctx, const uint8_t* data, size_t data_len) {
    WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb, data_len = %i", data_len);
    bson_visit("friend_request_list_cb", data);
    
    uint8_t *luid = NULL; 
    uint8_t *ruid = NULL;
    
    bson_iterator it;
    BSON_ITERATOR_FROM_BUFFER(&it, data);
    /* Snatch the local uid who got this friend request */
    bson_find_fieldpath_value("data.0.luid", &it);
    if (bson_iterator_type(&it) == BSON_BINDATA) {
        luid = (uint8_t *) bson_iterator_bin_data(&it);
    }
    else {
        WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb, unexpected datatype");
        /* If the friend request list was empty, we return here. */
        return;
    }
    
    BSON_ITERATOR_FROM_BUFFER(&it, data);
    /* Snatch the remote uid who sent this friend request*/
    bson_find_fieldpath_value("data.0.ruid", &it);
    if (bson_iterator_type(&it) == BSON_BINDATA) {
        ruid = (uint8_t *) bson_iterator_bin_data(&it);
    }
    else {
        WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb, unexpected datatype");
        return;
    }
    
    bson bs;
    const size_t buf_sz = 500;
    uint8_t buf[buf_sz];
    bson_init_buffer(&bs, buf, buf_sz);
    /* Accept the friend request */
    bson_append_string(&bs, "op", "identity.friendRequestAccept");
    bson_append_start_array(&bs, "args");
    bson_append_binary(&bs, "0", luid, 32); //bson *b, const char *name, const char *str, int len
    bson_append_binary(&bs, "1", ruid, 32); //bson *b, const char *name, const char *str, int len
    bson_append_finish_array(&bs);
    bson_append_int(&bs, "id", 0);
    bson_finish(&bs);
    
    //bson_visit("accept req:", bson_data(&bs));
    wish_app_request(mist_app->app, &bs, friend_request_accept_cb, NULL);
}

#ifdef MIST_PORT_WITH_CLOUD
struct friend_req_entry {
    uint8_t ruid[WISH_UID_LEN];
    bool active;
    struct friend_req_entry* next;
};

struct friend_req_entry* friend_req_uids;

static void friend_request_list_cb_for_push_notification(rpc_client_req* req, void* ctx, const uint8_t* data, size_t data_len) {
    WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb_for_push_notification, data_len = %i", data_len);
    bson_visit("friend_request_list_cb_for_push_notification", data);
    
    char *alias = NULL;
    bson_iterator it;
    const int max_num_friend_reqs = 4;
    
    struct friend_req_entry* elt = NULL;
    struct friend_req_entry* tmp = NULL;
    
    /* First set active flags on all friend requests to false */
    LL_FOREACH(friend_req_uids, elt) {
        elt->active = false;
    }
    
    for (int i = 0; i < max_num_friend_reqs; i++) {
        BSON_ITERATOR_FROM_BUFFER(&it, data);
        
        char fieldpath_str[20];
        snprintf(fieldpath_str, 20, "data.%i.ruid", i);
        bson_find_fieldpath_value(fieldpath_str, &it);
        if (bson_iterator_type(&it) != BSON_BINDATA) {
            WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb_for_push_notification, unexpected datatype for ruid");
            break;
        }
        const uint8_t *friend_req_ruid = bson_iterator_bin_data(&it);
        /* Check if we have already sent a push notification for this ruid */
        
        bool new_friend_req = true;
        LL_FOREACH(friend_req_uids, elt) {
            if (memcmp(elt->ruid,  friend_req_ruid, WISH_UID_LEN) == 0) {
                /* We have already handled this friend req! */
                new_friend_req = false;
                elt->active = true;
            }
        }
        
        if (new_friend_req == false) {
            WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb_for_push_notification, We have already handled this friend req!");
            continue;
        }
        
        if (mist_cloud_get_active_state() != CLOUD_ONLINE) {
            WISHDEBUG(LOG_CRITICAL, "Cloud not online, not sending friend request push notificatoin!");
            continue;
        }
        
        struct friend_req_entry *new_elt = malloc(sizeof (struct friend_req_entry));
        
        if (new_elt == NULL) {
            WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb_for_push_notification, out of memory!");
            break;
        }
        
        memset(new_elt, 0, sizeof (struct friend_req_entry));
        new_elt->active = true;
        memcpy(new_elt->ruid, friend_req_ruid, WISH_UID_LEN);
        
        LL_APPEND(friend_req_uids, new_elt);
        
        
        snprintf(fieldpath_str, 20, "data.%i.alias", i);
        /* Snatch the local uid who got this friend request */
        BSON_ITERATOR_FROM_BUFFER(&it, data);
        bson_find_fieldpath_value(fieldpath_str, &it);
        if (bson_iterator_type(&it) == BSON_STRING) {
            alias = (char *) bson_iterator_string(&it);
            mist_cloud_send_notification(MIST_CLOUD_NOTIFICATION_FRIEND_REQUEST, alias, claim_core_get_preferred_service_name()); // { type: "friendRequest", name: "Mortimer", code: "" }
        }
        else if (bson_iterator_type(&it) == BSON_EOO) {
            WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb_for_push_notification, alias not found");
            break;
        }
        else {
            WISHDEBUG(LOG_CRITICAL, "friend_request_list_cb_for_push_notification, unexpected datatype for alias");
            break;
        }
    }
    
    LL_FOREACH_SAFE(friend_req_uids, elt, tmp) {
        if (elt->active == false) {
            LL_DELETE(friend_req_uids, elt);
            free(elt);
        }
    }
    
}
#endif //MIST_PORT_WITH_CLOUD    

static void make_friend_request_list(void) {
    bson bs;
    const size_t buf_sz = 100;
    uint8_t buf[buf_sz];
    bson_init_buffer(&bs, buf, buf_sz);

    bson_append_string(&bs, "op", "identity.friendRequestList");
    bson_append_start_array(&bs, "args");
    bson_append_finish_array(&bs);
    bson_append_int(&bs, "id", 0);
    bson_finish(&bs);

    wish_app_request(mist_app->app, &bs, friend_request_list_cb, NULL);
}

#ifdef MIST_PORT_WITH_CLOUD
static void make_friend_request_list_for_push_notification(void) {
    bson bs;
    const size_t buf_sz = 100;
    uint8_t buf[buf_sz];
    bson_init_buffer(&bs, buf, buf_sz);

    bson_append_string(&bs, "op", "identity.friendRequestList");
    bson_append_start_array(&bs, "args");
    bson_append_finish_array(&bs);
    bson_append_int(&bs, "id", 0);
    bson_finish(&bs);

    wish_app_request(mist_app->app, &bs, friend_request_list_cb_for_push_notification, NULL);
}
#endif //MIST_PORT_WITH_CLOUD

static void connections_list_cb(rpc_client_req* req, void* ctx, const uint8_t* data, size_t data_len) {
    bson_visit("connections_list_cb", data);
}

static void connections_list(void) {
    bson bs;
    const size_t buf_sz = 100;
    uint8_t buf[buf_sz];
    bson_init_buffer(&bs, buf, buf_sz);

    bson_append_string(&bs, "op", "connections.list");
    bson_append_start_array(&bs, "args");
    bson_append_finish_array(&bs);
    bson_append_int(&bs, "id", 0);
    bson_finish(&bs);

    wish_app_request(mist_app->app, &bs,  connections_list_cb, NULL);
}


static void signals_cb(rpc_client_req* req, void* ctx, const uint8_t* data, size_t data_len) {
    bson_visit("Got core signals:", data);
    
    bson_iterator it;
    BSON_ITERATOR_FROM_BUFFER(&it, data);
    
    bson_find_fieldpath_value("data.0", &it);
    if (bson_iterator_type(&it) == BSON_STRING) {
        if (strncmp(bson_iterator_string(&it), "friendRequest", 100) == 0) {
            WISHDEBUG(LOG_CRITICAL, "We got friend request");
            if (port_net_get_core()->config_skip_connection_acl) {
                make_friend_request_list(); 
            }
            else {
#ifdef MIST_PORT_WITH_CLOUD
                make_friend_request_list_for_push_notification();
#endif //MIST_PORT_WITH_CLOUD
            }
        }
        else if (strncmp(bson_iterator_string(&it), "connections", 100) == 0) {
            connections_list();
        }
    }
    else {
        WISHDEBUG(LOG_CRITICAL, "Unexpected datatype");
    }
}

static void host_setWldClass_cb(rpc_client_req* req, void* ctx, const uint8_t* data, size_t data_len) {
    
}

void mist_config_init(void) {
    //WISHDEBUG(LOG_CRITICAL, "entering config init");
    
    mist_app = start_mist_app();
    if (mist_app == NULL) {
        WISHDEBUG(LOG_CRITICAL, "Failed creating wish app");
        return;
    }

    app = wish_app_create(MIST_APP_NAME);

   if (app == NULL) {
        WISHDEBUG(LOG_CRITICAL, "Failed creating wish app");
        return; 
    }

    wish_app_add_protocol(app, &(mist_app->protocol));
    mist_app->app = app;

    mist_ep_add(&(mist_app->model), NULL, &mist_super_ep);
    /* FIXME should use parent's full path, just to illustrate */
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_name_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_class_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_type_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_version_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_list_available_ep);
    mist_ep_add(&(mist_app->model), mist_super_ep.id, &mist_commissioning_ep);
    mist_ep_add(&(mist_app->model), NULL, &uptime_ep);
    mist_ep_add(&(mist_app->model), NULL, &port_version_ep);

    ota_update_init(&(mist_app->model));

#ifndef MIST_CONFIG_DISABLE_CLAIM_CORE
    claim_core_init(&(mist_app->model));
#endif
        
    wish_app_connected(app, true);
   
#ifdef MIST_PORT_WITH_CLOUD
    mist_cloud_init(mist_app);
#endif
    
#ifndef MIST_CONFIG_DISABLE_CLAIM_CORE
    claim_core_post_init();
#endif
    
    /* Subscribe to MistApi signals */
    
    bson bs;
    const size_t buf_sz = 100;
    uint8_t buf[buf_sz];
    bson_init_buffer(&bs, buf, buf_sz);
    bson_append_string(&bs, "op", "signals");
    bson_append_start_array(&bs, "args");
    bson_append_finish_array(&bs);
    bson_append_int(&bs, "id", 0);
    bson_finish(&bs);

    wish_app_request(mist_app->app, &bs, signals_cb, NULL);
    
    bson_init_buffer(&bs, buf, buf_sz);
    bson_append_string(&bs, "op", "host.setWldClass");
    bson_append_start_array(&bs, "args");
    bson_append_string(&bs, "0", MIST_PORT_WLD_META_PRODUCT);
    bson_append_finish_array(&bs);
    bson_append_int(&bs, "id", 0);
    bson_finish(&bs);

    wish_app_request(mist_app->app, &bs, host_setWldClass_cb, NULL); 
}

void mist_config_periodic(void) {
#ifndef RELEASE_BUILD
    wish_platform_printf(".");
    fflush(stdout);
#endif
    wish_app_periodic(app);
    static int cnt = 0;
    cnt++;
    if (cnt >= 60) {
        cnt = 0;
        uptime_minutes++;
        mist_value_changed(uptime_ep.model->mist_app, uptime_ep.id); 
    }
}
