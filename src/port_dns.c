#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/dns.h"

#include "wish_ip_addr.h"
#include "port_dns.h"
#include "wish_connection_mgr.h"
#include "port_relay_client.h"
#include "port_log.h"

QueueHandle_t dnsResultQueue;

struct dns_callback_arg {
    wish_connection_t *conn;
    wish_relay_client_t *relay;
    wish_core_t *core;
};

struct dns_result_item {
    bool error; 
    wish_ip_addr_t ip; 
    wish_connection_t *conn;
    wish_relay_client_t *relay;
    wish_core_t *core;
};

#define TAG "port_dns"

/** The callback to dns_gethostbyname(). 
 * Note that this is called by an other thread (not the main thread which runs Wish and Mist). Hence the IPC messaging.
 */
void dns_resolve_cb(const char *name, const ip_addr_t *ip_addr, void *arg) {
    struct dns_callback_arg *callback_arg = arg;
    struct dns_result_item queue_item; 
    memset(&queue_item, 0, sizeof (struct dns_result_item));
    
    if (ip_addr == NULL) {
        // There was a DNS error. 
        PORT_LOGERR(TAG, "DNS error, conn %p relay %p", callback_arg->conn, callback_arg->relay);
        
        // Put structure signifying DNS error to message queue
        queue_item.error = true;         
    }
    else {
    

        PORT_LOGINFO(TAG, "%s %s resolved to : %i.%i.%i.%i",
                queue_item.relay?"Relay":"Host",
                name,
                ip4_addr1(&ip_addr->u_addr.ip4), 
                ip4_addr2(&ip_addr->u_addr.ip4), 
                ip4_addr3(&ip_addr->u_addr.ip4), 
                ip4_addr4(&ip_addr->u_addr.ip4));

        /* Put structure: ip addr, and callback addr to a packet and put it in message queue */
        queue_item.error = false;
        queue_item.ip.addr[0] = ip4_addr1(&ip_addr->u_addr.ip4);
        queue_item.ip.addr[1] = ip4_addr2(&ip_addr->u_addr.ip4);
        queue_item.ip.addr[2] = ip4_addr3(&ip_addr->u_addr.ip4);
        queue_item.ip.addr[3] = ip4_addr4(&ip_addr->u_addr.ip4);
    }
    
    queue_item.conn = callback_arg->conn;
    queue_item.relay = callback_arg->relay;
    queue_item.core = callback_arg->core;
    if (xQueueSend(dnsResultQueue, &queue_item, 0) != pdTRUE) {
        PORT_LOGERR(TAG, "Cannot put to DNS result queue!");
    }
}

void port_dns_init(void) {
    dnsResultQueue = xQueueCreate(10, sizeof (struct dns_result_item));
}

/**
 * This function is to be called periodically by the main loop, to fetch results of any DNS queries which have become ready.
 */
void port_dns_poll_result(void) {
    
    /* Poll message queue and pick result. If error, signal error to wish connection or relay client */
    struct dns_result_item item_in;
    if (xQueueReceive(dnsResultQueue, &item_in, 0) == pdTRUE) {
        /* Handle item */
        if (item_in.conn) {
            /* Wish connection resolving ready */
            if (item_in.error) {
                /* Resolving resulted to an error */
                wish_core_signal_tcp_event(item_in.core, item_in.conn, TCP_DISCONNECTED);
            }
            else {
                /* Resolving was a success, we can now open connection using the IP */
                wish_open_connection(item_in.conn->core, item_in.conn, &item_in.ip, item_in.conn->remote_port, item_in.conn->via_relay); 
            }
        } 
        else if (item_in.relay) {
            /* Relay client resolving ready */
            if (item_in.error) {                
                relay_ctrl_disconnect_cb(item_in.core, item_in.relay);
            }
            else {
                /* Relay client resolving succeeded */
                port_relay_client_open(item_in.core, item_in.relay, &item_in.ip);
            }
        }
    }
    
}

