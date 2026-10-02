# Attiny13NeoControl

ATtiny13A controller for WS2812B LEDs with four buttons, three lighting modes,
eight brightness levels and persistent EEPROM settings. No NeoPixel library required.

Connect buttons to GND; internal pull-ups are enabled. Use a shared ground and
a suitable LED power supply. Do not press buttons during ISP programming.

## Controls

- **MODE:** cycle through rainbow, solid color and fire.
- **BRIGHT:** cycle through eight brightness levels, approximately 100% to 12.5%.
- **UP / DOWN:** adjust rainbow speed, solid hue or fire color, depending on the mode.

Settings wrap at their limits and are saved after button presses. Holding a button
does not repeat the action. The default is solid red at full brightness.

## Build

1. Install [MicroCore](https://github.com/MCUdude/MicroCore) in Arduino IDE.
2. Open [Attiny13NeoControl.ino](Attiny13NeoControl.ino).
3. Select ATtiny13A, internal 9.6 MHz, no bootloader; configure matching fuses via ISP.
4. Upload using an ISP programmer. Preserve EEPROM to retain settings across uploads.

WS2812 timing requires an actual 9.6 MHz clock without the divide-by-eight prescaler.
Change `NEO_PIXELS` to adjust the LED count; the default channel order is GRB.

## Credits

Based on [ATtiny13-NeoController](https://github.com/wagiminator/ATtiny13-NeoController)
by Stefan Wagner (wagiminator). License: [CC BY-SA 3.0](https://creativecommons.org/licenses/by-sa/3.0/).