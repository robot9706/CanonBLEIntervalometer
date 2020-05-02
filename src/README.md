# ESP32 intervalometer

The ESP32-intervalometer is a simple intervalometer which uses BLE to trigger a Canon camera.

The project uses the following components:
* ESP32-WROOM
* SSD1306 based 128x64 OLED display
* * __SCL__ connected to __GPIO25__
* * __SDA__ connected to __GPIO26__
* A simple rotary encoder with a push button:
* * __CLK__ is connected to __GPIO35__
* * __D__ is connected to __GPIO32__
* * __Button__ is connected to __GPIO34__

The rotary encoder is used to navigate the menus and set the timer.

The menus are the following:
* __MainMenu__:
* * __Connect__: Connect to an already paired camera.
* * __Pair__: Pair with a camera.


* __Connect__ and __Pair__:
* * __Back__: Go back to the main menu
* * Or select a device and press the rotary encoder to connect or pair.


* __Camera main menu__:
* * __Trigger__: Trigger the camera once.
* * __Timer__: Goto the intervalometer/timer menu.
* * __Disconnect__: Disconnect from the camera.


* __Timer__:
* * __Set__: The number of seconds between each trigger.
* * __Back__: Back to the camera menu.
* * __Start/Stop__: Start and stop the timer.