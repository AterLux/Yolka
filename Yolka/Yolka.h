/*
 * Yolka.h
 *
 * Проект "Ёлка"
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 


#ifndef YOLKA_H_
#define YOLKA_H_


#include "ws2812.h"

#define MAX_LED_COUNT 512 // Максимально допустимое количество светодиодов

#define DEFAULT_LED_COUNT 50 // Количество светодиодов, по-умолчанию 

#define BOOTLOADER_OFFSET (FLASHEND - 4095)
#define run_bootloader() ((void (*)(void))(BOOTLOADER_OFFSET / 2))()

#define ADMUX_VOLTAGE ((1 << REFS1) | (1 << REFS0) | 2)
#define ADMUX_TEMP ((1 << REFS1) | (1 << REFS0) | 8)

#define DEFAULT_VOLTAGE_ADJUST 660
#define DEFAULT_TEMP_ADJUST 128

#define DEFAULT_EFFECT_TIME 20 // Минимальное время между эффектами, в секундах
#define DEFAULT_EFFECT_TIME_ADD 20 // Пределы случайно добавляемого времени, в секундах


// Позиции в EEPROM
#define EE_LED_NUM 2
#define EE_RANDOM_SEED 4
#define EE_VOLTAGE_ADJUST 8
#define EE_TEMP_ADJUST 10
#define EE_EFFECT_TIME 12
#define EE_EFFECT_TIME_ADD 13

#define EE_MAGIC 0 // позиция в EEPROM, где хранится код, означающий что прошивка встала нормально (устанавливается самой прошивкой)
#define MAGIC_PROGRAMMED 0x83 // сам код, который должен быть установлен прошивкой


#define YMODE_LINKID_MASK 0x0F
#define YMODE_MASK 0xF0
#define YMODE_REINIT 0xF0

#define PARAM_TYPE_U8 0
#define PARAM_TYPE_U16 1
#define PARAM_TYPE_STR_32 0x85
#define PARAM_TYPE_STR_64 0x86

#define IMMED_COUNTDONW_INIT 25 // Количество кадров (т.е. периодов 1/50 секунды) после вывода данных от клиента, в течение которых данные с тем же приоритетом от других подключившихся будут игнорироваться


// Различные виды анимаций (нарастание/затухание яркости)
#define ANIMATION_WAKE 1
#define ANIMATION_SLEEP 2
#define ANIMATION_POWER_ON 3
#define ANIMATION_POWER_OFF 4

// Скорости изменения яркости (от 0 до 255, или обратно) для каждого вида анимации
#define SLEEP_SPEED 1
#define WAKE_SPEED 1
#define POWER_ON_SPEED 5
#define POWER_OFF_SPEED 8

// Количество светодиодов в линейке
extern uint16_t led_num;

/* Ожидает синхронизацию по таймеру, при этом обрабатывая wi-fi подключения
 * Если возвращает 0, значит процедура эффекта должна немедленно завершится и передать управление вызвавшей процедуре. При этом никаких изменений в оперативной памяти не допускается
 * */
uint8_t wait_frame();

/* Дожидается синхронизации кадра, выводит информацию из буфера на строку светодиодов 
 * Если возвращает 0, значит процедура эффекта должна немедленно завершиться и передать управление вызвавшей процедуре. При этом никаких изменений в оперативной памяти не допускается
 * */
uint8_t sync_out(void * led_data);

/* Дожидается синхронизации кадра, выводит информацию из буфера на строку светодиодов, затме пропускает указанное число кадров
 * Если возвращает 0, значит процедура эффекта должна немедленно завершиться и передать управление вызвавшей процедуре. При этом никаких изменений в оперативной памяти не допускается
 * */
uint8_t sync_out_pause(void * led_data, uint8_t skip_frames);


/* Возвращает 16битное случайное число */
uint16_t random16();

/* Возвращает 8битное случайное число */
uint8_t random8();

/* Возвращает 8 битное случайно равномерно распределённое число, меньше чем scaler */
uint8_t random(uint8_t scaler);

/* Возвращает 16 битное случайно равномерно распределённое число, меньше чем scaler */
uint16_t randomw(uint16_t scaler);

#endif /* YOLKA_H_ */