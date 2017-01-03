/*
 * wifiman.h
 *
 * Подсистема взаимодействия с ESP8266, проект "Ёлка"
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 


#ifndef WIFIMAN_H_
#define WIFIMAN_H_

#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/pgmspace.h>

#define DEFAULT_AP_CHAN 6
#define DEFAULT_PORT 3388


#define CTS_PORT D
#define CTS_PIN_NUM 2
#define CTS (1 << CTS_PIN_NUM)

#define CALC_UBRR(osc) ((F_CPU + (osc * 4)) / (osc * 8) - 1) // формула для вычисления значения регистра UBRR для 2x скорости

#define UART_UBRR_DEFAULT CALC_UBRR(115200)
#define UART_UBRR_WORK CALC_UBRR(1000000)

#define PARSED_OK 1
#define PARSED_ERROR 2
#define PARSED_FAIL 3
#define PARSED_PROMPT 4
#define PARSED_SEND_OK 5
#define PARSED_CWJAP_MATCH 6  
#define PARSED_BUSY 7

#define PARSED_EVENT_MASK 0xF0
#define PARSED_LINKID_MASK 0x0F
#define EVENT_PACKET 0x10
#define EVENT_CONNECT 0x30
#define EVENT_DISCONNECT 0x40
#define EVENT_SENT_OK 0x50
#define EVENT_SENT_ERROR 0x60


#define IN_QUEUE_SIZE 64
#define IN_QUEUE_OFF_THRESHOLD 58
#define IN_QUEUE_ON_THRESHOLD 32

#define OUT_QUEUE_SIZE 32

#define PARSER_STATE_WAIT_START 0
#define PARSER_STATE_WAIT_LINE_FEED 1


#define WIFIMAN_READY 0
#define WIFIMAN_INIT_REQUIRED  1 // Затребована переинициализация. Отправляется 
#define WIFIMAN_INIT_UART_TEST 2 // Проверен ответ на текущей скорости, затем если овет получен ATE0, если нет - DEF_SET
#define WIFIMAN_INIT_UART_DEF_SET 3 // Задана новая скорость на скорости по-умолчанию, затем пауза
#define WIFIMAN_INIT_UART_DEF_DELAY 4 // Задана новая скорость на скорости по-умолчанию, затем WORK_SET
#define WIFIMAN_INIT_UART_WORK_SET 5 // Заданы настройки UART на рабочей скорости, затем пауза
#define WIFIMAN_INIT_UART_WORK_DELAY 6 // Задана новая скорость на скорости по-умолчанию, затем ATE0
#define WIFIMAN_INIT_ATE0 7 // Отключение эхо, затем WIFIMAN_INIT_CIPCLOSE
#define WIFIMAN_INIT_CIPCLOSE 8 // Закрытие всех подключений, если есть, затем CIPMUX
#define WIFIMAN_INIT_CIPMUX 9 // Режим множественных подключений, затем CIPSERVERSTOP
#define WIFIMAN_INIT_CIPSERVERSTOP 10 // Закрытие прослушки порта, если есть, затем CWMODE
#define WIFIMAN_INIT_CWMODE 11 // Выбор режима: станция, или точка доступа, затем CWDHCP_AP для точки, CIPSTA_DHCP для станции
#define WIFIMAN_INIT_CWMODE_AFTER_ST_ERROR 12 // Выбор режима - точка доступа принудительно, затем CWDHCP_AP.
#define WIFIMAN_INIT_CWDHCP_AP 13 // Точка доступа: Настроен DHCP, затем CIPAP
#define WIFIMAN_INIT_CIPAP 14 // Точка доступа: настроен IP, затем CWDHCP_AP
#define WIFIMAN_INIT_CWSAP 15 // Точка доступа: Настроен SSID и пароль, затем CIPSERVER
#define WIFIMAN_INIT_CIPSTA_DHCP 16 // Станция: Настроен IP адрес, или DHCP, затем проверка текущего подключения CWJAP_TEST
#define WIFIMAN_INIT_CWJAP_TEST 17 // Станция: Проверено текущее подключение, затем CWJAP_SET, если не совпало, и CIPSTA_DHCP, если уже подключено
#define WIFIMAN_INIT_CWJAP_SET 18 // Станция: Задано новое подключение, затем CIPSERVER
#define WIFIMAN_INIT_CIPSERVER 19 // Переход в режим WIFIMAN_READY, если всё нормально
#define WIFIMAN_CIPSEND 20
#define WIFIMAN_CIPSEND_DATA 21
#define WIFIMAN_DISCONNECT 22

#define SEND_TYPE_RAM 0
#define SEND_TYPE_PGM 1

#define CWMODE_AP 2
#define CWMODE_ST 1

#define DELAY_ONE_SECOND 50

// Позиции в EEPROM
#define EE_PORT 60 
#define EE_AP_CHAN 63
#define EE_AP_IP 64
#define EE_AP_GATE 96
#define EE_AP_MASK 128
#define EE_ST_IP 160
#define EE_ST_GATE 192
#define EE_ST_MASK 224
#define EE_AP_SSID 256
#define EE_AP_PWD 320
#define EE_ST_SSID 384
#define EE_ST_PWD 448

extern const PROGMEM uint8_t str_default_ap_ssid[];
extern const PROGMEM uint8_t str_default_ap_pwd[];
extern const PROGMEM uint8_t str_default_ap_ip[];
extern const PROGMEM uint8_t str_default_ap_gate[];
extern const PROGMEM uint8_t str_default_ap_mask[];


/* Инициализирует менеджер вай-фая, таймер, USART*/
void wifiman_init();

/* Обрабатывает вай-фай. Необходимо вызывать циклически.
 * Возвращает код события, или 0, если событие не произошло.
 * Старшие 4ре бита кодирует тип события, младшие 4ре - linkid.
 * Событие EVENT_TYPE_DATA/EVENT_TYPE_DATA_END необходимо обработать и завершить вызовом wifiman_packet_finish()
 * */
uint8_t wifiman_pull();

/* В цикле вызывает wifiman_pool() пока таймер не переполнится.
 * Если произошло событие, то немедленно возвращает код события */
uint8_t wifiman_wait_frame();


/* Количество байт оставшихся для чтения в текущем пакете */
uint16_t wifiman_packet_len();

/* Читает очередной байт из пакета. Если достигнут конец пакета, будет возвращать нули */
uint8_t wifiman_read();

/* Признак готовности (отправке очередного пакета, закрытию соединения, запрос на реинициализацию) */
uint8_t wifiman_ready();

/* Запрос на реинициализацию */
void wifiman_request_reinit();

/* Отправка строки завершённой нулём из флеш памяти */
void wifiman_send_pgmz(uint8_t linkid, PGM_VOID_P p_pgm);

/* Отправка строки из ОЗУ */
void wifiman_send_buf(uint8_t linkid, void * buf, uint8_t len);

/* Закрывает имеющееся подключение */
void wifiman_close(uint8_t linkid);

/* Приостанавливает приём и возвращает состояние */
uint8_t wifiman_suspend_cts();

/* Восстанавливает приём к предыдущему состоянию */
void wifiman_restore_cts(uint8_t prev);

/* Ожидает опустошения выходного буфера */
void wifiman_wait_outbuf();

#endif /* WIFIMAN_H_ */