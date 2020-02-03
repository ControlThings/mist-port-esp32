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
#pragma once

#include "esp_wifi_types.h"

#define WIFI_CONTROL_FILENAME "wifi_control.bson"
/** The maximum length and access point SSID can have */
#define WIFI_SSID_MAX_LEN 32 /* FIXME: replace this with platform's own define. But 32 is the max len anyway. */ 

enum wifi_control_state {
    WIFI_CONTROL_INITIAL,
    /* States related to commissioning flow */
    WIFI_CONTROL_COMMISSIONING_AP_WAIT_ACTIVATION,
    WIFI_CONTROL_COMMISSIONING_AP_WAIT_SCAN_WIFI,
    WIFI_CONTROL_COMMISSIONING_AP_STARTED,
    /* States related to when the wifi-module is configured to join a wifi */
    WIFI_CONTROL_STA_WAIT_CONNECT,
    WIFI_CONTROL_STA_CONNECTED,
    WIFI_CONTROL_STA_DISCONNECTED,
    /* States related to when the wifi module is configured to stand-alone state */
    WIFI_CONTROL_STANDALONE_AP_STARTED
};

enum wifi_control_mode {
    WIFI_SETUP_MODE_UNCOMMISSIONED,
    WIFI_SETUP_MODE_STATION_FIRST_TIME, //After new network settings are saved, we save this mode, so that we can detect later if it fails initially or not (e.g. for bad password entered by user) 
    WIFI_SETUP_MODE_STATION_CONFIRMED, //After we have once successfully joined a network, this mode is saved
    WIFI_SETUP_MODE_STANDALONE_AP,
    WIFI_SETUP_MODE_ILLEGAL,
};

#define WIFI_COMMISSIONING_AP_ACTIVE_TIME (15*60) /* seconds */

void wifi_control_init(void);

void wifi_control_periodic(void);

enum wifi_control_mode wifi_control_get_mode(void);

void wifi_control_save_mode(enum wifi_control_mode mode);

void wifi_control_event_start_commissioning_ap(void);

void wifi_control_event_stop_commissioning_ap(void);

int wifi_ap_get_num_stations_connected(void);

enum wifi_control_state wifi_get_control_state(void);

wifi_ap_record_t* get_ap_records(void);
int get_ap_records_num(void);

void wifi_control_ap_scan_start(void);
void wifi_control_start_ap_scan_blocking(void);
void wifi_control_ap_scan_complete(void);

/**
 * Set the SSID to use when configured for standalone accespoint mode, ie. when mode is WIFI_SETUP_MODE_STANDALONE_AP
 */
void wifi_set_standalone_ap_ssid(char *ssid);