Lazymote
========

This is a TV control panel that I stuck on my coffee table, since I'm too lazy
to find the remote.

From left to right, the functions assigned to the buttons are:

- Input select (black)
- Volume down (white)
- Volume up (white)
- Power (red)
- Mute (by pressing both volume buttons)


It's incredibly simple: the hardware consists of an ATTiny45 microcontroller
(since I had some lying around), four buttons, a 3V coin cell battery, and an
infrared LED.

The board is stuck to the underside of my coffee table with double-sided
adhesive strips. An infrared LED and current-limiting resistor are affixed to
the opposite side of the table (nearest to the TV) and a small cable runs
underneath, connecting the LED to the board.

The firmware is written in C, and is only 340 bytes compiled. When no buttons
are pressed, the microcontroller is in a deep sleep mode where it only draws 0.1
µA. (That's 100 *nano*amps!) The coin cell should last several years before it
needs to be replaced!


Hardware
--------

The basis of nearly all consumer infrared control signals is a carrier wave
produced by an infrared LED blinking at 40kHz. By switching the carrier wave on
and off in pulses, a binary signal is encoded. The Lazymote was designed to
control a Sony KDL-52W4100 TV, so it speaks the 
[SIRC](http://www.sbprojects.com/knowledge/ir/sirc.php) protocol.

Each command begins with a 2400 microsecond burst of the carrier wave, followed
by a 600 microsecond space where the LED is turned off. Then, either 12, 15, or
20 data bits are sent. A "1" bit consists of a 1200 microsecond burst of the
carrier wave followed by a 600 microsecond space. A "0" bit consists of a 600
microsecond burst of the carrier wave followed by a 600 microsecond space. When
the final data bit is sent, the device waits at least 45 milliseconds before
sending another command.

Most "basic" TV commands use the 12-bit encoding, so that's the only one the
software currently supports. It consists of a 7-bit command number, transmitted
from LSB to MSB, followed by a 5-bit address, also transmitted LSB-to-MSB. All
signals intented for a TV use an address of 1 (transmitted as "10000").

The LED used for transmission has a peak wavelength of 940nm. Since the distance
between the coffee table and the television is only about a meter, the LED is
driven directly by the microcontroller, without an amplifying transistor.

Care was taken to make the board consume as little current as possible. When no
buttons are pressed, the microcontroller is in "power-down" sleep mode. All
peripherals and clocks are disabled. The analog comparator, ADC, and serial
interface are not used, so they are always disabled. When timing out pulses, the
microcontroller is in "idle" sleep, with all peripherals except Timer0 and
Timer1 disabled. (Timer0 generates the 40kHz square wave.) When simply waiting
(e.g. for switch debouncing), the microprocessor is in idle sleep with Timer0
disabled as well, and draws approx. 375 µA.


Software
--------

On startup, the microcontroller immediately disables unused peripherals; the
serial interface, ADC, and analog comparator. (The watchdog timer and brownout
detector are disabled in the fuse settings.)

The MCU is set to trigger an interrupt when any of the 4 input lines (i.e. the
buttons) change state, and immediately enters deep sleep. When a button is
pressed, the MCU wakes up, but doesn't know which button was pressed; so it
waits 10 milliseconds for the input lines to stabilize (switch debouncing) and
reads the input lines again.

The MCU then finds the appropriate command for the given input state (commands
can be assigned to simultaneous button presses if desired). If no command
matches the input state, the microcontroller waits 10 milliseconds and reads
the input pins again.

Otherwise, the code is transmitted, followed by a 45 millisecond delay. Timer0
is used in CTC mode to generate a (not quite) 40kHz square wave on pin PB0.

Control then loops back and the input state is checked again. If buttons are
still held down, more commands are sent. Otherwise, all peripherals are disabled
and the microcontroller enters deep sleep again.

In all cases where the MCU needs to wait a given number of milliseconds or
microseconds, it is placed in idle sleep mode and woken up by a Timer1 compare
match interrupt. Compared to the standard busy-waiting macros `_delay_ms()` and
`_delay_us()`, this method reduces current draw by several hundred microamps.


Bill of materials
-----------------

Most of these parts just came from whatever I had lying around:

- **Atmel ATtiny45 microcontroller**
    - An ATtiny25 would certainly be sufficient, and ideally I'd use one with
      the "V" suffix, which allows operation down to 1.8V.
    - The code should fit in an ATtiny13A, though it only has one timer, so it
      can't go into idle sleep and wake up after a delay while it's generating
      a square wave.
- **Four tactile switches and colored caps**
    - They're the big square kind. I think I got them from Newark.
- **LVIR3333 infrared LED**
    - [Jameco #106526](http://www.jameco.com/webapp/wcs/stores/servlet/Product_10001_10001_106526_-1).
- **CR2025 battery and holder**
    - The battery came out of an old remote I wasn't using.
- **Misc. wire and protoboard**
    - When building prototypes, I like to keep the top of the board clean by
      making all the connections on the underside using [enameled magnet
wire](https://www.flickr.com/photos/74hc595/11042466593/). However, I knew that I'd be putting adhesives on the underside, and
      didn't want any wires getting snagged. So I did all the wiring on the top
      surface using regular insulated wire.


Schematic
---------

A [gEDA](http://www.geda-project.org) schematic is included.

![Schematic](/remote_schematic.png?raw=true)


Uploading firmware
------------------

The AVR-GCC toolchain is required to compile the code; if you're a Mac user,
just install [CrossPack](http://www.obdev.at/products/crosspack/index.html).
Run `make hex` to build it.

You'll also need an in-system programmer, like Atmel's [AVRISP mkII](http://store.atmel.com/PartDetail.aspx?q=p:10500054#tc:description)
or the [USBtinyISP](https://www.adafruit.com/products/46). I've also heard you
can use [any old Arduino as an ISP programmer](http://arduino.cc/en/Tutorial/ArduinoISP).
If you're not using an AVRISP mkII, change the `AVRDUDE = ...` line in the Makefie
appropriately. Then, `make program` will burn the fuses and upload the
firmware!


