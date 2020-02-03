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
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_event_loop.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "app_util.h"
#include "wifi_control.h"
#include "button_gpio.h"
#include "claim_core.h"
#include "ota_update.h"

#ifndef WITHOUT_MIST_CONFIG_APP
#include "mist_config.h"
#endif //WITHOUT_MIST_CONFIG_APP

#include "led_gpio.h"

#define TAG "wifi_control"

#ifndef MIST_PORT_COMMISSIONING_WIFI_SSID
#error MIST_PORT_COMMISSIONING_WIFI_SSID must be defined via CFLAGS, usually in main/Makefile.projbuild of the top-level esp32 app project
#endif

/* FreeRTOS event group to signal when we are connected & ready to make a request */
static EventGroupHandle_t wifi_event_group;

static enum wifi_control_state wifi_control_state;
enum wifi_control_state wifi_get_control_state(void) {
    return wifi_control_state;
}
static int ap_started_ts;
static int ap_num_stations_connected;
static char wifi_standalone_ssid_name[WIFI_SSID_MAX_LEN];

int wifi_ap_get_num_stations_connected(void) {
    return ap_num_stations_connected;
}
static int current_time;

/** Event group bit for "wifi connected" status. This is used for station mode, and is set when DHCP address is setup. */
static const int CONNECTED_BIT = BIT0;
/** Event group bit for "Wifi scan complete" status */
static const int WIFI_SCAN_COMPLETE_BIT = BIT1;

static void wifi_poll_ap_list_updated(void);

/** Wifi event handler. 
 * @note Please note that this event handler function is run in the context of the event handler thread! */
esp_err_t event_handler(void *ctx, system_event_t *event)
{
    switch(event->event_id) {
    case SYSTEM_EVENT_STA_START:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_START");
        esp_wifi_connect();
        if (wifi_control_state == WIFI_CONTROL_STA_WAIT_CONNECT) { /* Note: this occurs also when starting commissioning AP, so that is why we need to filter out */
            led_gpio_set_state(BLINK_JOINING);
        }
        break;
    case SYSTEM_EVENT_STA_GOT_IP:
        ESP_LOGI(TAG, "Got IP");
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        led_gpio_set_state(BLINK_NETWORK_OK);
        wifi_control_state = WIFI_CONTROL_STA_CONNECTED;
        if (wifi_control_get_mode() != WIFI_SETUP_MODE_STATION_CONFIRMED) {
            wifi_control_save_mode(WIFI_SETUP_MODE_STATION_CONFIRMED);
        }
        break;
    case SYSTEM_EVENT_STA_DISCONNECTED:
        ESP_LOGI(TAG, "SYSTEM_EVENT_STA_DISCONNECTED");
        /* This is a workaround as ESP32 WiFi libs don't currently
           auto-reassociate. */
        led_gpio_set_state(BLINK_JOINING);

        xEventGroupClearBits(wifi_event_group, CONNECTED_BIT);
        if (wifi_control_state == WIFI_CONTROL_STA_WAIT_CONNECT) {
            /* We were trying to connect, but it failed immediately */
            wifi_control_state = WIFI_CONTROL_STA_DISCONNECTED;
            /* If first time, then reset wifi config and restart the AP */
            if (wifi_control_get_mode() == WIFI_SETUP_MODE_STATION_FIRST_TIME) {
                ESP_LOGI(TAG, "Wifi join failed, and we ere joining for the first time, setting commissioning AP mode and restart.");
                wifi_control_save_mode(WIFI_SETUP_MODE_UNCOMMISSIONED);
                ESP_ERROR_CHECK( esp_wifi_stop() );
                ESP_ERROR_CHECK( esp_wifi_restore() );
                ESP_ERROR_CHECK( nvs_flash_erase() ); //This should get rid of calibration data too
                esp_restart();
            }
            else {
                ESP_LOGI(TAG, "Wifi join failed, and we were joining to a network that worked previously   - not rebooting - ");
                esp_wifi_connect();
                wifi_control_state = WIFI_CONTROL_STA_WAIT_CONNECT;
            }
        }
        else if (wifi_control_state == WIFI_CONTROL_STA_CONNECTED) {
            /* We were connected, but now we are no longer connected */
            esp_wifi_connect();
        }
     
        break;
    case SYSTEM_EVENT_AP_START:
        ESP_LOGI(TAG, "AP started, wifi state %i", wifi_control_state);
 
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
        if (wifi_control_state == WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION) {
            wifi_control_state = WIFI_CONTROL_COMMISSIONING_AP_STARTED;
            ap_started_ts = current_time;
            led_gpio_set_state(BLINK_COMMISSIONING);
        }
        else {
            wifi_control_state = WIFI_CONTROL_STANDALONE_AP_STARTED;
            led_gpio_set_state(BLINK_STANDALONE_AP);
        }
        break;
    case SYSTEM_EVENT_AP_STACONNECTED:
        /* A client station has connectied to our ESP32 in soft AP mode */
        ap_started_ts = current_time;
        ap_num_stations_connected++;
        ESP_LOGI(TAG, "Detected station connect, count is now %i", ap_num_stations_connected);
        break;
    case SYSTEM_EVENT_AP_STADISCONNECTED:
        ap_num_stations_connected--;
        ESP_LOGI(TAG, "Detected station disconnect, count is now %i", ap_num_stations_connected);
        break;
    case SYSTEM_EVENT_SCAN_DONE:
        ESP_LOGI(TAG, "Wifi scan complete");
        if (wifi_control_state == WIFI_CONTROL_STA_CONNECTED) {
            xEventGroupSetBits(wifi_event_group, WIFI_SCAN_COMPLETE_BIT);
        }
        
        break;
    default:
        break;
    }
    return ESP_OK;
}

