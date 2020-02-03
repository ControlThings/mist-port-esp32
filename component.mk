COMPONENT_SRCDIRS := src

COMPONENT_ADD_INCLUDEDIRS := src/

# submodule wish-c99
COMPONENT_SRCDIRS+=deps/wish-c99/src deps/wish-c99/deps/bson \
deps/wish-c99/deps/ed25519/src   \
deps/wish-c99/deps/uthash/src    \
deps/wish-c99/deps/wish-rpc-c99/src/

COMPONENT_ADD_INCLUDEDIRS+=deps/wish-c99/src \
deps/wish-c99/deps/wish-rpc-c99/src/ \
deps/wish-c99/deps/bson \
deps/wish-c99/deps/uthash/src \
deps/wish-c99/deps/ed25519/src

CFLAGS+=-DWITHOUT_STRTOIMAX \
-DWISH_PORT_RPC_BUFFER_SZ=4096 \
-DNDEBUG

# submodule mist-c99
COMPONENT_SRCDIRS+=deps/mist-c99/src deps/mist-c99/wish_app

COMPONENT_ADD_INCLUDEDIRS+=deps/mist-c99/src \
deps/mist-c99/deps/bson \
deps/mist-c99/deps/uthash/src \
deps/mist-c99/wish_app

CFLAGS+=-DMIST_API_VERSION_STRING=\"esp32\" \
-DMIST_RPC_REPLY_BUF_LEN=4096 \
-DMIST_API_MAX_UIDS=10 \
-DMIST_API_REQUEST_POOL_SIZE=10 \
-DWISH_PORT_RPC_BUFFER_SZ=4096 \
-DMIST_CONTROL_MODEL_BUFFER_FROM_HEAP

# Mist config ESP32 app. It can be disabled with -DWITHOUT_MIST_CONFIG_APP in project's Makefile.projbuild
COMPONENT_SRCDIRS+=mist_config_esp32
COMPONENT_ADD_INCLUDEDIRS+=mist_config_esp32

# Disable select warnings, applying to wish-c99 and mist-c99
CFLAGS+=-Wno-pointer-sign -Wno-unused-variable -Wno-unused-but-set-variable -Wno-unused-function

#If sdkconfig log level is 0, then do not include any logging from wish/mist
ifeq ($(CONFIG_LOG_DEFAULT_LEVEL),0)
    CFLAGS+=-DRELEASE_BUILD
endif
