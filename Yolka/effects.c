/*
 * effects.c
 *
 * Модуль визуальных эффектов, проект "Ёлка"
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 

#include <avr/io.h>
#include <avr/pgmspace.h>
#include "effects.h"
#include "Yolka.h"

MemoryBlock mem;

// Таблица четверть периода синуса
// sin_table[i] = round(sin(i / 256 * Pi / 2) * 65536)
const static PROGMEM uint16_t sin_table[] = {
    0, 402, 804, 1206, 1608, 2010, 2412, 2814, 3216, 3617, 4019, 4420, 4821, 5222, 5623, 6023,
    6424, 6824, 7224, 7623, 8022, 8421, 8820, 9218, 9616, 10014, 10411, 10808, 11204, 11600, 11996, 12391,
    12785, 13180, 13573, 13966, 14359, 14751, 15143, 15534, 15924, 16314, 16703, 17091, 17479, 17867, 18253, 18639,
    19024, 19409, 19792, 20175, 20557, 20939, 21320, 21699, 22078, 22457, 22834, 23210, 23586, 23961, 24335, 24708,
    25080, 25451, 25821, 26190, 26558, 26925, 27291, 27656, 28020, 28383, 28745, 29106, 29466, 29824, 30182, 30538,
    30893, 31248, 31600, 31952, 32303, 32652, 33000, 33347, 33692, 34037, 34380, 34721, 35062, 35401, 35738, 36075,
    36410, 36744, 37076, 37407, 37736, 38064, 38391, 38716, 39040, 39362, 39683, 40002, 40320, 40636, 40951, 41264,
    41576, 41886, 42194, 42501, 42806, 43110, 43412, 43713, 44011, 44308, 44604, 44898, 45190, 45480, 45769, 46056,
    46341, 46624, 46906, 47186, 47464, 47741, 48015, 48288, 48559, 48828, 49095, 49361, 49624, 49886, 50146, 50404,
    50660, 50914, 51166, 51417, 51665, 51911, 52156, 52398, 52639, 52878, 53114, 53349, 53581, 53812, 54040, 54267,
    54491, 54714, 54934, 55152, 55368, 55582, 55794, 56004, 56212, 56418, 56621, 56823, 57022, 57219, 57414, 57607,
    57798, 57986, 58172, 58356, 58538, 58718, 58896, 59071, 59244, 59415, 59583, 59750, 59914, 60075, 60235, 60392,
    60547, 60700, 60851, 60999, 61145, 61288, 61429, 61568, 61705, 61839, 61971, 62101, 62228, 62353, 62476, 62596,
    62714, 62830, 62943, 63054, 63162, 63268, 63372, 63473, 63572, 63668, 63763, 63854, 63944, 64031, 64115, 64197,
    64277, 64354, 64429, 64501, 64571, 64639, 64704, 64766, 64827, 64884, 64940, 64993, 65043, 65091, 65137, 65180,
    65220, 65259, 65294, 65328, 65358, 65387, 65413, 65436, 65457, 65476, 65492, 65505, 65516, 65525, 65531, 65535
 };
  
static int16_t sin_t(uint16_t period, int16_t scaler) {
  // эквивалентно return round(sin(2.0 * M_PI * (period / 65536)) * scaler)
  uint16_t tabpos = (period >> 6) & 511;
  uint8_t tabsub = period & 63;
  if (tabpos == 256) return (period & 0x8000) ? -scaler : scaler;
  if (tabpos > 256) {
    tabpos = 512 - tabpos;
    tabsub = 64 - tabsub;
  }    
  uint16_t tv = pgm_read_word(&sin_table[tabpos]);
  if (tabsub > 0) {
    if (tabpos < 255) {
      // Интерполируем между двумя соседними табличными значениями.
      // tabsub всегда меньше 64, шаг таблицы выбран так, что произведение tabsub на разницу двух ячеек укладывается в 16 бит
      tv += ((pgm_read_word(&sin_table[tabpos + 1]) - tv) * tabsub + 32) >> 6;
    } else {
      // на позиции 255 значение в таблице 65535, поэтому интерполяция тут ни к чему
      // оставим 65535, если мы ближе к 255й позиции, или будем считать 65536 (т.е. множитель единица) если ближе к 256й
      if (tabsub >= 32) return (period & 0x8000) ? -scaler : scaler;
    }
  }
  int16_t res = ((uint32_t)tv * scaler + 32768) >> 16;
  return (period & 0x8000) ? -res : res;
}

void hb(uint8_t h, uint8_t b, led_rec * led) {
  uint16_t th = h * 6;
  uint8_t a = ((uint8_t)th * b + 128) >> 8;
  switch (th >> 8) {
    case 0: led->r = b; led->g = a; led->b = 0; break;
    case 1: led->r = b - a; led->g = b; led->b = 0; break;
    case 2: led->r = 0; led->g = b; led->b = a; break;
    case 3: led->r = 0; led->g = b - a; led->b = b; break;
    case 4: led->r = a; led->g = 0; led->b = b; break;
    default: led->r = b; led->g = 0; led->b = b - a; 
  }
}

void hsb(uint8_t h, uint8_t s, uint8_t b, led_rec * led) {
  uint16_t th = h * 6;
  uint8_t z = ((255 - s) * b + 128) >> 8; // Нулевая точка
  uint8_t ab = b - z;
  uint8_t a = (((uint8_t)th * ab + 128) >> 8);
  switch (th >> 8) {
    case 0: led->r = b; led->g = z + a; led->b = z; break;
    case 1: led->r = b - a; led->g = b; led->b = z; break;
    case 2: led->r = z; led->g = b; led->b = z + a; break;
    case 3: led->r = z; led->g = b - a; led->b = b; break;
    case 4: led->r = z + a; led->g = z; led->b = b; break;
    default: led->r = b; led->g = z; led->b = b - a; 
  }
}

void blur() {
  led_rec * led = &mem.leds[0];
  uint8_t pr = led->r;
  uint8_t pg = led->g;
  uint8_t pb = led->b;
  led->r = (pr * 7 + led[1].r) >> 3;
  led->g = (pg * 7 + led[1].g) >> 3;
  led->b = (pb * 7 + led[1].b) >> 3;
  led++;
  for (uint16_t i = 2; i < led_num; i++) {
    uint8_t nr = led->r;
    uint8_t ng = led->g;
    uint8_t nb = led->b;
    led->r = (pr + nr * 14 + led[1].r) >> 4;
    led->g = (pg + ng * 14 + led[1].g) >> 4;
    led->b = (pb + nb * 14 + led[1].b) >> 4;
    pr = nr;
    pg = ng;
    pb = nb;
    led++;
  }
  led->r = (pr + led->r * 7) >> 3;
  led->g = (pg + led->g * 7) >> 3;
  led->b = (pb + led->b * 7) >> 3;
}

void blur2(uint8_t fade) {
  led_rec * led = &mem.leds[0];
  uint8_t pr = led->r;
  uint8_t pg = led->g;
  uint8_t pb = led->b;
  int8_t t;
  t = (pr + led[1].r) >> 1;
  led->r = (t < fade) ? 0 : (t - fade);
  t = (pg + led[1].g) >> 1;
  led->g = (t < fade) ? 0 : (t - fade);
  t = (pb + led[1].b) >> 1;
  led->b = (t < fade) ? 0 : (t - fade);
  led++;
  for (uint16_t i = 2; i < led_num; i++) {
    uint8_t nr = led->r;
    uint8_t ng = led->g;
    uint8_t nb = led->b;
    t = (pr + nr * 2 + led[1].r) >> 2;
    led->r = (t < fade) ? 0 : (t - fade);
    t = (pg + ng * 2 + led[1].g) >> 2;
    led->g = (t < fade) ? 0 : (t - fade);
    t = (pb + nb * 2 + led[1].b) >> 2;
    led->b = (t < fade) ? 0 : (t - fade);
    pr = nr;
    pg = ng;
    pb = nb;
    led++;
  }
  t = (pr + led->r) >> 1;
  led->r = (t < fade) ? 0 : (t - fade);
  t = (pg + led->g) >> 1;
  led->g = (t < fade) ? 0 : (t - fade);
  t = (pb + led->b) >> 1;
  led->b = (t < fade) ? 0 : (t - fade);
}

void clear() {
  uint8_t * ptr = (uint8_t*)&mem.leds[0];
  for (uint16_t i = led_num * 3; i; i--) {
    *(ptr++) = 0;
  }
}  
  
void hbover(uint8_t h, uint16_t b, led_rec * led) {
  uint8_t rb;
  uint8_t rs;
  if (b > 255) {
    rb = 255;
    rs = (b > 510) ? 0 : 510 - b;
  } else {
    rb = b;
    rs = 255;
  }
  hsb(h, rs, rb, led);
}

void wave() {
  uint16_t p = random16();
  uint8_t fp = 0;
  do {
    uint8_t ip = p >> 8;
    for (uint16_t i = 0; i < led_num; i++) {
      uint8_t pha = (i < fp) ? (i + 150 - fp) : (i - fp);
      while (pha >= 150) pha -= 150;
      if (pha < 25) pha = 0; else pha -= 25;
      hbover(ip, pha * 3, &mem.leds[i]);
//      hbover(p, i * 10, &mem.leds[i]);
      ip += 7;
    }
    p += 61;
    fp++;
    if (fp >= 150) fp = 0;
  } while (sync_out(&mem.leds));
}

void sparkles() {
  uint16_t time_to_sparkle = 0;
  do {
    led_rec * led = &mem.leds[0];
    for (uint16_t i = 0; i < led_num; i++) {
/*      led->r >>= 1;
      led->g >>= 1;
      led->b >>= 1;*/
      led->r = 0;
      led->g = 0;
      led->b = 0;
      led++;
    }
    while (time_to_sparkle <= led_num) {
      led = &mem.leds[randomw(led_num)];
      led->r = 255 - (random8() >> 3);
      led->g = 255 - (random8() >> 3);
      led->b = 255 - (random8() >> 3);
      time_to_sparkle += randomw(250);
    }
    time_to_sparkle -= led_num;
  } while (sync_out(&mem.leds));
}