/**
 * Load file to deduce which which mode the system is configured
 * 
 * NOTE: wifi_driver_init must be run first.
 */
enum wifi_control_mode wifi_control_get_mode(void) {
    enum wifi_control_mode mode = WIFI_SETUP_MODE_ILLEGAL;
    bson bs;
    memset(&bs, 0, sizeof(bs));
    int n = app_util_load_file(WIFI_CONTROL_FILENAME, &bs);
 
    if (n > 0) {
        ESP_LOGI(TAG, "wifi_control_init config load: %i", n);
        bson_iterator it;
        bson_find_from_buffer(&it, bs.data, "wifiMode");
        if (BSON_INT != bson_iterator_type(&it)) {
            ESP_LOGI(TAG, "wifi_control_init bad format %i", n);
            mode = WIFI_SETUP_MODE_UNCOMMISSIONED;
        }
        else {
            mode = bson_iterator_int(&it);
            ESP_LOGI(TAG, "wifi_control_init wifiMode %i", wifi_control_state);
        }
    }
    else {
        /* No config file available */
        ESP_LOGI(TAG, "wifi_control_init initial mode, %i.", n);
        mode = WIFI_SETUP_MODE_UNCOMMISSIONED;
    }
    
    bson_destroy(&bs);
    
    ESP_ERROR_CHECK( mode != WIFI_SETUP_MODE_ILLEGAL?ESP_OK:ESP_FAIL );
    return mode;
}

void wifi_control_init(void) {
    enum wifi_control_mode mode = wifi_control_get_mode();
    
    ESP_ERROR_CHECK( esp_event_loop_init(event_handler, NULL) );
    wifi_event_group = xEventGroupCreate();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK( esp_wifi_init(&cfg) );
    ESP_ERROR_CHECK( esp_wifi_set_storage(WIFI_STORAGE_FLASH) );

    wifi_country_t wifi_country = { .cc="EU", .schan=1, .nchan=13, .policy=WIFI_COUNTRY_POLICY_AUTO };
    ESP_ERROR_CHECK ( esp_wifi_set_country(&wifi_country) );

    int8_t max_pwr=0;

    switch (mode) {
        case WIFI_SETUP_MODE_UNCOMMISSIONED:
            wifi_control_state = WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION;
            ESP_LOGI(TAG, "wifi_control_init SSID WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION");
            break;
        case WIFI_SETUP_MODE_STATION_FIRST_TIME:
        case WIFI_SETUP_MODE_STATION_CONFIRMED:
             /* Start connecting to the wifi */
            wifi_control_state = WIFI_CONTROL_STA_WAIT_CONNECT;
            
            wifi_config_t stored_wifi_config;
            ESP_ERROR_CHECK ( esp_wifi_get_config(ESP_IF_WIFI_STA, &stored_wifi_config));
            /* Valid wifi config must be present, this is a precondition */
            ESP_ERROR_CHECK ( (strlen(stored_wifi_config.sta.ssid) > 0)?ESP_OK:ESP_FAIL );
            
            ESP_LOGI(TAG, "Stored WiFi configuration SSID %s %s...", stored_wifi_config.sta.ssid, stored_wifi_config.sta.password);
            ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_STA, &stored_wifi_config) );
            ESP_ERROR_CHECK(esp_wifi_start());
            
            // In the event handler we will move to WIFI_CONTROL_STA_CONNECTED when we see that we have connected.
            break;
        case WIFI_SETUP_MODE_STANDALONE_AP:
            wifi_control_state = WIFI_CONTROL_STANDALONE_AP_STARTED;
            
            wifi_control_start_ap_scan_blocking();
            
            ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
            ESP_ERROR_CHECK( esp_wifi_get_config(ESP_IF_WIFI_AP, &stored_wifi_config) );
            ESP_LOGI(TAG, "AP configuration has stored SSID %s", stored_wifi_config.ap.ssid);
            ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &stored_wifi_config) );
            ESP_ERROR_CHECK( esp_wifi_start() );
            
            break;
        case WIFI_SETUP_MODE_ILLEGAL:
            ESP_LOGI(TAG, "Bad wifi mode in wifi_control_init");
            ESP_ERROR_CHECK( ESP_FAIL );
            break;
    }
    ESP_LOGI(TAG, "wifi_control_init initialised to %i", wifi_control_state);
    esp_wifi_get_max_tx_power(&max_pwr);
    ESP_LOGI(TAG, "wifi power: %i\n", max_pwr);
    
}

