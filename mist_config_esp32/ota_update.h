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
 * File:   ota_update.h
 * Author: jan
 *
 * Created on February 7, 2018, 12:41 PM
 */

#ifndef OTA_UPDATE_H
#define OTA_UPDATE_H

#ifdef __cplusplus
extern "C" {
#endif
    
    enum ota_state { OTA_INITIAL, OTA_ONGOING, OTA_FINISHED };

    void ota_update_init(mist_model* model);
    
    enum ota_state ota_update_get_state(void);


#ifdef __cplusplus
}
#endif

#endif /* OTA_UPDATE_H */

