# Mist ESP32 generic port submodule

This is the generic Mist port for the ESP32 platform. This repository is
supposed to be used as a submodule to a ESP32 project repository.

The role of this submodule is to group all ESP32-specific Wish and Mist
port functions to one and same repo, which can be used in all the ESP32
projects.

## Tunable options for the esp32 port: Wish & Mist

The following options can be set.

### The default name for the automatically created device identity

Device identity defined by _mist_port_esp32_init()_, which calls functions in _port_platform.c_ to create an initial identity, if there are no identities.

### Commissioning wifi network name 

The wifi network name is set in main/Makefile.projbuild of the *enclosing project*.

Add in main/Makefile.projbuild

```
CFLAGS+=-DMIST_PORT_COMMISSIONING_WIFI_SSID=\"mist-olimex-esp32-evb\"
```

### Wish local discovery class

The wld class, also known as wld meta product

Add in main/Makefile.projbuild 
```
CFLAGS+=-DMIST_PORT_WLD_META_PRODUCT=\"fi.controlthings.olimex-esp32-evb\"
```

### Allow remote management of local wish core by all peers

Add in main/Makefile.projbuild 
```
CFLAGS+=-DWISH_REMOTE_MANAGEMENT
```

### Relay server for module

Add in main/Makefile.projbuild 

```
CFLAGS+=-DRELAY_SERVER_HOST=\"host:port\"
```


### Mist config app

mist-port-esp32 includes the Mist config ESP32 app, which is used for for
wifi commissioning. The app can be *disabled* by adding to CFLAGS:

```
CFLAGS+=-DWITHOUT_MIST_CONFIG_APP
```

