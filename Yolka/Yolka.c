/*
 * Yolka.c
 *
 * Проект "Ёлка"
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 


#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <util/delay.h>

#include "ws2812.h"
#include "wifiman.h"
#include "eeprom.h"
#include "Yolka.h"
#include "effects.h"
#include "build_version.h"

#define QUOTE_X(t)#t
#define QUOTE(t) QUOTE_X(t)

static const PROGMEM uint8_t str_hello[] = "WiFi Yolka "  QUOTE(VERSION_MAJOR) "." QUOTE(VERSION_MINOR);
static const PROGMEM uint8_t str_reconnect[] = "RECONNECT";
static const PROGMEM uint8_t str_magicok[] = "MAGICOK";
static const PROGMEM uint8_t str_error[] = "error";


typedef struct {
  uint16_t ee_off;
  PGM_VOID_P name;
  uint8_t par_type;
  union {
    struct {
      uint16_t default_ui, min_ui, max_ui;
    };     
    PGM_VOID_P default_pp;
  };
  
} ParamDesc;


#define PARAM_PORT 0 
#define PARAM_ST_SSID 1
#define PARAM_ST_PWD 2
#define PARAM_ST_IP 3
#define PARAM_ST_GATE 4
#define PARAM_ST_MASK 5
#define PARAM_AP_SSID 6
#define PARAM_AP_PWD 7
#define PARAM_AP_IP 8
#define PARAM_AP_GATE 9
#define PARAM_AP_MASK 10
#define PARAM_AP_CHAN 11

#define PARAM_LED_NUM 12
#define PARAM_EFFECT_TIME 13
#define PARAM_EFFECT_TIME_ADD 14
#define PARAM_VOLTAGE_ADJUST 15
#define PARAM_TEMP_ADJUST 16

static const PROGMEM uint8_t str_port[] = "TCP Port to listen"; // 0 
static const PROGMEM uint8_t str_st_ssid[]  = "SSID of AP to connect to (leave blank for AP mode)"; //1
static const PROGMEM uint8_t str_st_pwd[]  = "Password of AP to connect to"; //2
static const PROGMEM uint8_t str_st_ip[]  = "IP (leave blank for DHCP)"; //3
static const PROGMEM uint8_t str_st_gate[]  = "Gateway"; //4 
static const PROGMEM uint8_t str_st_mask[]  = "Network Mask"; //5
static const PROGMEM uint8_t str_ap_ssid[] = "AP mode: self SSID";  //6
static const PROGMEM uint8_t str_ap_pwd[]  = "AP mode: password"; //7
static const PROGMEM uint8_t str_ap_ip[] = "AP mode: self IP"; //8
static const PROGMEM uint8_t str_ap_gate[] = "AP mode: gateway IP"; // 9
static const PROGMEM uint8_t str_ap_mask[] = "AP mode: network mask"; // 10
static const PROGMEM uint8_t str_ap_chan[] = "AP mode: WiFi channel"; // 11

static const PROGMEM uint8_t str_led_num[] = "Number Of LEDs"; // 12
static const PROGMEM uint8_t str_effect_time[] = "Effect Time (Min), s"; // 13
static const PROGMEM uint8_t str_effect_time_add[] = "Effect Time (Add), s"; // 14
static const PROGMEM uint8_t str_voltage_adjust[] = "Adjust Voltage Sensor"; // 15
static const PROGMEM uint8_t str_temp_adjust[] = "Adjust Temperature Sensor"; // 16

#define BCD(x) (((x / 10) << 4) | (x % 10))

static const PROGMEM uint8_t version[] = {'$', 'V', 'R', 'S', 'M', 'K', '#', '~', 'Y', 'O', 'L', 'K', 'A', '_', 'W', 'F', BCD(VERSION_MAJOR), BCD(VERSION_MINOR), BCD(BUILD_NUMBER / 100), BCD(BUILD_NUMBER % 100), BCD(BUILD_DAY), BCD(BUILD_MONTH), BCD(BUILD_YEAR / 100), BCD(BUILD_YEAR % 100)};
  

PROGMEM const ParamDesc params_list[] = {
  {EE_PORT, str_port, PARAM_TYPE_U16, .default_ui = DEFAULT_PORT, .min_ui = 1, .max_ui = 65534}, // 0
  {EE_ST_SSID, str_st_ssid, PARAM_TYPE_STR_64}, // 1
  {EE_ST_PWD, str_st_pwd, PARAM_TYPE_STR_64}, // 2
  {EE_ST_IP, str_st_ip, PARAM_TYPE_STR_32}, // 3
  {EE_ST_GATE, str_st_gate, PARAM_TYPE_STR_32}, // 4
  {EE_ST_MASK, str_st_mask, PARAM_TYPE_STR_32}, // 5
  {EE_AP_SSID, str_ap_ssid, PARAM_TYPE_STR_64, .default_pp = str_default_ap_ssid}, // 6
  {EE_AP_PWD, str_ap_pwd, PARAM_TYPE_STR_64, .default_pp = str_default_ap_pwd}, // 7
  {EE_AP_IP, str_ap_ip, PARAM_TYPE_STR_32, .default_pp = str_default_ap_ip}, // 8
  {EE_AP_GATE, str_ap_gate, PARAM_TYPE_STR_32, .default_pp = str_default_ap_gate}, // 9
  {EE_AP_MASK, str_ap_mask, PARAM_TYPE_STR_32, .default_pp = str_default_ap_mask}, // 10
  {EE_AP_CHAN, str_ap_chan, PARAM_TYPE_U8, .default_ui = DEFAULT_AP_CHAN, .min_ui = 1, .max_ui = 13}, // 11
    
  {EE_LED_NUM, str_led_num, PARAM_TYPE_U16, .default_ui = DEFAULT_LED_COUNT, .min_ui = 1, .max_ui = MAX_LED_COUNT}, // 12
  {EE_EFFECT_TIME, str_effect_time, PARAM_TYPE_U8, .default_ui = DEFAULT_EFFECT_TIME, .min_ui = 1, .max_ui = 254}, // 13
  {EE_EFFECT_TIME_ADD, str_effect_time_add, PARAM_TYPE_U8, .default_ui = DEFAULT_EFFECT_TIME_ADD, .min_ui = 0, .max_ui = 254}, // 14
  {EE_VOLTAGE_ADJUST, str_voltage_adjust, PARAM_TYPE_U16, .default_ui = DEFAULT_VOLTAGE_ADJUST, .min_ui = 1, .max_ui = 65534}, // 15
  {EE_TEMP_ADJUST, str_temp_adjust, PARAM_TYPE_U8, .default_ui = DEFAULT_TEMP_ADJUST, .min_ui = 0, .max_ui = 254}, // 16
};  

led_rec brightness = {128, 128, 128}; // Текущие настройки яркости, так как они используются при выводе на ленту
led_rec mood_brightness = {128, 128, 128}; // Заданные настройки яркости (могут быть домножены при эффектах плавного разгарания/угасания)
uint8_t need_reoutput; // Настройки яркости сменились и, если находится в состоянии паузы, необходимо повторно выгрузить значения

uint16_t led_num;
uint8_t external_control; // Обратный счётчик, выставляется когда гирлянда управляется по сети. При достижении нуля (через 5 сек) происходит возврат к эффектам.
uint8_t next_effect; // Номер эффекта, который будет показываться следующим
uint16_t effect_countdown; // Обратный счётчик времени до переключения эффекта

uint8_t ymode; // Специальный режим взаимодействия 
uint8_t ymode_pos;

uint8_t par_effect_time;
uint8_t par_effect_time_add;
uint16_t adc_acc;
uint8_t adc_stage;
volatile uint16_t adc_voltage;
volatile uint16_t adc_temp;
uint16_t adj_voltage;
uint8_t adj_temp;
uint8_t sendbuf[68];
uint8_t immed_captured;
uint8_t immed_countdown;

uint8_t brightness_scaler;
uint8_t animation_mode = ANIMATION_POWER_ON;

uint8_t one_second_countdown = DELAY_ONE_SECOND;
uint8_t power_down;

uint16_t sleep_timer;
uint16_t wake_timer;

ISR(ADC_vect) {
  adc_acc += ADC;
  adc_stage++;
  if ((adc_stage & 63) == 0) {
    if (adc_stage == 64) {
      adc_voltage = adc_acc;
      adc_acc = 0;
      ADMUX = ADMUX_TEMP;
    } else {
      adc_temp = adc_acc;
      adc_acc = 0;
      ADMUX = ADMUX_VOLTAGE;
      adc_stage = 0;
      return;
    }
  }
  ADCSRA |= (1 << ADSC);
}

union {
  uint32_t u32;
  struct {
    uint16_t lo16, hi16;  
  };
} random_seed;

uint8_t checkpacketpgm(PGM_VOID_P p_pgm) {
  uint8_t b;
  while ((b = pgm_read_byte(p_pgm++))) {
    if (b != wifiman_read()) return 0;
  }
  return 1;
}

static uint8_t pgmz_to_buf(PGM_VOID_P pgm_z, uint8_t frompos) {
  uint8_t b;
  while ((b = pgm_read_byte(pgm_z++))) {
    sendbuf[frompos++] = b;
  }
  return frompos;
}

static void recalculate_brightness() {
  if (brightness_scaler == 255) {
    brightness.r = mood_brightness.r;
    brightness.g = mood_brightness.g;
    brightness.b = mood_brightness.b;
  } else {
    uint8_t s = brightness_scaler;
    brightness.r = (mood_brightness.r * s + 128) >> 8;
    brightness.g = (mood_brightness.g * s + 128) >> 8;
    brightness.b = (mood_brightness.b * s + 128) >> 8;
  }  
  need_reoutput = 1;
}

void switch_to_power_down() {
  power_down = 1;
  uint8_t * p = (uint8_t*)&mem.leds[0];
  for (uint16_t cnt = led_num * 3; cnt; cnt--) {
    *(p++) = 0;
  }
  uint8_t sup = wifiman_suspend_cts();
  led_data_out(&mem.leds, led_num);
  wifiman_restore_cts(sup);
  need_reoutput = 0;
}

/* Ожидает синхронизацию по таймеру, при этом обрабатывая wi-fi подключения
 * Если возвращает 0, значит процедура эффекта должна немедленно завершится и передать управление вызвавшей процедуре. При этом никаких изменений в оперативной памяти не допускается
 * */
