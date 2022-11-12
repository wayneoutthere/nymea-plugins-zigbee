# ZigBee IKEA TRÅDFRI

This plugin allows to interact with IKEA TRÅDFRI ZigBee devices using a native ZigBee network controller in nymea.

## Supported Things

* TRADFRI bulb E27 CWS opal 600lm (color light)
* TRADFRI bulb E27 WS clear 806lm (color temperature light)
* [Wireless dimmer](https://www.ikea.com/us/en/p/tradfri-wireless-dimmer-white-10408598/)*
* [Shortcut Button](https://www.ikea.com/us/en/p/tradfri-shortcut-button-white-20356382/)*
* [Remote control](https://zigbee.blakadder.com/Ikea_E1810.html)**May be discontinued.**
* [Symfonisk sound remote](https://www.ikea.com/us/en/p/symfonisk-sound-remote-white-20370482/)
* [Motion sensor](https://www.ikea.com/us/en/p/tradfri-wireless-motion-sensor-white-60377655/)
* [Signal repeater](https://www.ikea.com/us/en/p/tradfri-signal-repeater-30400407/) -**DISCONTINUED**
* [Control outlet](https://www.ikea.com/us/en/p/tradfri-wireless-control-outlet-30356169/)
* [Starkvind air purifier](https://www.ikea.com/de/de/new/neu-starkvind-luftreiniger-pub4c72a520), [USA English page](https://www.ikea.com/us/en/p/starkvind-air-purifier-black-smart-40501967/)
 
(*) Known working device firmware version: `2.2.008`
(**) Known working device firmware version: `2.3.014`

## Pairing instructions

### Lights
Open the ZigBee network for joining. Switch the light off and on 6 times in a 1 second rythm. Once the light start flashing/dimming, the pairing process has been started successfully and the lamp will join the nymea ZigBee network.

### Remotes/Sockets/Other
Open the ZigBee network for joining. Click the connect button 4 times within 5 seconds.

