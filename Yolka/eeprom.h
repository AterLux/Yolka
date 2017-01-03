/*
 * eeprom.h
 *
 * Модуль для работы с EEPROM
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 


#ifndef EEPROM_H_
#define EEPROM_H_

#include <avr/io.h>

uint8_t eeprom_read(uint16_t address, uint8_t return_if_0xFF);

void eeprom_write(uint16_t address, uint8_t data);

int16_t eeprom_read_int16(uint16_t address, int16_t return_if_0xFFFF);

void eeprom_write_int16(uint16_t address, int16_t data);



#endif /* EEPROM_H_ */