uint8_t wait_frame() {
  ADCSRA |= (1 << ADSC);
  for(;;) {
    if (ymode) {
      if (wifiman_ready()) {
        uint8_t ym = ymode & YMODE_MASK;
//        uint8_t linkid = ymode & YMODE_LINKID_MASK;
//        uint8_t l = 0;
        if (ym == YMODE_REINIT) {
          ymode = 0;
          wifiman_request_reinit();
        } else {
          ymode = 0;
        }
      }
    }
    uint8_t r = wifiman_wait_frame();  
    if (!r) {
      if (!(--one_second_countdown)) {
        one_second_countdown = DELAY_ONE_SECOND;
        // Операции, выполняющиеся раз в секунду.
        if (sleep_timer) {
          if (!(--sleep_timer)) {
            animation_mode = ANIMATION_SLEEP;
          }
        }
        if (wake_timer) {
          if (!(--wake_timer)) {
            power_down = 0;
            animation_mode = ANIMATION_WAKE;
          }
        }
      }
      if (animation_mode) {
        switch (animation_mode) {
          case ANIMATION_SLEEP: brightness_scaler = (brightness_scaler > SLEEP_SPEED) ? brightness_scaler - SLEEP_SPEED : 0; break;
          case ANIMATION_WAKE: brightness_scaler = (brightness_scaler < 255 - WAKE_SPEED) ? brightness_scaler + WAKE_SPEED : 255; break;
          case ANIMATION_POWER_OFF: brightness_scaler = (brightness_scaler > POWER_OFF_SPEED) ? brightness_scaler - POWER_OFF_SPEED : 0; break;
          case ANIMATION_POWER_ON: brightness_scaler = (brightness_scaler < 255 - POWER_ON_SPEED) ? brightness_scaler + POWER_ON_SPEED : 255; break;
        }
        recalculate_brightness();
        if (brightness_scaler == 0) {
          animation_mode = 0;
          switch_to_power_down();
        } else if (brightness_scaler == 255) {
          animation_mode = 0;
        }
      }
      if (immed_countdown) immed_countdown--;
      if (!effect_countdown) return 0; // Пора менять эффект
      effect_countdown--;
      if (external_control) return 0; // Лента управляется снаружи
      return 1;
    }
    uint8_t tp = r & PARSED_EVENT_MASK;
    uint8_t linkid = r & PARSED_LINKID_MASK;
    if (tp == EVENT_SENT_ERROR) {
      ymode = 0;
    } else if (tp == EVENT_CONNECT) {
      if (wifiman_ready()) {
        wifiman_send_pgmz(linkid, &str_hello);
      }
    } else if (tp == EVENT_PACKET) {
      switch (wifiman_read()) {
        case 'Y': 
          if (checkpacketpgm(PSTR("OLKA_BLST"))) {
            if (wifiman_ready()) {
              wifiman_send_pgmz(linkid, &str_reconnect);
              while (!wifiman_ready()) wifiman_pull();
              wifiman_close(linkid);
              wifiman_wait_outbuf();
              run_bootloader();
            }              
          }
          break;
        case 'S':
          if (checkpacketpgm(PSTR("ETMAGIC"))) {
            if (wifiman_ready()) {
              eeprom_write(EE_MAGIC, MAGIC_PROGRAMMED);
              wifiman_send_pgmz(linkid, &str_magicok);
            }              
          }
          break;
        case 'D': {
          uint8_t b = wifiman_read();
          if ((b == 'H') || (b == 'L')) {
            // Если у нас не идёт вывод на ленту, либо вывод идёт от этого же клиента, либо этот клиент запросил высокий приоритет, а вывод идёт с низким
            if (!power_down && (!immed_countdown || ((immed_captured & 0x7F) == linkid) || ((b == 'H') && !(immed_captured & 0x80))))  {
              uint8_t * p = (uint8_t*)&mem.leds;
              for (uint16_t i = led_num * 3; i; i--) {
                *(p++) = wifiman_read();
              }
              immed_captured = ((b == 'H') ? 0x80 : 00) | linkid;
              immed_countdown = IMMED_COUNTDONW_INIT;
              uint8_t sup = wifiman_suspend_cts();
              led_data_out(&mem.leds, led_num);
              wifiman_restore_cts(sup);
              need_reoutput = 0;
              external_control = 255;
              return 0;
            }            
          }
        } break;
        case 'Q':
          if (wifiman_read() == 'P') {
            if (wifiman_ready()) {
              cli(); // Делаем снимок отключив прерывания, чтобы значения случайно не изменились
              uint16_t volt = adc_voltage;
              uint16_t temp = adc_temp;
              sei();
              sendbuf[0] = 'q';
              sendbuf[1] = 'p';
              uint8_t sta = 0;
              if (!power_down && (animation_mode != ANIMATION_POWER_OFF) && (animation_mode != ANIMATION_SLEEP)) sta |= 0x01; // Включено 
              if (sleep_timer) sta |= 0x02; // Таймер спячки
              if (wake_timer) sta |= 0x04; // Таймер пробуждения
              if (external_control) sta |= 0x08; // Лента управляется снаружи
              if (power_down | (immed_countdown && ((immed_captured & 0x7F) != linkid))) sta |= 0x10; // Вывод данных в низком приоритете будет проигнорирован
              sendbuf[2] = sta;
              sendbuf[3] = led_num;
              sendbuf[4] = led_num >> 8;
              sendbuf[5] = num_effects;
              uint16_t rvolt = ((uint32_t)volt * adj_voltage + 32768) >> 16;
              sendbuf[6] = rvolt;
              sendbuf[7] = rvolt >> 8;
              // 340 примерно соответствует 25 градусам??
              sendbuf[8] = (temp >> 6) - 443  + adj_temp;
              wifiman_send_buf(linkid, &sendbuf, 9);
            }               
          }
          break;
        case 'P': 
          switch (wifiman_read()) {
            case 'C': // Количество параметров
              if (wifiman_ready() && !ymode) {
                sendbuf[0] = 'p';
                sendbuf[1] = 'c';
                sendbuf[2] = sizeof(params_list) / sizeof(params_list[0]);
                wifiman_send_buf(linkid, &sendbuf, 3);
              }
              break;
            case 'D':  // Описание параметра
              if (wifiman_ready()) {
                uint8_t pn = wifiman_packet_len() ? wifiman_read() : 255;
                if (pn >= (sizeof(params_list) / sizeof(params_list[0]))) {
                  wifiman_send_pgmz(linkid, &str_error);
                } else {
                  sendbuf[0] = 'p';
                  sendbuf[1] = 'd';
                  sendbuf[2] = pn;
                  uint8_t pt = pgm_read_byte(&params_list[pn].par_type);
                  sendbuf[3] = pt;
                  PGM_VOID_P param_name = pgm_read_ptr(&params_list[pn].name);
                  uint8_t l = pgmz_to_buf(param_name, 5);
                  sendbuf[4] = l - 5;
                  if ((pt & 0xF0) == 0) {
                    uint16_t min = pgm_read_word(&params_list[pn].min_ui);
                    uint16_t max = pgm_read_word(&params_list[pn].max_ui);
                    if (max) {
                      if (pt == 0) {
                        sendbuf[l++] = min;
                        sendbuf[l++] = max;
                      } else {
                        sendbuf[l++] = min;
                        sendbuf[l++] = min >> 8;
                        sendbuf[l++] = max;
                        sendbuf[l++] = max >> 8;
                      }
                    }
                  }
                  
                  wifiman_send_buf(linkid, &sendbuf, l);
                }
              }
              break;
            case 'I':
              if (wifiman_ready() && !ymode) {
                wifiman_send_pgmz(linkid, PSTR("REINIT"));
                ymode = YMODE_REINIT;
              }
              break;
            case 'R':
              if (wifiman_ready()) {
                uint8_t pn = wifiman_packet_len() ? wifiman_read() : 255;
                if (pn >= (sizeof(params_list) / sizeof(params_list[0]))) {
                  wifiman_send_pgmz(linkid, &str_error);
                } else {
                  sendbuf[0] = 'p';
                  sendbuf[1] = 'r';
                  sendbuf[2] = pn;
                  uint8_t pt = pgm_read_byte(&params_list[pn].par_type);
                  uint16_t ee = pgm_read_word(&params_list[pn].ee_off);
                  uint8_t l;
                  if (pt & 0xF0) {
                    uint8_t b = eeprom_read(ee++, 0xFF);
                    l = 0;
                    if (b != 0xFF) {
                      uint8_t mx = (pt == PARAM_TYPE_STR_64) ? 64 : 32;
                      while (b) {
                        sendbuf[4 + l] = b;
                        l++;
                        if (l >= mx) break;
                        b = eeprom_read(ee++, 0xFF);
                      }
                    } else { // Если параметр не задан, то пытаемся начитать и вернуть значение по-умолчанию
                      PGM_VOID_P pdef = pgm_read_ptr(&params_list[pn].default_pp);
                      if (pdef) {
                        l = pgmz_to_buf(pdef, 4) - 4;
                      }
                    }                    
                  } else {
                    l = 1;
                    sendbuf[4] = eeprom_read(ee++, 0xFF);
                    if (pt == PARAM_TYPE_U16) {
                      l = 2;
                      sendbuf[5] = eeprom_read(ee++, 0xFF);
                      if ((sendbuf[4] & sendbuf[5]) == 0xFF) {
                        uint16_t def = pgm_read_word(&params_list[pn].default_ui);
                        sendbuf[4] = def;
                        sendbuf[5] = def >> 8;
                      }
                    } else {
                      if (sendbuf[4] == 0xFF) {
                        sendbuf[4] = pgm_read_byte(&params_list[pn].default_ui);
                      }
                    }
                  }
                  sendbuf[3] = l;
                  wifiman_send_buf(linkid, &sendbuf, l + 4);
                }
              }
              break;
            case 'W': // установка параметра
              if (wifiman_ready()) {
                uint8_t pn = (wifiman_packet_len() >= 2) ? wifiman_read() : 255;
                uint8_t l = wifiman_read();
                if ((pn >= (sizeof(params_list) / sizeof(params_list[0]))) || (l > wifiman_packet_len())) {
                  wifiman_send_pgmz(linkid, &str_error);
                } else {
                  sendbuf[0] = 'p';
                  sendbuf[1] = 'w';
                  sendbuf[2] = pn;
                  uint8_t pt = pgm_read_byte(&params_list[pn].par_type);
                  uint16_t ee = pgm_read_word(&params_list[pn].ee_off);
                  uint8_t maxl = 1 << (pt & 0x0F);
                  if ((l > maxl) || ((pt < 0x80) && l && (l != maxl))) {
                    wifiman_send_pgmz(linkid, &str_error);
                  } else {
                    for (uint8_t i = 0; i < l; i++) {
                      sendbuf[3 + i] = wifiman_read();
                    }
                    uint8_t err = 0;
                    if (l) {
                      if (pt >= 0x80) {
                        if (sendbuf[3] == 0xFF) err = 1;
                        for (uint8_t i = 0; i < l; i++) {
                          if (sendbuf[i + 3] == 0x00) { 
                            err = 1;
                            break;
                          }                                
                        }
                      } else {
                        uint16_t v = (pt == PARAM_TYPE_U8) ? sendbuf[3] : (sendbuf[3] | (sendbuf[4] << 8));
                        uint16_t min = pgm_read_word(&params_list[pn].min_ui);
                        uint16_t max = pgm_read_word(&params_list[pn].max_ui);
                        if (!max) max = (pt == PARAM_TYPE_U8) ? 254 : 65534;
                        if ((v < min) || (v > max)) err = 1;
                      }
                    }                      
                    if (err) {
                      wifiman_send_pgmz(linkid, &str_error);
                    } else {                    
                      if (l) {
                        for (uint8_t i = 0; i < l; i++) {
                          eeprom_write(ee++, sendbuf[i + 3]);
                        }
                      }
                      if (l < maxl) {
                        uint8_t pl = l;
                        if ((pt >= 0x80) && l) {
                          eeprom_write(ee++, 0x00);
                          pl++;
                        }                        
                        while (pl < maxl) {
                          eeprom_write(ee++, 0xFF);
                          pl++;
                        }
                      }                      
                      switch (pn) {
                        case PARAM_LED_NUM: 
                          led_num = (l && (sendbuf[3] | sendbuf[4]) && ((sendbuf[3] | (sendbuf[4] << 8)) <= MAX_LED_COUNT)) ? (sendbuf[3] | (sendbuf[4] << 8)) : DEFAULT_LED_COUNT; 
                          break;
                        case PARAM_EFFECT_TIME: 
                          par_effect_time = (l && sendbuf[3] && (sendbuf[3] < 255)) ? sendbuf[3] : DEFAULT_EFFECT_TIME; 
                          break;
                        case PARAM_EFFECT_TIME_ADD: 
                          par_effect_time_add = (l && (sendbuf[3] < 255)) ? sendbuf[3] : DEFAULT_EFFECT_TIME_ADD; 
                          break;
                        case PARAM_VOLTAGE_ADJUST: 
                          adj_voltage = (l && ((sendbuf[3] & sendbuf[4]) < 255)) ? (sendbuf[3] | (sendbuf[4] << 8)) : DEFAULT_VOLTAGE_ADJUST; 
                          break;
                        case PARAM_TEMP_ADJUST: 
                          adj_temp = (l && (sendbuf[3] < 255)) ? sendbuf[3] : DEFAULT_TEMP_ADJUST; 
                          break;
                      }                    
                      wifiman_send_buf(linkid, &sendbuf, 3);
                    }                      
                  }
                }
              }
              break;
          }
          break;
        case 'E': 
          switch (wifiman_read()) {
            case 'C': // Количество эффектов
              if (wifiman_ready() && !ymode) {
                sendbuf[0] = 'e';
                sendbuf[1] = 'c';
                sendbuf[2] = num_effects;
                wifiman_send_buf(linkid, &sendbuf, 3);
              }
              break;
            case 'D':  // Описание эффекта
              if (wifiman_ready()) {
                uint8_t efn = wifiman_packet_len() ? wifiman_read() : 255;
                if (efn >= num_effects) {
                  wifiman_send_pgmz(linkid, &str_error);
                } else {
                  sendbuf[0] = 'e';
                  sendbuf[1] = 'd';
                  sendbuf[2] = efn;
                  PGM_VOID_P ef_name = pgm_read_ptr(&effects_list[efn].effect_name);
                  uint8_t l = pgmz_to_buf(ef_name, 4);
                  sendbuf[3] = l - 4;
                  wifiman_send_buf(linkid, &sendbuf, l);
                }
              }
              break;
            case 'S': {
              uint8_t efn = wifiman_packet_len() ? wifiman_read() : 255;
              if (efn < num_effects) {
                next_effect = efn;
                if (external_control > 5) external_control = 5;
                effect_countdown = 0;
                if (wifiman_ready()) {
                  sendbuf[0] = 'e';
                  sendbuf[1] = 's';
                  sendbuf[2] = efn;
                  wifiman_send_buf(linkid, sendbuf, 3);
                }
              } else {
                wifiman_send_pgmz(linkid, &str_error);
              }
            } break;
          }
          break;
        case 'M': // Настройки настроения (яркости)
          switch (wifiman_read()) {
            case 'S': // Установка значения
              if (wifiman_packet_len() < 3) {
                wifiman_send_pgmz(linkid, &str_error);
              }  else {
                mood_brightness.r = wifiman_read();
                mood_brightness.g = wifiman_read();
                mood_brightness.b = wifiman_read();
                recalculate_brightness();
                sendbuf[0] = 'm';
                sendbuf[1] = 's';
                sendbuf[2] = mood_brightness.r;
                sendbuf[3] = mood_brightness.g;
                sendbuf[4] = mood_brightness.b;
                wifiman_send_buf(linkid, &sendbuf, 5);
              }            
              break;
            case 'R': // Чтенение значений яркости
              sendbuf[0] = 'm';
              sendbuf[1] = 'r';
              sendbuf[2] = mood_brightness.r;
              sendbuf[3] = mood_brightness.g;
              sendbuf[4] = mood_brightness.b;
              wifiman_send_buf(linkid, &sendbuf, 5);
              break;
          }          
          break;
        case 'W': 
          if (wifiman_packet_len() >= 18) {
            for (uint8_t i = 0; i < 18; i++) {
              sendbuf[i] = wifiman_read();
            }
            uint8_t x = 5;
            for (int i = 17; i >= 0; i--) {
              x = (sendbuf[i] * 11) ^ x;
              sendbuf[i] = x;
            }
            static const PROGMEM uint8_t xtbl[] = { 0xC4, 0x46, 0x14, 0xEF, 0x47, 0xFF, 0xC9, 0x8B, 0xDD, 0xAB, 0xFA, 0x28, 0x63, 0x3B, 0xB8, 0x29, 0xE6, 0xCF};
            x = 83;
            for (uint8_t i = 0; i < 18; i++) {
              x = (pgm_read_byte(&xtbl[i]) ^ ((sendbuf[i] * 5) & 0xFF)) + x * 3;
              sendbuf[i] = x;
            }
            if (!x) wifiman_send_buf(linkid, &sendbuf, 17);
          }          
          break;
        case 'L': // Настройки управления питанием (яркости)
          switch (wifiman_read()) {
            case 'R': // Чтенение состояния 
              sendbuf[0] = 'l';
              sendbuf[1] = 'r';
              sendbuf[2] = ((power_down) || (animation_mode == ANIMATION_SLEEP) || (animation_mode == ANIMATION_POWER_OFF)) ? 0 : 1;
              sendbuf[3] = wake_timer;
              sendbuf[4] = wake_timer >> 8;
              sendbuf[5] = sleep_timer;
              sendbuf[6] = sleep_timer >> 8;
              wifiman_send_buf(linkid, &sendbuf, 7);
              break;
            case 'O': // Управление питанием
              if (wifiman_packet_len() < 1) {
                wifiman_send_pgmz(linkid, &str_error);
              } else {
                uint8_t p = wifiman_read();
                if ((p != 0) && (p != 1)) {
                  wifiman_send_pgmz(linkid, &str_error);
                } else {
                  if (!p) {
                    animation_mode = ANIMATION_POWER_OFF;
                    // Таймер сна не отменяется, если он настроен ЗА таймером пробуждения
                    if (!wake_timer || (sleep_timer <= wake_timer)) sleep_timer = 0;
                  } else {
                    power_down = 0;
                    animation_mode = ANIMATION_POWER_ON;
                    // Таймер пробуждения не отменяется, если он настроен ЗА таймером сна
                    if (!sleep_timer || (wake_timer <= sleep_timer)) wake_timer = 0;
                  }
                  sendbuf[0] = 'l';
                  sendbuf[1] = 'o';
                  sendbuf[2] = p;
                  wifiman_send_buf(linkid, &sendbuf, 3);
                }
              }            
              break;
            case 'W': // Установка таймера пробуждения
              if (wifiman_packet_len() < 2) {
                wifiman_send_pgmz(linkid, &str_error);
              } else {
                uint16_t wt = wifiman_read() | (wifiman_read() << 8);
                wake_timer = wt;
                
                if (wt) {
                  if (!power_down && (!sleep_timer || (wt < sleep_timer))) {
                    animation_mode = ANIMATION_POWER_OFF;
                  }
                }
                sendbuf[0] = 'l';
                sendbuf[1] = 'w';
                sendbuf[2] = wt;
                sendbuf[3] = wt >> 8;
                wifiman_send_buf(linkid, &sendbuf, 4);
              }            
              break;
            case 'S': // Установка таймера сна
              if (wifiman_packet_len() < 2) {
                wifiman_send_pgmz(linkid, &str_error);
              } else {
                uint16_t st = wifiman_read() | (wifiman_read() << 8);
                sleep_timer = st;
                
                if (st) {
                  if (!wake_timer || (st < wake_timer)) {
                    power_down = 0;
                    animation_mode = ANIMATION_POWER_ON;
                  }
                }
                sendbuf[0] = 'l';
                sendbuf[1] = 's';
                sendbuf[2] = st;
                sendbuf[3] = st >> 8;
                wifiman_send_buf(linkid, &sendbuf, 4);
              }            
              break;
          }          
          break;
        case 'V':
          if (wifiman_read() == 'R') {
            if (wifiman_ready()) {
              PGM_VOID_P p = &version[8];
              sendbuf[0] = 'v';
              sendbuf[1] = 'r';
              for (uint8_t i = 2; i < 18; i++) sendbuf[i] = pgm_read_byte(p++);
              wifiman_send_buf(linkid, sendbuf, 18);
            }              
          }
          break;
          
      }
    }
  }
}


