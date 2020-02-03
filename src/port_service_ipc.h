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
 * port_service_ipc.h
 *
 *  Created on: Jan 24, 2018
 *      Author: jan
 */

#ifndef SOURCE_MIST_PORT_PORT_SERVICE_IPC_H_
#define SOURCE_MIST_PORT_PORT_SERVICE_IPC_H_

void port_service_ipc_task(void);

bool port_service_ipc_task_has_more(void);

#endif /* SOURCE_MIST_PORT_PORT_SERVICE_IPC_H_ */
