// ===================================================================================
// NeoController for ATtiny13A + WS2812B, 4-button version
// Based on TinyNeoController by Stefan Wagner (wagiminator)
// https://github.com/wagiminator/ATtiny13-NeoController
// License: CC BY-SA 3.0
// ===================================================================================

#include <avr/io.h>
#include <util/delay.h>
#include <avr/eeprom.h>

// ===================================================================================
// Pin definitions
// ===================================================================================

#define NEO_PIN       PB3     // pin 2: WS2812B DATA
#define BTN_MODE      PB0     // pin 5: switch mode
#define BTN_BRIGHT    PB1     // pin 6: brightness
#define BTN_UP        PB2     // pin 7: increase mode parameter
#define BTN_DOWN      PB4     // pin 3: decrease mode parameter

#define BUTTON_MASK   ((1<<BTN_MODE) | (1<<BTN_BRIGHT) | (1<<BTN_UP) | (1<<BTN_DOWN))

// ===================================================================================
// NeoPixel parameters
// ===================================================================================

#define NEO_GRB               // WS2812B: green, red, blue
#define NEO_PIXELS    5       // number of pixels in the string

#define MODE_RAINBOW  0
#define MODE_SOLID    1
#define MODE_COUNT    2

#define SOLID_HUE_STEP 4

// ===================================================================================
// Global settings
// ===================================================================================

uint8_t mode          = MODE_RAINBOW;
uint8_t brightness    = 0;    // 0..7: 100%, 87.5%, 75%, 62.5%, 50%, 37.5%, 25%, 12.5%
uint8_t rainbowStart  = 0;    // current rainbow offset, 0..191
uint8_t rainbowSpeed  = 3;    // 1..8
uint8_t rainbowDense  = 16;   // hue difference between neighboring pixels
uint8_t solidHue      = 0;    // solid color, 0..191

// ===================================================================================
// Persistent settings (EEPROM)
// ===================================================================================

#define SETTINGS_MAGIC 0xA5

uint8_t EEMEM ee_magic;
uint8_t EEMEM ee_mode;
uint8_t EEMEM ee_brightness;
uint8_t EEMEM ee_rainbowSpeed;
uint8_t EEMEM ee_solidHue;

void SETTINGS_save(void) {
  // update_byte writes only when the value really changed
  eeprom_update_byte(&ee_mode,         mode);
  eeprom_update_byte(&ee_brightness,   brightness);
  eeprom_update_byte(&ee_rainbowSpeed, rainbowSpeed);
  eeprom_update_byte(&ee_solidHue,     solidHue);
  eeprom_update_byte(&ee_magic,        SETTINGS_MAGIC);
}

void SETTINGS_load(void) {
  if(eeprom_read_byte(&ee_magic) != SETTINGS_MAGIC) {
    SETTINGS_save();                    // first start: store defaults
    return;
  }

  uint8_t value;

  value = eeprom_read_byte(&ee_mode);
  if(value < MODE_COUNT) mode = value;

  value = eeprom_read_byte(&ee_brightness);
  if(value <= 7) brightness = value;

  value = eeprom_read_byte(&ee_rainbowSpeed);
  if(value >= 1 && value <= 8) rainbowSpeed = value;

  value = eeprom_read_byte(&ee_solidHue);
  if(value < 192) solidHue = value;
}

// ===================================================================================
// Neopixel implementation for 9.6 MHz MCU clock and 800 kHz pixels
// ===================================================================================

#define NEO_init()    DDRB |= (1<<NEO_PIN)
#define NEO_latch()   _delay_us(281)

// Send a byte to the pixel string
void NEO_sendByte(uint8_t byte) {
  for(uint8_t bit=8; bit; bit--) asm volatile(
    "sbi  %[port], %[pin]   \n\t"
    "sbrs %[byte], 7        \n\t"
    "cbi  %[port], %[pin]   \n\t"
    "rjmp .+0               \n\t"
    "add  %[byte], %[byte]  \n\t"
    "cbi  %[port], %[pin]   \n\t"
    ::
    [port]  "I" (_SFR_IO_ADDR(PORTB)),
    [pin]   "I" (NEO_PIN),
    [byte]  "r" (byte)
  );
}

// Apply one of 8 brightness levels using shifts only (no multiplication)
uint8_t NEO_applyBrightness(uint8_t value) {
  switch(brightness) {
    case 1: return value - (value >> 3);             // 87.5%
    case 2: return value - (value >> 2);             // 75%
    case 3: return (value >> 1) + (value >> 3);      // 62.5%
    case 4: return value >> 1;                       // 50%
    case 5: return (value >> 2) + (value >> 3);      // 37.5%
    case 6: return value >> 2;                       // 25%
    case 7: return value >> 3;                       // 12.5%
    default: return value;                           // 100%
  }
}