void rain() {
  uint8_t h = 0;
  uint16_t b = 0;
  uint8_t c = 0;
  do {
    led_rec * led = &mem.leds[0];
    for (uint16_t i = 1; i < led_num; i++) {
      led->r = led[1].r;
      led->g = led[1].g;
      led->b = led[1].b;
      led++;
    }
    if (!c) {
      h = random8();
      b = 500;
      c = random(35) + 3;
    } else {
      b = (b * 3) >> 2;
      h += 7;
      c--;
    }
    hbover(h, b, led);
  } while (sync_out(&mem.leds));
}

void drops() {
  clear();
  uint16_t time_to_drop = 0;
  do {
    blur();
    while (time_to_drop <= led_num) {
      hb(random8(), 255, &mem.leds[randomw(led_num)]);
      time_to_drop += randomw(700);
    }
    time_to_drop -= led_num;
  } while (sync_out(&mem.leds));
}

void twist() {
  uint16_t alpha = random16();
  uint16_t p = random16();
  uint16_t hstepmulfor = 0;
  uint16_t hstepmul = 0;
  do {
    uint16_t h = p;
    if (hstepmulfor != led_num) {
      hstepmul = 50000 / (led_num >> 3);
      hstepmulfor = led_num;
    }
    uint16_t hstep = sin_t(alpha, hstepmul);
    for (uint16_t i = led_num; i; i--) {
      hb(h >> 8, 255, &mem.leds[i - 1]);
//      hbover(p, i * 10, &mem.leds[i]);
      h += hstep;
    }
    p += 97;
    alpha += 61;
  } while (sync_out(&mem.leds));
}

