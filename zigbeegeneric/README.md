# Generic ZigBee

This plugin adds support for ZigBee specification compliant devices which don't require manufacturer specific extras. Any certified ZigBee device should
be supported by this. However, please note that many ZigBee devices out there either don't follow the specifiction or require manufacturer specific
extra handling in which case they may not work or only work partially with this plugin.

For such devices, please see the manufacturer specific ZigBee integration plugins.

Installing a manufacturer specific integration plugin will have higher priorty to handle their devices in the nymea system.

## Supported Things

> *IMPORTANT NOTE:* This list is non-exhaustive.

The following list contains devices that are known to work with this plugin. If a ZigBee device is not included in the following 
list, it does not necessarily imply that the device is not supported. Devices certified with the ZigBee logo are most likely to work
regardless.

> Got a device that works but is not listed here? Help us extend this list by letting us know in one of the community channels.

### On/off power sockets

All smart plugs, relays, power sockets and switches that follow the ZigBee OnOff cluster specification.

Known to work:

* Climax smart plug (PSS-29ZBSR, PSM-29ZBSR)
* QS-Zigbee-D02-TRIAC-2C-LN
* Sonoff ZBMINI Smart Switch (ZBMINI1)
* Feibit Light Switch (ZSW01)
* Dresden Electronics (FLS-PP3)
* Silvercrest (Lidl) Motion sensor (TZ1800)
* Silvercrest (Lidl) power strip (TS011F)
* Silvercrest (Lidl) smart button (TS004F)

### Radiator thermostats

All radiator thermostats that follow the ZigBee HVAC cluster specification.

Known to work:

* Eurotronic Spirit Zigbee Thermostat

### Door/window sensors

All door/window that follow the ZigBee IAS Zone cluster specification.

Known to work:

* Samotech Door sensor
* Sonoff Wireless Door/Window sensor
* eWeLink door/window sensor (DS01)

### Motion sensors

All motion that follow the ZigBee IAS Zone cluster specification.

Known to work:

* SILVERCREST motion sensor

