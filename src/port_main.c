/* "Normal" C includes */
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

/* posix C */
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>

/* FreeRTOS includes */
#include "freertos/FreeRTOS.h"

/* Wish and Mist lib includes */
#include "wish_connection.h"
#include "wish_connection_mgr.h"
#include "wish_local_discovery.h"
#include "wish_identity.h"
#include "wish_event.h"
#include "wish_port_config.h"
#include "utlist.h"

#ifndef WITHOUT_MIST_CONFIG_APP
/* Built-in apps includes */
#include "mist_config.h"
#endif //WITHOUT_MIST_CONFIG_APP

/* Port includes */
#include "port_platform.h"
#include "port_net.h"
#include "port_dns.h"
#include "port_service_ipc.h"
#include "port_main.h"
#include "port_log.h"

static bool listen_to_adverts = true;
static bool as_server = true;
static bool as_relay_client = true;

#define TAG "port_main"

void mist_port_esp32_init(char* default_alias) {

    port_platform_deps();
    
    /* Start initialising Wish core */
    wish_core_t *core = port_net_get_core();
    
    wish_core_init(core);
    
    port_platform_load_ensure_identities(core, default_alias);
    
    if (as_server) {
        setup_wish_server(core);
    }
    
    if (listen_to_adverts) {
        setup_wish_local_discovery();
    }
    
    port_dns_init();
#ifndef WITHOUT_MIST_CONFIG_APP
    mist_config_init();
#endif //WITHOUT_MIST_CONFIG_APP
}

static int max_fd = 0;

static void update_max_fd(int fd) {
    if (fd >= max_fd) {
        max_fd = fd + 1;
    }
}

