# ESP32 intervalometer

The "src" contains the code for an ESP32-IDF based intervalometer.

# Canon BLE protocol

## Overview

There are 4 services in a Canon camera, which can be used to control it:
* __00010000-0000-1000-0000-d8492fffa821__: Used for pairing.
* * __00010005-0000-1000-0000-d8492fffa821__: Unknown
* * __00010006-0000-1000-0000-d8492fffa821__: Pairing commands.
* * __0001000a-0000-1000-0000-d8492fffa821__: Pairing data.
* * __0001000b-0000-1000-0000-d8492fffa821__: Unknown
* __0002000-0000-1000-0000-d8492fffa821__: Unknown. Seems to contain information about the camera.
* __0003000-0000-1000-0000-d8492fffa821__: Used for triggering the camera.
* * __00030010-0000-1000-0000-d8492fffa821__: Seems to configure the trigger state notifications.
* * __00030030-0000-1000-0000-d8492fffa821__: Trigger.
* * __00030031-0000-1000-0000-d8492fffa821__: Trigger state notifications.
* __0004000-0000-1000-0000-d8492fffa821__: Unknown.

The camera only connects to paired devices so in order to communicate with the camera the pairing process needs to be done first. Also only BLE bonded devices can communicate.

___Pairing commands___:
* 0x01: Confirm.
* 0x02: Fail.
* 0x03: Set UUID.
* 0x04: Set nickname.
* 0x05: Set type:
* * Types:
* * 0x01: iOS
* * 0x02: Android
* * 0x03: Remocon

Pairing commands start with the command byte then the data:
* UUID: __[__ 0x03, 128bits of UUID __]__
* Nickname: __[__ 0x04, ASCII name __]__
* Type: __[__ 0x05, 0x03 __]__ (Android)

___Pairing indication results___:
* 0x01: Request (does not seem to be used)
* 0x02: OK
* 0x03: *"NG"* (?)

### Pairing process

The pairing process uses Service 1, the pairing process looks as follows:
1) Connect to the camera and find all services and characteristics.
2) Send the __Set nickname command__ *to the pairing command characteristic* with *security* and bonding enabled, this should fail because the devices are not bonded.
3) Wait for the bonding process to finish.
4) Send the __Set name command__ again without any security requirements.
5) Enable *indications* for the *pairing command* characteristic.
6) Wait for a callback, if the pairing is accepted continue, otherwise abort.
7) Send the __Set name command__, __Set platform command__ and the __Confirm command__ to the *pairing data* characteristic.
8) The devices are now paired.

### Connection process

After a device is paired with a camera and wants to connect it needs to do the following:
1) Connect to the camera and find all services and characteristics.
2) Write any characteristic or characteristic descriptor with *security*, this should fail and initiate the bonding process.
3) After the bonding is finished commands can be sent to the camera.

### Triggering

1) Write __0x0001__ to the *trigger* characteristic. This triggers the camera.
2) Write __0x0002__ to the *trigger* characteristic. This finishes the trigger process and shows the result.


(Tested and works with Canon EOS M50)