void wifi_control_periodic(void) {
    
    current_time++;
    
    if (current_time % 10 == 0) {
        ESP_LOGI(TAG, "wifi_control_state %i", wifi_control_state);
    }
    
    if (wifi_control_state == WIFI_CONTROL_STA_CONNECTED) {
        wifi_poll_ap_list_updated();
    }
        
    switch (wifi_control_state) {
        case WIFI_CONTROL_COMMISSIONING_AP_STARTED:
            if (current_time > (ap_started_ts + WIFI_COMMISSIONING_AP_ACTIVE_TIME)) {
                if (ap_num_stations_connected > 0 && ota_update_get_state() == OTA_ONGOING) {
                    ESP_LOGI(TAG, "Stations connected and OTA update ongoing, postpone AP shutdown");
                    ap_started_ts = current_time;
                }
                else {
                    wifi_control_event_stop_commissioning_ap();
                    ap_started_ts = 0;
                }
            }
            break;
        default:
            break;
        
    }
}

wifi_ap_record_t *ap_records = NULL;
int ap_records_num = 0;

wifi_ap_record_t* get_ap_records(void) {
    return ap_records;
}

int get_ap_records_num(void) {
    return ap_records_num;
}

/** Start a blocking wifi scan. You must enter this mode with wifi disabled. Note that this will start wifi, switch mode to Station, do the scan, and then stop wifi.
 This function cannot be used in station mode, but only when the AP is stopped. */
void wifi_control_start_ap_scan_blocking(void) {
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK( esp_wifi_start() );
    wifi_scan_config_t scan_config = { 0 };
    ESP_ERROR_CHECK( esp_wifi_scan_start(&scan_config, true) ); /* Note: this is blocks until scan is ready! */

    /* Update AP list */
    uint16_t num_found_ap = 0;
    ESP_ERROR_CHECK( esp_wifi_scan_get_ap_num(&num_found_ap) );

    if (ap_records != NULL) {
        free(ap_records);
    }
    ap_records = (wifi_ap_record_t*) malloc(num_found_ap * sizeof(wifi_ap_record_t));
    ap_records_num = num_found_ap;
    ESP_ERROR_CHECK( esp_wifi_scan_get_ap_records(&num_found_ap, ap_records) );

    ESP_LOGI(TAG, "Found %i networks in inital scan.", num_found_ap);

    ESP_ERROR_CHECK( esp_wifi_stop() );
}

/**
 * Request system to start a scan to refresh the wifi AP list that is presented via Mist config app  
 * Note that this can only be run if the WIFI is in STA mode and wifi has been started.
 * 
 */
void wifi_control_ap_scan_start(void) {
    
    wifi_scan_config_t scan_config = { 0 };
    ESP_ERROR_CHECK( esp_wifi_scan_start(&scan_config, false) ); /* Note: non-blocking scan */
    xEventGroupClearBits(wifi_event_group, WIFI_SCAN_COMPLETE_BIT);
    /* The story will continue once SYSTEM_EVENT_SCAN_DONE is handled */
}

