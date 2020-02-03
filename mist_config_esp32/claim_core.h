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

#include "mist_model.h"

void claim_core_init(mist_model *model);

void claim_core_post_init(void);

bool claim_core_is_claimed(void);

/** 
 * Register a service, which will be "advertised" when the claimCore invoke is called.
 * 
 * @param wsid the service id of the service
 * @param service_name the name of the service to be advertised
 */
void claim_core_set_preferred_service(uint8_t *wsid, char *service_name); 

/**
 * Return the service name of the service set with claim_core_set_preferred_service()
 */
char *claim_core_get_preferred_service_name(void);