int port_dns_start_resolving_wish_conn(wish_connection_t *conn, char *qname) {
    
    ip_addr_t cached_ip_addr;
    IP_ADDR4( &cached_ip_addr, 0,0,0,0 );
    
    struct dns_callback_arg *arg = malloc(sizeof (struct dns_callback_arg));
    if (arg == NULL) {
        //DNS resolve error, insufficient resources
        
        // signal connection close
        return 0;
    }
    memset(arg, 0, sizeof (struct dns_callback_arg));
    arg->conn = conn;
    arg->relay = NULL;
    arg->core = conn->core;
    
    err_t dns_err = dns_gethostbyname(qname, &cached_ip_addr, dns_resolve_cb,  arg);
    
    if (dns_err == ERR_OK) {
        /* Result was cached, result in ip_addr */
        free(arg);
        
        PORT_LOGINFO(TAG, "Host %s resolved to (cached): %i.%i.%i.%i", 
                qname,
                ip4_addr1(&cached_ip_addr.u_addr.ip4), 
                ip4_addr2(&cached_ip_addr.u_addr.ip4), 
                ip4_addr3(&cached_ip_addr.u_addr.ip4), 
                ip4_addr4(&cached_ip_addr.u_addr.ip4));
        
        wish_ip_addr_t ip;
        ip.addr[0] = ip4_addr1(&cached_ip_addr.u_addr.ip4);
        ip.addr[1] = ip4_addr2(&cached_ip_addr.u_addr.ip4);
        ip.addr[2] = ip4_addr3(&cached_ip_addr.u_addr.ip4);
        ip.addr[3] = ip4_addr4(&cached_ip_addr.u_addr.ip4);
        
        //open connection to ip here. 
        wish_open_connection(conn->core, conn, &ip, conn->remote_port, conn->via_relay);
    }
    else if (dns_err == ERR_INPROGRESS) {
        /* DNS lookup has started */
        PORT_LOGINFO(TAG, "Started resolving host %s", qname);
    }
    else {
        PORT_LOGWARN(TAG, "dns_gethostbyname() unhandled return value %i", dns_err);
    }
    return 0;
}

int port_dns_start_resolving_relay_client(wish_core_t *core, wish_relay_client_t *rc, char *qname) {
    ip_addr_t cached_ip_addr;
    IP_ADDR4( &cached_ip_addr, 0,0,0,0 );
    
    struct dns_callback_arg *arg = malloc(sizeof (struct dns_callback_arg));
    if (arg == NULL) {
        //DNS resolve error, insufficient resources
        
        // signal connection close
        return 0;
    }
    memset(arg, 0, sizeof (struct dns_callback_arg));
    
    arg->conn = NULL;
    arg->relay = rc;
    arg->core = core;
    
    /* ... */
    
    err_t dns_err = dns_gethostbyname(qname, &cached_ip_addr, dns_resolve_cb,  arg);
    
    if (dns_err == ERR_OK) {
        /* Result was cached, result in ip_addr */
        free(arg);
        
        PORT_LOGINFO(TAG, "Relay host %s resolved to (cached): %i.%i.%i.%i", 
                qname,
                ip4_addr1(&cached_ip_addr.u_addr.ip4), 
                ip4_addr2(&cached_ip_addr.u_addr.ip4), 
                ip4_addr3(&cached_ip_addr.u_addr.ip4), 
                ip4_addr4(&cached_ip_addr.u_addr.ip4));
        
        wish_ip_addr_t ip;
        ip.addr[0] = ip4_addr1(&cached_ip_addr.u_addr.ip4);
        ip.addr[1] = ip4_addr2(&cached_ip_addr.u_addr.ip4);
        ip.addr[2] = ip4_addr3(&cached_ip_addr.u_addr.ip4);
        ip.addr[3] = ip4_addr4(&cached_ip_addr.u_addr.ip4);
        
        //open connection to ip here. 
         port_relay_client_open(core, rc, &ip);
    }
    else if (dns_err == ERR_INPROGRESS) {
        /* DNS lookup has started */
        PORT_LOGINFO(TAG, "Started resolving relay %s", qname);
    }
    else {
        PORT_LOGERR(TAG, "relay dns_gethostbyname() unhandled return value %i", dns_err);
    }
    
    return 0;
}