void wifi_control_ap_scan_complete(void) {
    
    if (wifi_control_state != WIFI_CONTROL_STA_CONNECTED) {
        return;
    }
    /* Update AP list */
    uint16_t num_found_ap = 0;
    ESP_ERROR_CHECK( esp_wifi_scan_get_ap_num(&num_found_ap) );

    if (ap_records != NULL) {
        free(ap_records);
    }
    ap_records = (wifi_ap_record_t*) malloc(num_found_ap * sizeof(wifi_ap_record_t));
    ap_records_num = num_found_ap;
    ESP_ERROR_CHECK( esp_wifi_scan_get_ap_records(&num_found_ap, ap_records) );

    ESP_LOGI(TAG, "Found %i networks in scan.", num_found_ap);
}

void wifi_poll_ap_list_updated(void) {
    
    EventBits_t event_bits = xEventGroupWaitBits(wifi_event_group, WIFI_SCAN_COMPLETE_BIT, pdTRUE, pdTRUE, 10 );
    if (event_bits & WIFI_SCAN_COMPLETE_BIT) {
        wifi_control_ap_scan_complete();
#ifndef WITHOUT_MIST_CONFIG_APP
        wifi_scan_complete_send_results();
#endif
    }
}

/**
 * Signal that user has decided to start a commissioning. 
 * Start the scan of wifi networks, and when results are ready, start the commissioning AP 
 */
void wifi_control_event_start_commissioning_ap(void) {
    if (wifi_control_state != WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION) {
        ESP_LOGI(TAG, "Not in state WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION, skipping");
        return;
    }
    
    led_gpio_set_state(BLINK_DARK);
    
    wifi_control_start_ap_scan_blocking();
    
    /* Start as AP.  */
    ESP_ERROR_CHECK( esp_wifi_set_mode(WIFI_MODE_AP) );
    
    /* AP configuration is default */
    const int ssid_max_len = WIFI_SSID_MAX_LEN;
    uint8_t ssid_str[ssid_max_len];

    if (wifi_control_get_mode() == WIFI_SETUP_MODE_STANDALONE_AP) {
        if (strnlen(wifi_standalone_ssid_name, WIFI_SSID_MAX_LEN) > 0) {
            snprintf(ssid_str, ssid_max_len, wifi_standalone_ssid_name);
        }
        else {
            uint8_t ap_mac_addr[6] = { 0 };
            ESP_ERROR_CHECK( esp_wifi_get_mac(ESP_IF_WIFI_AP, ap_mac_addr)  );
            snprintf(ssid_str, ssid_max_len, "Mist ESP32 %i:%i", ap_mac_addr[4], ap_mac_addr[5]);
            ESP_LOGW(TAG, "Standalone SSID not set. Using %s", ssid_str);
        }
    }
    else {
        snprintf(ssid_str, ssid_max_len, MIST_PORT_COMMISSIONING_WIFI_SSID);
    }
    wifi_config_t ap_config = {
        .ap = {
            .authmode = WIFI_AUTH_OPEN,
            .channel = 3,
            .max_connection = 4,
        },
    };
    memcpy(ap_config.ap.ssid, ssid_str, ssid_max_len);

    if (wifi_control_get_mode() == WIFI_SETUP_MODE_STANDALONE_AP) {
        ESP_LOGI(TAG, "Starting as AP %s", ap_config.ap.ssid); 
    }
    else {
        ESP_LOGI(TAG, "Starting as Mist commissioning AP %s", ap_config.ap.ssid); 
    }
    ESP_ERROR_CHECK( esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config) );
    ESP_ERROR_CHECK(esp_wifi_start());
    ap_num_stations_connected = 0;
    led_gpio_set_state(BLINK_COMMISSIONING);
}

void wifi_control_event_stop_commissioning_ap(void) {
    ESP_LOGI(TAG, "Stopping Mist commissioning AP"); 
    ESP_ERROR_CHECK(esp_wifi_stop());
    wifi_control_state = WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION;
    led_gpio_set_state(BLINK_WAITING);
}

void wifi_control_save_mode(enum wifi_control_mode mode) {
    ESP_ERROR_CHECK( mode != WIFI_SETUP_MODE_ILLEGAL ? ESP_OK:ESP_FAIL);
    
    bson bs;
    bson_init(&bs);
    bson_append_int(&bs, "wifiMode", mode);
    bson_finish(&bs);
    int save_ret = app_util_save_file(WIFI_CONTROL_FILENAME, &bs);
    ESP_LOGI(TAG, "wifi_control_init mode saved, %i.", save_ret);
    bson_destroy(&bs);
}

void wifi_set_standalone_ap_ssid(char *ssid) {
    strncpy(wifi_standalone_ssid_name, ssid, WIFI_SSID_MAX_LEN);
}