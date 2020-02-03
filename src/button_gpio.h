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

/* 
 * File:   button_gpio.h
 * Author: jan
 *
 * Created on January 22, 2017, 11:15 PM
 */

#ifndef BUTTON_GPIO_H
#define BUTTON_GPIO_H

#ifdef __cplusplus
extern "C" {
#endif
#include <stdbool.h>
#include <sys/time.h>

/** Typedef for callback function for signaling a button new state */
typedef void (*button_new_state_cb)(bool new_state);

/** Typedef for callback function for signaling a button short press */
typedef void (*button_short_press_cb)(void);

/** Typedef for callback function for signaling a button long press */
typedef void (*button_long_press_cb)(void);

struct gpio_event {
    int gpio_num;
    bool state;
    struct timeval tval;
};

/**
 * Setup GPIO task for button events
 * 
 * @param gpio_num The GPIO number of the button input
 * @param button_new_state_cb The CB function which will be called when button has been pressed continously for over the debounce time
 * @param short_press_seconds_max The maxmimum time a "short press" can last
 * @param button_short_press_cb The CB function which will be called when short button press is detected
 * @param long_press_seconds_min The minimum time a long button press must last
 * @param button_long_press_cb The CB function to call when long button press is detected
 */
void button_gpio_setup(int gpio_num, button_new_state_cb, int short_press_seconds, button_short_press_cb, int long_press_seconds, button_long_press_cb);

/**
 * Periodic function to be called for Processing GPIO events.
 * 
 * @param ticksToWait The amount of FreeRTOS system ticks to wait for GPIO events. Value 0 will make the function return immediately, but you need to make sure that this will not lead to busy-looping, it means that some other step in the task loop puts the task in blocked state so that other tasks can run!
 */
void button_gpio_process_events(int ticksToWait);

bool button_gpio_get_current_state(void);

#ifdef __cplusplus
}
#endif

#endif /* BUTTON_GPIO_H */