void meteors() {
  typedef struct {
    int16_t phase;
    int16_t start;
    int8_t direction;
    uint16_t phase_dec;
    uint16_t first_color;
    uint16_t size;
    uint16_t color_twist;
  } Meteor;
  Meteor mets[5];
  led_rec led;
  uint8_t time_to_met = 0;
  for (uint8_t i = 0; i < (sizeof(mets) / sizeof(mets[0])); i++) {
    mets[i].phase = 0;
  }   
  do {
    clear();
    uint8_t free_met = 255;
    for (uint8_t i = 0; i < (sizeof(mets) / sizeof(mets[0])); i++) {
      if (mets[i].phase == 0) {
        free_met = i;
      } else {
        int16_t ln = mets[i].start;
        int8_t dir = mets[i].direction;
        uint16_t sz = mets[i].size;
        int16_t ph = mets[i].phase;
        uint16_t phdec = mets[i].phase_dec;
        uint16_t col = mets[i].first_color;
        uint16_t coltw = mets[i].color_twist;
        do {
          if ((ln >= 0) && (ln < led_num)) {
            int16_t bright = 512 - (ph >> 5);
            if (bright > 0) {
              hbover(col >> 8, bright, &led);
              if (led.r > mem.leds[ln].r) mem.leds[ln].r = led.r;
              if (led.g > mem.leds[ln].g) mem.leds[ln].g = led.g;
              if (led.b > mem.leds[ln].b) mem.leds[ln].b = led.b;
            }
          }
          col += coltw;
          ln += dir;
          sz--;
          ph -= phdec;
        } while ((ph > 0) && (sz > 0));
        if (ph > 16384) {
          mets[i].phase = 0;
          free_met = i;
        } else {
          mets[i].phase += 256;
        }
      }
    }
    if (time_to_met == 0) {
      if (free_met < 255) {
        mets[free_met].phase = 256;
        mets[free_met].direction = (random8() & 1) ? 1 : -1;
        mets[free_met].size = random(led_num >> 1) + (led_num >> 3);
        mets[free_met].start = randomw(led_num - mets[free_met].size);
        if (mets[free_met].direction < 0) mets[free_met].start += mets[free_met].size;
        mets[free_met].phase_dec = (8192 + randomw(16384)) / led_num;
        mets[free_met].first_color = random16();
        mets[free_met].color_twist = random16();
        time_to_met = random(50);
      }
    } else {
      time_to_met--;
    }
  } while (sync_out(&mem.leds));
}