static void network_periodic(unsigned int max_block_time_ms) {
    int wld_fd = get_wld_fd();
    int server_fd = get_server_fd();
    wish_core_t *core = port_net_get_core();
    
    port_dns_poll_result();
    fd_set rfds;
    fd_set wfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_ZERO(&wfds);

    if (as_server) {
        FD_SET(server_fd, &rfds);
        update_max_fd(server_fd);
    }

    if (listen_to_adverts) {
        FD_SET(wld_fd, &rfds);
        update_max_fd(wld_fd);
    }

    if (as_relay_client) {
        wish_relay_client_t* relay;

        LL_FOREACH(core->relay_db, relay) {
            if (relay->curr_state == WISH_RELAY_CLIENT_CONNECTING) {
                FD_SET(relay->sockfd, &wfds);
                update_max_fd(relay->sockfd);
            }
            else if (relay->curr_state == WISH_RELAY_CLIENT_WAIT_RECONNECT) {
                /* connect to relay server has failed or disconnected and we wait some time before retrying */
            }
            else if (relay->curr_state == WISH_RELAY_CLIENT_RESOLVING) {
                /* Don't do anything as the resolver is resolving. relay->sockfd is not valid as it has not yet been initted! */
            }
            else if (relay->curr_state != WISH_RELAY_CLIENT_INITIAL) {
                FD_SET(relay->sockfd, &rfds);
                update_max_fd(relay->sockfd);
            }
        }
    }

    int i = -1;
    for (i = 0; i < WISH_PORT_CONTEXT_POOL_SZ; i++) {
        wish_connection_t* ctx = &(core->connection_pool[i]);
        if (ctx->context_state == WISH_CONTEXT_FREE) {
            continue;
        }
        else if (ctx->curr_transport_state == TRANSPORT_STATE_RESOLVING) {
            /* The transport host addr is being resolved, sockfd is not valid and indeed should not be added to any of the sets! */
            continue;
        }
        if (ctx->send_arg == NULL) {
            PORT_LOGERR(TAG, "context in state %i but send_arg is NULL", ctx->context_state);
            
            /* Here we should abandon the connection, since this occurs when everything else fails too, e.g. when all socket resources have been exhausted */
            wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
            continue;
        }
        int sockfd = *((int *) ctx->send_arg);
        if (ctx->curr_transport_state == TRANSPORT_STATE_CONNECTING) {
            /* If the socket has currently a pending connect(), set
             * the socket in the set of writable FDs so that we can
             * detect when connect() is ready */
            FD_SET(sockfd, &wfds);
        }
        else {
            FD_SET(sockfd, &rfds);
        }
        update_max_fd(sockfd);
    }


    tv.tv_sec = 0;
    tv.tv_usec = max_block_time_ms*1000;
    int select_ret = select( max_fd, &rfds, &wfds, NULL, &tv );

    /* Zero fds ready means we timed out */
    if ( select_ret > 0 ) {
        //printf("there is a fd ready\n");
        if (FD_ISSET(wld_fd, &rfds)) {
            read_wish_local_discovery();
        }

        if (as_relay_client) {
            wish_relay_client_t* relay;

            LL_FOREACH(core->relay_db, relay) {

                if (FD_ISSET(relay->sockfd, &wfds)) {
                    int connect_error = 0;
                    socklen_t connect_error_len = sizeof(connect_error);
                    if (getsockopt(relay->sockfd, SOL_SOCKET, SO_ERROR, 
                            &connect_error, &connect_error_len) == -1) {
                        perror("Unexepected getsockopt error");
                        PORT_ABORT();
                    }
                    if (connect_error == 0) {
                        /* connect() succeeded, the connection is open */
                        //printf("Relay client connected\n");
                        relay_ctrl_connected_cb(core, relay);
                        wish_relay_client_periodic(core, relay);
                    }
                    else {
                        /* connect fails. Note that perror() or the
                         * global errno is not valid now */
                        PORT_LOGERR(TAG, "relay control connect() failed: %s", strerror(errno));

                        // FIXME only one relay context assumed!
                        relay_ctrl_connect_fail_cb(core, relay);
                        close(relay->sockfd);
                    }
                }

                if (FD_ISSET(relay->sockfd, &rfds)) {
                    uint8_t byte;   /* That's right, we read just one
                        byte at a time! */
                    int read_len = read(relay->sockfd, &byte, 1);
                    if (read_len > 0) {
                        wish_relay_client_feed(core, relay, &byte, 1);
                        wish_relay_client_periodic(core, relay);
                    }
                    else if (read_len == 0) {
                        PORT_LOGWARN(TAG, "Relay control connection disconnected");
                        relay_ctrl_disconnect_cb(core, relay);
                        close(relay->sockfd);
                    }
                    else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            PORT_LOGWARN(TAG, "relay control read(), errno: %s, this causes no further actions", strerror(errno));
                        }
                        else {
                            PORT_LOGERR(TAG, "relay control read(), errno: %s", strerror(errno));
                            relay_ctrl_disconnect_cb(core, relay);
                            close(relay->sockfd);
                        }
                    }
                }
            }
        }


        int i = 0;
         /* Check for Wish connections status changes */
        for (i = 0; i < WISH_PORT_CONTEXT_POOL_SZ; i++) {
            wish_connection_t* ctx = &(core->connection_pool[i]);
            if (ctx->context_state == WISH_CONTEXT_FREE) {
                continue;
            }
            if ((int *) ctx->send_arg == NULL) {
                continue;
            }
            int sockfd = *((int *)ctx->send_arg);
            if (FD_ISSET(sockfd, &rfds)) {
                //printf("wish socket readable\n");
                /* The Wish connection socket is now readable. Data
                 * can be read without blocking */
                int rb_free = wish_core_get_rx_buffer_free(core, ctx);
                if (rb_free == 0) {
                    /* Cannot read at this time because ring buffer
                     * is full */
                    PORT_LOGERR(TAG, "ring buffer full");
                    continue;
                }
                if (rb_free < 0) {
                    PORT_LOGERR(TAG, "Error getting ring buffer free sz");
                    PORT_ABORT();
                }
                const size_t read_buf_len = rb_free;
                uint8_t buffer[read_buf_len];
                int read_len = read(sockfd, buffer, read_buf_len);
                if (read_len > 0) {
                    //printf("Read some data\n");
                    wish_core_feed(core, ctx, buffer, read_len);
                    struct wish_event ev = { 
                        .event_type = WISH_EVENT_NEW_DATA,
                        .context = ctx };
                    wish_message_processor_notify(&ev);
                }
                else if (read_len == 0) {
                    PORT_LOGINFO(TAG, "Wish connection closed.");
                    close(sockfd);
                    free(ctx->send_arg);
                    wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
                    continue;
                }
                else if (read_len < 0) {
                    if (errno == EAGAIN || errno == EWOULDBLOCK) {
                        ESP_LOGW(TAG, "wish connection socket, errno=%s, just continuing", strerror(errno));
                        continue;
                    }
                    else {
                        ESP_LOGE(TAG, "wish connection socket read_len=%i: %s, closing connection", read_len, strerror(errno));
                        close(sockfd);
                        free(ctx->send_arg);
                        wish_core_signal_tcp_event(core, ctx, TCP_DISCONNECTED);
                        continue;
                    }
                }

                
            }
            if (FD_ISSET(sockfd, &wfds)) {
                /* The Wish connection socket is now writable. This
                 * means that a previous connect succeeded. (because
                 * normally we don't select for socket writability!)
                 * */
                int connect_error = 0;
                socklen_t connect_error_len = sizeof(connect_error);
                if (getsockopt(sockfd, SOL_SOCKET, SO_ERROR, 
                        &connect_error, &connect_error_len) == -1) {
                    PORT_LOGERR(TAG, "Unexepected getsockopt error, errno=%s", strerror(errno));
                    PORT_ABORT();
                }
                if (connect_error == 0) {
                    /* connect() succeeded, the connection is open
                     * */
                    if (ctx->curr_transport_state 
                            == TRANSPORT_STATE_CONNECTING) {
                        if (ctx->via_relay) {
                            connected_cb_relay(ctx);
                        }
                        else {
                            connected_cb(ctx);
                        }
                    }
                    else {
                        PORT_LOGERR(TAG, "There is somekind of state inconsistency");
                        PORT_ABORT();
                    }
                }
                else {
                    /* connect fails. Note that perror() or the
                     * global errno is not valid now */
                    PORT_LOGERR(TAG, "wish connection connect() failed: %s", strerror(errno));
                    connect_fail_cb(ctx);
                    close(sockfd);
                }
            }

        }


        /* Check for incoming Wish connections to our server */
        if (as_server) {
            if (FD_ISSET(server_fd, &rfds)) {
                PORT_LOGINFO(TAG, "Detected incoming connection!");
                int newsockfd = accept(server_fd, NULL, NULL);
                if (newsockfd < 0) {
                    PORT_LOGERR(TAG, "on accept: errno %s, abandoning incoming connection", strerror(errno));
                }
                else {                
                    socket_set_nonblocking(newsockfd);
                    /* Start the wish core with null IDs. 
                    * The actual IDs will be established during handshake
                    * */
                    uint8_t null_id[WISH_ID_LEN] = { 0 };
                    wish_connection_t *ctx = wish_connection_init(core, null_id, null_id);
                    if (ctx == NULL) {
                        /* Fail... no more contexts in our pool */
                        PORT_LOGERR(TAG, "No new Wish connections can be accepted!");
                        close(newsockfd);
                    }
                    else {
                        int *fd_ptr = malloc(sizeof(int));
                        *fd_ptr = newsockfd;
                        /* New wish connection can be accepted */
                        wish_core_register_send(core, ctx, write_to_socket, fd_ptr);
                        wish_core_signal_tcp_event(core, ctx, TCP_CLIENT_CONNECTED);
                    }
                }
            }
        }



    }
    else if (select_ret == 0) {
        //printf("select timeout\n");
    }
    else {
        PORT_LOGERR(TAG, "select error: %s", strerror(errno));
        PORT_ABORT();
        exit(0);
    }
}

void mist_port_esp32_periodic(unsigned int max_block_time_ms) {
    network_periodic(max_block_time_ms);
    wish_core_t *core = port_net_get_core();
    
    while (1) {
        /* FIXME this loop is bad! Think of something safer */
        /* Call wish core's connection handler task */
        struct wish_event *ev = wish_get_next_event();
        if (ev != NULL) {
            wish_message_processor_task(core, ev);
        }
        else {
            /* There is nothing more to do, exit the loop */
            break;
        }
    }

    while (port_service_ipc_task_has_more()) {
        port_service_ipc_task();
        taskYIELD();
    }
    
    static time_t one_sec_ts = 0;
    if (time(NULL) > one_sec_ts) {
        /* Perform periodic action one second interval */
        one_sec_ts = time(NULL);
        wish_time_report_periodic(core);
#ifndef WITHOUT_MIST_CONFIG_APP
        mist_config_periodic();
#endif //WITHOUT_MIST_CONFIG_APP
    }
    
    static time_t timestamp = 0;
    if (time(NULL) > timestamp + 10) {
        timestamp = time(NULL);
        /* Perform periodic action 10s interval */
        PORT_LOGINFO(TAG, "System free heap: %i bytes.", esp_get_free_heap_size());
    }
}