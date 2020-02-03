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

#include "wish_connection.h"

int port_dns_start_resolving_wish_conn(wish_connection_t *conn, char *qname);

int port_dns_start_resolving_relay_client(wish_core_t *core, wish_relay_client_t *rc, char *qname);


void port_dns_init(void);

void port_dns_poll_result(void);

void port_dns_resolver_cancel_by_wish_connection(wish_connection_t *conn);

void port_dns_resolver_cancel_by_relay_client(wish_relay_client_t *rc);
