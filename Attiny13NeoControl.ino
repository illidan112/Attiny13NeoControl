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
#define NEO_PIXELS    15       // number of pixels in the string

#define MODE_RAINBOW  0
#define MODE_SOLID    1
#define MODE_FIRE     2
#define MODE_COUNT    3

#define RAINBOW_DENSE 16

// ===================================================================================
// Global settings
// ===================================================================================

uint8_t mode          = MODE_RAINBOW;
uint8_t brightness    = 0;    // 0..7: 100%, 87.5%, 75%, 62.5%, 50%, 37.5%, 25%, 12.5%
uint8_t rainbowStart  = 0;    // current rainbow offset, 0..191
uint8_t rainbowSpeed  = 3;    // 1..8
uint8_t solidHue      = 0;    // solid color, 0..191
uint8_t fireHeat      = 96;   // fire color: green component, 48..144
uint8_t fireRnd       = 0xA5; // compact 8-bit pseudo-random state

// ===================================================================================
// Persistent settings (EEPROM)
// ===================================================================================

#define SETTINGS_MAGIC 0xA5

uint8_t EEMEM ee_magic;
uint8_t EEMEM ee_mode;
uint8_t EEMEM ee_brightness;
uint8_t EEMEM ee_rainbowSpeed;
uint8_t EEMEM ee_solidHue;
uint8_t EEMEM ee_fireHeat;

void SETTINGS_save(void) {
  eeprom_update_byte(&ee_mode,         mode);
  eeprom_update_byte(&ee_brightness,   brightness);
  eeprom_update_byte(&ee_rainbowSpeed, rainbowSpeed);
  eeprom_update_byte(&ee_solidHue,     solidHue);
  eeprom_update_byte(&ee_fireHeat,     fireHeat);
  eeprom_update_byte(&ee_magic,        SETTINGS_MAGIC);
}

void SETTINGS_load(void) {
  if(eeprom_read_byte(&ee_magic) != SETTINGS_MAGIC) {
    SETTINGS_save();
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

  value = eeprom_read_byte(&ee_fireHeat);
  if(value >= 48 && value <= 144) fireHeat = value;
}

// ===================================================================================
// Neopixel implementation for 9.6 MHz MCU clock and 800 kHz pixels
// ===================================================================================

#define NEO_init()    DDRB |= (1<<NEO_PIN)

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
    case 1: return value - (value >> 3);
    case 2: return value - (value >> 2);
    case 3: return (value >> 1) + (value >> 3);
    case 4: return value >> 1;
    case 5: return (value >> 2) + (value >> 3);
    case 6: return value >> 2;
    case 7: return value >> 3;
    default: return value;
  }
}

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

void NEO_writeHue(uint8_t hue) {
  uint8_t phase = hue >> 6;
  uint8_t step  = (hue & 63) << 2;
  uint8_t nstep = 252 - step;

  switch(phase) {
    case 0: NEO_writeColor(nstep, step, 0); break;
    case 1: NEO_writeColor(0, nstep, step); break;
    case 2: NEO_writeColor(step, 0, nstep); break;
  }
}

// ===================================================================================
// Buttons
// ===================================================================================

void BTN_init(void) {
  DDRB  &= (uint8_t)~BUTTON_MASK;
  PORTB |= BUTTON_MASK;
}

static inline uint8_t BTN_readPress(void) {
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
      if(++rainbowSpeed > 8) rainbowSpeed = 1;
    }
    else if(mode == MODE_SOLID) {
      solidHue += 8;
      if(solidHue >= 192) solidHue -= 192;
    }
    else {
      if(fireHeat < 144) fireHeat += 16;
      else fireHeat = 48;
    }
    changed = 1;
  }

  if(pressed & (1<<BTN_DOWN)) {
    if(mode == MODE_RAINBOW) {
      if(rainbowSpeed > 1) rainbowSpeed--;
      else rainbowSpeed = 8;
    }
    else if(mode == MODE_SOLID) {
      if(solidHue < 8) solidHue += 192;
      solidHue -= 8;
    }
    else {
      if(fireHeat > 48) fireHeat -= 16;
      else fireHeat = 144;
    }
    changed = 1;
  }

  if(changed) SETTINGS_save();
}

void waitFrame(uint8_t ms) {
  while(ms--) {
    BTN_handle();
    _delay_ms(1);
  }
}

// ===================================================================================
// Lighting modes
// ===================================================================================

static inline void modeRainbow(void) {
  uint8_t current = rainbowStart;

  for(uint8_t i=NEO_PIXELS; i; i--) {
    NEO_writeHue(current);
    current += RAINBOW_DENSE;
    if(current >= 192) current -= 192;
  }

  rainbowStart += rainbowSpeed;
  if(rainbowStart >= 192) rainbowStart -= 192;
}

static inline void modeSolid(void) {
  for(uint8_t i=NEO_PIXELS; i; i--)
    NEO_writeHue(solidHue);
}

// Compact 8-bit maximal-length LFSR.
// Much cheaper on ATtiny13A than the previous 16-bit implementation.
static inline uint8_t FIRE_random(void) {
  fireRnd = (fireRnd >> 1) ^ ((uint8_t)-(fireRnd & 1) & 0xB8);
  return fireRnd;
}

void modeFire(void) {
  for(uint8_t i=NEO_PIXELS; i; i--) {
    uint8_t flicker = FIRE_random() & 31;
    NEO_writeColor(255 - (flicker >> 1), fireHeat - flicker, 0);
  }
}

// ===================================================================================
// Main
// ===================================================================================

int main(void) {
  ACSR = (1<<ACD);
  PRR  = (1<<PRADC);

  SETTINGS_load();
  NEO_init();
  BTN_init();

  while(1) {
    switch(mode) {
      case MODE_RAINBOW: modeRainbow(); break;
      case MODE_SOLID:   modeSolid();   break;
      case MODE_FIRE:    modeFire();    break;
    }

    // waitFrame(40) keeps DATA LOW far longer than the WS2812 reset/latch time,
    // so a separate NEO_latch() delay is unnecessary.
    waitFrame(40);
  }
}