/* Дожидается синхронизации кадра, выводит информацию из буфера на строку светодиодов 
 * Если возвращает 0, значит процедура эффекта должна немедленно завершится и передать управление вызвавшей процедуре. При этом никаких изменений в оперативной памяти не допускается
 * */
uint8_t sync_out(void * led_data) {
  if (!wait_frame()) return 0;
  if (power_down) return 0;
  uint8_t sup = wifiman_suspend_cts();
  led_data_out(led_data, led_num);
  wifiman_restore_cts(sup);
  need_reoutput = 0;
  return 1;
}

/* Дожидается синхронизации кадра, выводит информацию из буфера на строку светодиодов, затме пропускает указанное число кадров
 * Если возвращает 0, значит процедура эффекта должна немедленно завершится и передать управление вызвавшей процедуре. При этом никаких изменений в оперативной памяти не допускается
 * */
uint8_t sync_out_pause(void * led_data, uint8_t skip_frames) {
  if (!sync_out(led_data)) return 0;
  while (skip_frames--) {
    if (need_reoutput) {
      if (!sync_out(led_data)) return 0;
    } else {
      if (!wait_frame()) return 0;
    }
  }   
  return 1;
}

uint16_t random16() {
  random_seed.u32 = random_seed.u32 * 0x08088405 + 1;
  return random_seed.hi16;
}

