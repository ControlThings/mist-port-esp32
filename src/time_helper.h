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
 * File:   time_helper.h
 * Author: jan
 *
 * Created on January 23, 2017, 1:35 PM
 */

#ifndef TIME_HELPER_H
#define TIME_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

    double timeval_subtract (struct timeval *result, struct timeval *x, struct timeval *y);


#ifdef __cplusplus
}
#endif

#endif /* TIME_HELPER_H */

