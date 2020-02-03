#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h> 
#include <time.h>
#include <errno.h>

#include "esp_log.h"


#include "wish_relay_client.h"
#include "wish_connection.h"
#include "port_dns.h"
#include "port_relay_client.h"

#define TAG "port relay_client"

void socket_set_nonblocking(int sockfd);

/* Function used by Wish to send data over the Relay control connection
 * */
int relay_send(int relay_sockfd, unsigned char* buffer, int len) {
    int n = write(relay_sockfd, buffer, len);
    ESP_LOGI(TAG, "Wrote %i bytes to relay", n);
    if (n < 0) {
        perror("ERROR writing to relay");
    }
    return 0;
}

/* FIXME move the sockfd inside the relay context, so that we can
 * support many relay server connections! */
int relay_sockfd;

void wish_relay_client_open(wish_core_t* core, wish_relay_client_t *relay, 
        uint8_t relay_uid[WISH_ID_LEN]) {
    /* FIXME this has to be split into port-specific and generic
     * components. For example, setting up the RB, next state, expect
     * byte, copying of id is generic to all ports */
    
    ring_buffer_init(&(relay->rx_ringbuf), relay->rx_ringbuf_storage, 
        RELAY_CLIENT_RX_RB_LEN);
    memcpy(relay->uid, relay_uid, WISH_ID_LEN);


    ESP_LOGI(TAG, "Relay client open: %s port %i", relay->host, relay->port);

    wish_ip_addr_t relay_ip;
    if (wish_parse_transport_ip(relay->host, 0, &relay_ip) == RET_FAIL) {
        /* The relay's host was not an IP address. DNS Resolve first. */
        relay->curr_state = WISH_RELAY_CLIENT_RESOLVING;
        port_dns_start_resolving_relay_client(core, relay, relay->host);
    }
    else {
        
        port_relay_client_open(core, relay, &relay_ip);
    }
    
}
        
void port_relay_client_open(wish_core_t *core, wish_relay_client_t* relay, wish_ip_addr_t *relay_ip) {
    relay->curr_state = WISH_RELAY_CLIENT_CONNECTING;

    struct sockaddr_in relay_serv_addr;
    relay->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    
    if (relay->sockfd < 0) {
        perror("ERROR opening socket");
        relay->curr_state = WISH_RELAY_CLIENT_WAIT_RECONNECT;
        return;
    }
    socket_set_nonblocking(relay->sockfd);

    relay_serv_addr.sin_family = AF_INET;
    char ip_str[12+3+1] = { 0 };
    sprintf(ip_str, "%i.%i.%i.%i", 
        relay_ip->addr[0], relay_ip->addr[1], 
        relay_ip->addr[2], relay_ip->addr[3]);
    
    ESP_LOGI(TAG, "Relay server ip is %s port %d", ip_str, relay->port);
    inet_aton(ip_str, &relay_serv_addr.sin_addr);
    relay_serv_addr.sin_port = htons(relay->port);
    if (connect(relay->sockfd, (struct sockaddr *) &relay_serv_addr, 
            sizeof(relay_serv_addr)) == -1) {
        if (errno == EINPROGRESS) {
            relay->send = relay_send;
        }
        else {
            ESP_LOGE(TAG, "relay server connect: Unexpected errno %i", errno);
            perror("relay server connect()");
            wish_relay_client_close(core, relay);
        }
    }
}

void wish_relay_client_close(wish_core_t* core, wish_relay_client_t *relay) {
    close(relay->sockfd);
    relay_ctrl_disconnect_cb(core, relay);
}