// Write color to a single pixel; brightness is applied to every mode here
void NEO_writeColor(uint8_t r, uint8_t g, uint8_t b) {
  r = NEO_applyBrightness(r);
  g = NEO_applyBrightness(g);
  b = NEO_applyBrightness(b);

  #if defined (NEO_GRB)
    NEO_sendByte(g); NEO_sendByte(r); NEO_sendByte(b);
  #elif defined (NEO_RGB)
    NEO_sendByte(r); NEO_sendByte(g); NEO_sendByte(b);
  #elif defined (NEO_RGBW)
    NEO_sendByte(r); NEO_sendByte(g); NEO_sendByte(b); NEO_sendByte(0);
  #else
    #error Wrong or missing NeoPixel type definition!
  #endif
}

// Write hue 0..191 to one pixel at full color range
void NEO_writeHue(uint8_t hue) {
  uint8_t phase = hue >> 6;
  uint8_t step  = (hue & 63) << 2;   // 0..252
  uint8_t nstep = 252 - step;

  switch(phase) {
    case 0: NEO_writeColor(nstep, step, 0);     break; // red -> green
    case 1: NEO_writeColor(0, nstep, step);     break; // green -> blue
    case 2: NEO_writeColor(step, 0, nstep);     break; // blue -> red
  }
}

// ===================================================================================
// Buttons
// Each button connects its pin to GND. Internal pull-ups are enabled.
// ===================================================================================

void BTN_init(void) {
  DDRB  &= (uint8_t)~BUTTON_MASK;
  PORTB |= BUTTON_MASK;
}

// Returns only newly pressed buttons (simple debounce, no auto-repeat)
uint8_t BTN_readPress(void) {
  static uint8_t last = 0;
  uint8_t now = (~PINB) & BUTTON_MASK;

  if(now != last) {
    _delay_ms(10);
    now = (~PINB) & BUTTON_MASK;
  }

  uint8_t pressed = now & (uint8_t)~last;
  last = now;
  return pressed;
}

void BTN_handle(void) {
  uint8_t pressed = BTN_readPress();
  uint8_t changed = 0;

  if(pressed & (1<<BTN_MODE)) {
    if(++mode >= MODE_COUNT) mode = 0;
    changed = 1;
  }

  if(pressed & (1<<BTN_BRIGHT)) {
    if(++brightness > 7) brightness = 0;
    changed = 1;
  }

  if(pressed & (1<<BTN_UP)) {
    if(mode == MODE_RAINBOW) {
      if(++rainbowSpeed > SOLID_HUE_STEP) rainbowSpeed = 1;
    }
    else {
      solidHue += SOLID_HUE_STEP;
      if(solidHue >= 192) solidHue -= 192;
    }
    changed = 1;
  }

  if(pressed & (1<<BTN_DOWN)) {
    if(mode == MODE_RAINBOW) {
      if(rainbowSpeed > 1) rainbowSpeed--;
      else rainbowSpeed = 8;
    }
    else {
      if(solidHue < SOLID_HUE_STEP) solidHue += 192;
      solidHue -= SOLID_HUE_STEP;
    }
    changed = 1;
  }

  if(changed) SETTINGS_save();
}

// Wait between frames while still polling the buttons
void waitFrame(uint8_t ms) {
  while(ms--) {
    BTN_handle();
    _delay_ms(1);
  }
}

// ===================================================================================
// Lighting modes
// ===================================================================================

void modeRainbow(void) {
  uint8_t current = rainbowStart;

  for(uint8_t i=NEO_PIXELS; i; i--) {
    NEO_writeHue(current);
    current += rainbowDense;
    if(current >= 192) current -= 192;
  }

  rainbowStart += rainbowSpeed;
  if(rainbowStart >= 192) rainbowStart -= 192;
}

void modeSolid(void) {
  for(uint8_t i=NEO_PIXELS; i; i--)
    NEO_writeHue(solidHue);
}

// ===================================================================================
// Main
// ===================================================================================

int main(void) {
  // Disable unused analog peripherals
  ACSR = (1<<ACD);
  PRR  = (1<<PRADC);

  SETTINGS_load();
  NEO_init();
  BTN_init();

  while(1) {
    switch(mode) {
      case MODE_RAINBOW: modeRainbow(); break;
      case MODE_SOLID:   modeSolid();   break;
    }

    NEO_latch();
    waitFrame(40);
  }
}