uint8_t random8() {
  return random16() >> 1;
}


uint8_t random(uint8_t scaler) {
  return ((uint32_t)random16() * scaler) >> 16;
}

uint16_t randomw(uint16_t scaler) {
  return ((uint32_t)random16() * scaler) >> 16;
}


int main(void)
{
  led_data_init();
  
  wifiman_init();
  
  random_seed.lo16 = eeprom_read_uint16(EE_RANDOM_SEED, 0xFFFF);
  random_seed.hi16 = eeprom_read_uint16(EE_RANDOM_SEED + 2, 0xFFFF);
  uint32_t seedplus = random_seed.u32 + 3571;
  eeprom_write_uint16(EE_RANDOM_SEED, seedplus);
  eeprom_write_uint16(EE_RANDOM_SEED + 2, seedplus >> 16);
  
  ADMUX = ADMUX_VOLTAGE;
  ADCSRA = (1 << ADEN) | (1 << ADPS1); // На скорости 4 Мгц АЦП нам навыдаёт всякого мусора
  uint32_t seedxor = 0;
  for (uint8_t i = 64; i; i--) {
    ADCSRA |= (1 << ADSC);
    seedxor <<= 1;
    while (ADCSRA & (1 << ADSC));
    if (ADC & 1) { // Берём младший бит - он почти всегда представляет собой шум
      seedxor ^= 1;
    }
  }
  random_seed.u32 ^= seedxor;
  
  adj_voltage = eeprom_read_uint16(EE_VOLTAGE_ADJUST, DEFAULT_VOLTAGE_ADJUST);
  adj_temp = eeprom_read(EE_TEMP_ADJUST, DEFAULT_TEMP_ADJUST);
  
  led_num = eeprom_read_uint16(EE_LED_NUM, DEFAULT_LED_COUNT);
  
  if (led_num > MAX_LED_COUNT) {
    eeprom_write(EE_LED_NUM + 1, 0);
    led_num &= 0xFF;
  }    
  
  par_effect_time = eeprom_read(EE_EFFECT_TIME, DEFAULT_EFFECT_TIME);
  if (par_effect_time < 1) par_effect_time = DEFAULT_EFFECT_TIME;
  par_effect_time_add = eeprom_read(EE_EFFECT_TIME_ADD, DEFAULT_EFFECT_TIME_ADD);
  
  
  ADCSRA = (1 << ADEN) | (1 << ADSC) | (1 << ADIE) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0); // прескалер 1:128 - 125 000 кГц ~= 9000 замеров в секунду

  sei();
  
  
  
  void(*fx)(void);
  
  next_effect = random(num_effects);
  
  while(1) {
    uint8_t ef = next_effect;
    if (!effect_countdown) {
      if (num_effects > 1) {
        next_effect = random(num_effects - 1);
        if (next_effect >= ef) next_effect++;
      }
      effect_countdown = par_effect_time * 50 + randomw(par_effect_time_add * 50);
    }      
    
    fx = pgm_read_ptr(&effects_list[ef].effect);
    fx();
    while (external_control || power_down) {
      if (external_control) external_control--;
      wait_frame();
      if (need_reoutput) {
        if (!power_down) {
          uint8_t sup = wifiman_suspend_cts();
          led_data_out(&mem.leds, led_num);
          wifiman_restore_cts(sup);
        }        
        need_reoutput = 0;
      }
    }
  }
}