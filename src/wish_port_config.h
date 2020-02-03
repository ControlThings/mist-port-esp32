#ifndef WISH_PORT_CONFIG_H
#define WISH_PORT_CONFIG_H

/** Port-specific config variables */

/** This specifies the size of the receive ring buffer */
#define WISH_PORT_RX_RB_SZ (2*1500)

/** This specifies the maximum number of simultaneous Wish connections
 * */
#define WISH_PORT_CONTEXT_POOL_SZ   16

/** If this is defined, include support for the App TCP server */
//#define WITH_APP_TCP_SERVER

/** This specifies the maximum number of simultaneous app requests to core */
#define WISH_PORT_APP_RPC_POOL_SZ ( 60 )

/** This specifies the maximum size of the buffer where some RPC handlers build the reply (1400) */
#ifndef WISH_PORT_RPC_BUFFER_SZ
#define WISH_PORT_RPC_BUFFER_SZ ( 4096 )
#else
#if WISH_PORT_RPC_BUFFER_SZ != 4096
#error WISH_PORT_RPC_BUFFER_SZ declaration mismatch!
#endif
#endif

/** This defines the maximum number of entries in the Wish local discovery table (4).
 * You should make sure that in the worst case any message will fit into WISH_PORT_RPC_BUFFFER_SZ  */
#define WISH_LOCAL_DISCOVERY_MAX ( 4 ) /* wld.list: 64 local discoveries should fit in 16k RPC buffer size */

/** This defines the maximum number of uids in database (max number of identities + contacts) (4) 
     You should make sure that in the worst case any message will fit into WISH_PORT_RPC_BUFFFER_SZ */
#define WISH_PORT_MAX_UIDS ( 15 ) /* identity.list: 128 uid entries should fit into 16k RPC buffer */

#endif
