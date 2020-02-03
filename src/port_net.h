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
 * File:   port_net.h
 * Author: jan
 *
 * Created on January 6, 2017, 1:45 PM
 */

#ifndef PORT_NET_H
#define PORT_NET_H

#include "wish_connection.h"
#include "wish_connection_mgr.h"
#include "wish_debug.h"
#include "wish_local_discovery.h"
#include "wish_identity.h"

#ifdef __cplusplus
extern "C" {
#endif

    void setup_wish_local_discovery(void);
    int get_wld_fd(void);
    int get_server_fd(void);
    void setup_wish_server(wish_core_t* core);
    int write_to_socket(wish_connection_t* conn, unsigned char* buffer, int len);
    void socket_set_nonblocking(int sockfd);

    void read_wish_local_discovery(void);
    
    void connected_cb(wish_connection_t *ctx);
    void connected_cb_relay(wish_connection_t *ctx);
    void connect_fail_cb(wish_connection_t *ctx);
    
    wish_core_t* port_net_get_core(void);
    void port_net_get_local_uid(uint8_t *luid_buffer);

    
#ifdef __cplusplus
}
#endif

#endif /* PORT_NET_H */

