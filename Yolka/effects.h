/*
 * effects.h
 *
 * Модуль визуальных эффектов, проект "Ёлка"
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 


#ifndef EFFECTS_H_
#define EFFECTS_H_

#include <avr/pgmspace.h>
#include "Yolka.h"

typedef struct {
  PGM_VOID_P effect_name;
  void(*effect)(void);
} EffectDesc;

typedef union {
  led_rec leds[MAX_LED_COUNT];
} MemoryBlock;

extern const PROGMEM EffectDesc effects_list[];
extern const uint8_t num_effects;

extern MemoryBlock mem;

#endif /* EFFECTS_H_ */