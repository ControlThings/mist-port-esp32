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



#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "button_gpio.h"
#include "reent.h" /* _gettimeofday_r */
#include "wifi_control.h"
#include "time_helper.h"
#include "esp_log.h"

#define ESP_INTR_FLAG_DEFAULT 0

static const char* TAG = "button-gpio";

static button_new_state_cb new_state_cb_fn = NULL;
static button_short_press_cb short_press_cb_fn = NULL;
static button_long_press_cb long_press_cb_fn = NULL;

static int button_gpio_num;
static float threshold_short_press = 0.0;
static float threshold_long_press = 0.0;

static const float debounce_time = 0.2;

static xQueueHandle gpio_evt_queue = NULL;

static void IRAM_ATTR gpio_isr_handler(void* arg)
{
    uint32_t gpio_num = (uint32_t) arg;
    struct gpio_event ev = { 0 };    /* Note: button state is inverted for app */
    struct _reent r;
    _gettimeofday_r(&r, &ev.tval, NULL); /* Note. this function is declared in IRAM, whereas the non-reentrant one is not! */
    ev.state = !gpio_get_level(gpio_num);   /* NOTE: sample the GPIO level only after gettiomeofday. It takes such a long time? */
    ev.gpio_num = gpio_num;
    
    xQueueSendFromISR(gpio_evt_queue, &ev, NULL);
}


void button_gpio_setup(int gpio_num, button_new_state_cb new_state_cb, int short_press_seconds_max, button_short_press_cb short_press_cb, int long_press_seconds_min, button_long_press_cb long_press_cb) {
    //create a queue to handle gpio event from isr
    gpio_evt_queue = xQueueCreate(10, sizeof(struct gpio_event));

    button_gpio_num = gpio_num;
    threshold_short_press = short_press_seconds_max;
    threshold_long_press = long_press_seconds_min;

    new_state_cb_fn = new_state_cb;
    short_press_cb_fn = short_press_cb;
    long_press_cb_fn = long_press_cb;

    gpio_config_t io_conf;

    //interrupt of rising edge
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    //bit mask of the pins
    io_conf.pin_bit_mask =  ((uint64_t)1<<button_gpio_num);
    //set as input mode    
    io_conf.mode = GPIO_MODE_INPUT;
    //enable pull-up mode
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);

    //install gpio isr service
    gpio_install_isr_service(ESP_INTR_FLAG_DEFAULT);
    //hook isr handler for specific gpio pin
    gpio_isr_handler_add(button_gpio_num, gpio_isr_handler, (void*) button_gpio_num);
}


static bool current_button_state;

bool button_gpio_get_current_state(void) {
    return current_button_state;
}

void button_gpio_process_events(int ticksToWait) {
       /* Process button event, and debounce logic */
    /* Storage for button event */
    struct gpio_event gpio_ev;
    /* Timestamp of previous button event */
    static struct timeval prev_button_ev_ts = { 0 };
    /* The raw, undebounced button state */
    static bool raw_button_state;
    
    static bool long_press_handled = true;
    static bool short_press_handled = true;
    /* Get all the button events from the queue, the last event will determine the "current undebounced" state of button */
    
    
    while (xQueueReceive(gpio_evt_queue, &gpio_ev, ticksToWait)) {
        ESP_LOGI(TAG, "GPIO[%d] intr, state %d time %i %i\n", gpio_ev.gpio_num, gpio_ev.state, (int) gpio_ev.tval.tv_sec, (int) gpio_ev.tval.tv_usec);
        
        struct timeval new_button_ev_ts = gpio_ev.tval;         
        prev_button_ev_ts = new_button_ev_ts;
        raw_button_state = gpio_ev.state;
    }
    
    /* Debounce */
    struct timeval curr_ts;
    gettimeofday(&curr_ts, NULL);
    struct timeval ts_diff;
    double time_difference = timeval_subtract (&ts_diff, &curr_ts, &prev_button_ev_ts);
    //ESP_LOGI(TAG, "time diff=%f\n", time_difference);
    if (raw_button_state != current_button_state && time_difference > 0.2) {
        current_button_state = raw_button_state;
        ESP_LOGI(TAG, "New button state: %i", current_button_state);
        
        if (new_state_cb_fn != NULL) {
            new_state_cb_fn(current_button_state);
        }
        else {
            ESP_LOGW(TAG, "No callback set for button new state event!");
        }

        long_press_handled = false;
        short_press_handled = false;
        
       
    }
    
    if (current_button_state == false && short_press_handled == false && time_difference > 0.2 && time_difference < 2.0) {
        if (short_press_cb_fn != NULL) {
            short_press_cb_fn();
        }
        else {
            ESP_LOGW(TAG, "No callback set for button short press event!");
        }
    }

    if (current_button_state == true && time_difference > 10.0 && long_press_handled == false) {
        /* Button has been held down for 10 minutes */
        long_press_handled = true;
        
        if (long_press_cb_fn != NULL) {
            long_press_cb_fn();
        }
        else {
            ESP_LOGW(TAG, "No callback set for button long press event!");
        }
    }
}
