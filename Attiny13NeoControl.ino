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

#define NEO_GRB                // WS2812B: green, red, blue
#define NEO_PIXELS    15       // number of pixels in the string

#define MODE_RAINBOW  0
#define MODE_SOLID    1
#define MODE_FIRE     2
#define MODE_COUNT    3

#define RAINBOW_DENSE 16

// ===================================================================================
// Global settings
// ===================================================================================

struct Settings {
  uint8_t mode;
  uint8_t brightness;    // 0..7: 100%, 87.5%, 75%, 62.5%, 50%, 37.5%, 25%, 12.5%
  uint8_t rainbowSpeed;  // 1..8
  uint8_t solidHue;      // solid color, 0..191
  uint8_t fireHeat;      // fire color: green component, 48..144
};

Settings settings = { MODE_SOLID, 0, 3, 0, 96 };
uint8_t rainbowStart  = 0;    // current rainbow offset, 0..191
uint8_t fireRnd       = 0xA5; // compact 8-bit pseudo-random state

// ===================================================================================
// Persistent settings (EEPROM)
// ===================================================================================

#define SETTINGS_MAGIC 0xA6

uint8_t EEMEM ee_magic;
Settings EEMEM ee_settings;

void SETTINGS_save(void) {
  const uint8_t* source = (const uint8_t*)&settings;
  uint8_t* target = (uint8_t*)&ee_settings;

  for(uint8_t index = 0; index < sizeof(settings); ++index)
    eeprom_update_byte(target + index, source[index]);

  eeprom_update_byte(&ee_magic,        SETTINGS_MAGIC);
}

void SETTINGS_load(void) {
  if(eeprom_read_byte(&ee_magic) != SETTINGS_MAGIC) {
    SETTINGS_save();
    return;
  }

  uint8_t value;

  value = eeprom_read_byte(&ee_settings.mode);
  if(value < MODE_COUNT) settings.mode = value;

  value = eeprom_read_byte(&ee_settings.brightness);
  if(value <= 7) settings.brightness = value;

  value = eeprom_read_byte(&ee_settings.rainbowSpeed);
  if((uint8_t)(value - 1) < 8) settings.rainbowSpeed = value;

  value = eeprom_read_byte(&ee_settings.solidHue);
  if(value < 192) settings.solidHue = value;

  value = eeprom_read_byte(&ee_settings.fireHeat);
  // if(value >= 48 && value <= 144) settings.fireHeat = value;
  if((uint8_t)(value - 48) < 97) settings.fireHeat = value;
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

// Apply one of 8 brightness levels using shifts only
uint8_t NEO_applyBrightness(uint8_t value) {
  uint8_t half    = value >> 1;
  uint8_t quarter = half >> 1;
  uint8_t eighth  = quarter >> 1;

  switch(settings.brightness) {
    case 1: return value - eighth;
    case 2: return value - quarter;
    case 3: return half + eighth;
    case 4: return half;
    case 5: return quarter + eighth;
    case 6: return quarter;
    case 7: return eighth;
    default: return value;
  }
}

void NEO_writeColor(uint8_t r, uint8_t g, uint8_t b) {

#if defined(NEO_GRB)
  NEO_sendByte(NEO_applyBrightness(g));
  NEO_sendByte(NEO_applyBrightness(r));
  NEO_sendByte(NEO_applyBrightness(b));
#elif defined(NEO_RGB)
  NEO_sendByte(NEO_applyBrightness(r));
  NEO_sendByte(NEO_applyBrightness(g));
  NEO_sendByte(NEO_applyBrightness(b));
#elif defined(NEO_RGBW)
  NEO_sendByte(NEO_applyBrightness(r));
  NEO_sendByte(NEO_applyBrightness(g));
  NEO_sendByte(NEO_applyBrightness(b));
  NEO_sendByte(0);
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
  if(!pressed) return;

  if(pressed & (1<<BTN_MODE)) {
    if(++settings.mode >= MODE_COUNT) settings.mode = 0;
  }

  if(pressed & (1<<BTN_BRIGHT)) {
    settings.brightness = (settings.brightness + 1) & 7;
    
  }

  if(pressed & (1<<BTN_UP)) {
    if(settings.mode == MODE_RAINBOW) {
      settings.rainbowSpeed = (settings.rainbowSpeed & 7) + 1;
    }
    else if(settings.mode == MODE_SOLID) {
      settings.solidHue += 8;
      if(settings.solidHue >= 192) settings.solidHue -= 192;
    }
    else {
      if(settings.fireHeat < 144) settings.fireHeat += 16;
      else settings.fireHeat = 48;
    }
    
  }

  if(pressed & (1<<BTN_DOWN)) {
    if(settings.mode == MODE_RAINBOW) {
      settings.rainbowSpeed = ((settings.rainbowSpeed + 6) & 7) + 1;
    }
    else if(settings.mode == MODE_SOLID) {
      if(settings.solidHue < 8) settings.solidHue += 192;
      settings.solidHue -= 8;
    }
    else {
      if(settings.fireHeat > 48) settings.fireHeat -= 16;
      else settings.fireHeat = 144;
    }
    
  }

  SETTINGS_save();
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

  rainbowStart += settings.rainbowSpeed;
  if(rainbowStart >= 192) rainbowStart -= 192;
}

static inline void modeSolid(void) {
  for(uint8_t i=NEO_PIXELS; i; i--)
    NEO_writeHue(settings.solidHue);
}

// Compact 8-bit maximal-length LFSR.
static inline uint8_t FIRE_random(void) {
  fireRnd = (fireRnd >> 1) ^ ((uint8_t)-(fireRnd & 1) & 0xB8);
  return fireRnd;
}

void modeFire(void) {
  for(uint8_t i=NEO_PIXELS; i; i--) {
    uint8_t flicker = FIRE_random() & 31;
    NEO_writeColor(255 - (flicker >> 1), settings.fireHeat - flicker, 0);
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
    switch(settings.mode) {
      case MODE_RAINBOW: modeRainbow(); break;
      case MODE_SOLID:   modeSolid();   break;
      case MODE_FIRE:    modeFire();    break;
    }

    // waitFrame(40) keeps DATA LOW far longer than the WS2812 reset/latch time,
    // so a separate latch() delay is unnecessary.
    waitFrame(40);
  }
}
