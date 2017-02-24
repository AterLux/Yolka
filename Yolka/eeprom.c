/*
 * eeprom.c
 *
 * Модуль для работы с EEPROM
 *
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 

#include <avr/io.h>
#include <avr/interrupt.h>
#include "eeprom.h"


uint8_t eeprom_read(uint16_t address, uint8_t return_if_0xFF) {
  while(EECR & (1<<EEPE)); 
  uint8_t oldSREG = SREG ;
  cli();
  while(EECR & (1<<EEPE));
  EEAR = address;
  EECR |= (1<<EERE);
  uint8_t d = EEDR;
  SREG = oldSREG;
  return (d == 0xFF) ? return_if_0xFF : d;
}
  
void eeprom_write(uint16_t address, uint8_t data) {
  while(EECR & (1<<EEPE));
  uint8_t oldSREG = SREG ;
  cli();
  while(EECR & (1<<EEPE));
  EEAR = address;
  EECR |= (1<<EERE);  
  uint8_t d = EEDR;
  if (d != data) {
    EEDR = data;
    EECR |= (1<<EEMPE);
    EECR |= (1<<EEPE);
  }
  SREG = oldSREG;
}

uint16_t eeprom_read_uint16(uint16_t address, uint16_t return_if_0xFFFF) {
  uint16_t w = eeprom_read(address, 0xFF) | (eeprom_read(address + 1, 0xFF) << 8);
  return (w == 0xFFFF) ? return_if_0xFFFF : w;
}

void eeprom_write_uint16(uint16_t address, uint16_t data) {
  eeprom_write(address, data);
  eeprom_write(address + 1, data >> 8);
}