void interference() {
  uint16_t startclr = random16();
  int16_t stepclr = randomw(8192) + 512;
  if (random8() & 1) stepclr = -stepclr;
  int16_t stepstartclr = randomw(256) + 64;
  if (random8() & 1) stepstartclr = -stepstartclr;
  
  uint16_t ampphase = random16();
  int16_t ampphstep = randomw(8192) + 4096;
  if (random8() & 1) ampphstep = -ampphstep;
  uint16_t ampphdira = random16();
  
  uint16_t levphase = random16();
  uint16_t levphasestep = randomw(256) + 256;
  
  do {
    uint16_t ampph = ampphase;
    ampphase += sin_t(ampphdira, 2048);
    ampphdira += 79;
    
    uint16_t level = sin_t(levphase, 96) + 160;
    levphase += levphasestep;
    
    
    uint16_t clr = startclr;
    startclr += stepstartclr;
    for(uint16_t i = 0; i < led_num; i++) {
      hbover(clr >> 8, 256 + sin_t(ampph, level), &mem.leds[i]);
      clr += stepclr;
      ampph += ampphstep;
    }
  } while (sync_out(&mem.leds));
  
}

PROGMEM const char str_wave[] = "Wave";
PROGMEM const char str_sparkles[] = "Sparkles";
PROGMEM const char str_rain[] = "Rain";
PROGMEM const char str_drops[] = "Drops";
PROGMEM const char str_twist[] = "Twist";
PROGMEM const char str_meteors[] = "Meteors";
PROGMEM const char str_interference[] = "Interference";

PROGMEM const EffectDesc effects_list[] = {
  {str_wave, wave},
  {str_sparkles, sparkles},
  {str_rain, rain},
  {str_drops, drops},
  {str_twist, twist},
  {str_meteors, meteors},
  {str_interference, interference},
};  

const uint8_t num_effects = sizeof(effects_list) / sizeof(EffectDesc);


