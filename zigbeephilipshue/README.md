# ZigBee Philips Hue

This plugin adds support for Philips Hue ZigBee devices using a ZigBee network hosted by nymea. It does not require a Hue bridge but a ZigBee adapter on the nymea system instead.

### Hue motion sensor

The [Hue motion sensor](https://www.philips-hue.com/en-us/p/hue-motion-sensor/046677570972) is fully supported. The time period for the present state can be specied in the thing setting.

**Pairing instructions**: Open the ZigBee network for joining. Press the setup button for 5 seconds until the LED start blinking in different colors.


### Hue outdoor sensor
The [Hue outdoor sensor](https://www.philips-hue.com/en-us/p/hue-outdoor-sensor/046677541736) is fully supported. The time period for the present state can be specied in the thing setting.


### Hue dimmer switch

The [Hue dimmer switch](https://www.philips-hue.com/en-us/p/hue-dimmer-switch--latest-model-/046677562779  https://www.philips-hue.com/en-us/p/hue-dimmer-switch--latest-model-/046677562779) (including the older model version) is fully supported.

**Pairing instructions**: Open the ZigBee network for joining. Press the setup button for 5 seconds until the LED start blinking in different colors.

### Hue smart button

The [Hue Smart button](https://www.philips-hue.com/en-us/p/hue-smart-button/046677553715) is fully supported.

**Pairing instructions**: Open the ZigBee network for joining. Press the setup button behind the back cap for 5 seconds until the LED start blinking in different colors.

### Hue wall switch module

The [Hue Smart button](https://www.philips-hue.com/en-us/p/hue-philips-hue-wall-switch-module/046677571160) is fully supported.

**Pairing instructions**: Open the ZigBee network for joining. Press the setup button on the back for 5 seconds until the LED start blinking in different colors.

### Hue lights

Most of the lights and lamps from Philips Hue should be handled in a generic way by the `nymea-plugin-zigbee-generic-lights` plugin. There are 2 methods for bringing a Hue light / bulb into the nymea ZigBee network.

1. Use a [Hue dimmer switch](https://www.philips-hue.com/en-us/p/hue-dimmer-switch--latest-model-/046677562779) (most reliable):

    * Open the ZigBee network in nymea for allowing new devices to join the network.
    * Switch the lamp off for a few seconds by cutting the power and turn it back on again.
    * On the Hue dimmer switch, press and hold the `On` and the `Off` keys (on newer models the `Hue` and the `Power` key) simultaneously.
    * Hold the switch as close as possible to the lamp while keep pressing both buttons.
    * Keep holding and pressing the buttons until the lamp has blinked 3 times and fades back to the normal mode.
    * Now release the buttons on the remote, the lamp should be resetted and start joining the nymea ZigBee network.

2. Remove the lamp from an existing Hue Bridge setup:

    * Open the ZigBee network in nymea for allowing new devices to join the network.
    * Remove the lamp from the Hue Bridge using the official Hue App or nymea.
    * The lamp should now be resetted and start joining the nymea ZigBee network.

3. Reset the lamp by power cutting several times:

    * Open the ZigBee network in nymea for allowing new devices to join the network.
    * Cut the lamps power supply for a second and then turn it back on. (Either wia a wall switch that cuts the circuit or by unscrewing the bulb)
    * Repeat the previous step seven times.
    * The lamp should now be resetted and start joinging the nymea ZigBee network.

## Requirements

* A compatible ZigBee controller and a running ZigBee network in nymea. You can find more information about supported controllers and ZigBee network configurations [here](https://nymea.io/documentation/users/usage/configuration#zigbee).

## More information

 [Philips hue](http://www2.meethue.com/) 
