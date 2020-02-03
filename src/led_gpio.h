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

enum blink_state { BLINK_WAITING, BLINK_COMMISSIONING, BLINK_JOINING, BLINK_NETWORK_OK, BLINK_STANDALONE_AP, BLINK_DARK };
enum led_active_state { 
    LED_ON_WHEN_PIN_LOW,    /* Select this if LED is between VCC and pin. (pin sinks current) */
    LED_ON_WHEN_PIN_HIGH,   /* Select this if LED is between GND and pin (pin sources current) */
};

void led_gpio_start_task(int gpio_num, enum led_active_state active_state, int task_priority);

void led_gpio_set_state(enum blink_state new_state);
