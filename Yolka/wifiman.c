/*
 * wifiman.c
 *
 * Подсистема взаимодействия с ESP8266, проект "Ёлка"
 * 
 * Author: Погребняк Дмитрий, г. Самара, 2015
 */ 

#include "wifiman.h"
#include "eeprom.h"
#include <avr/interrupt.h>
#include "tools.h"

const PROGMEM uint8_t str_default_ap_ssid[] = "WIFI_YOLKA";
const PROGMEM uint8_t str_default_ap_pwd[] = "0123456789";
const PROGMEM uint8_t str_default_ap_ip[] = "192.168.10.1";
const PROGMEM uint8_t str_default_ap_gate[] = "192.168.10.1";
const PROGMEM uint8_t str_default_ap_mask[] = "255.255.255.0";


uint8_t in_queue[IN_QUEUE_SIZE];
volatile uint8_t in_queue_read_pos;
volatile uint8_t in_queue_write_pos;

uint8_t out_queue[IN_QUEUE_SIZE];
volatile uint8_t out_queue_read_pos;
volatile uint8_t out_queue_write_pos;

uint8_t parser_state;

volatile uint8_t next_frame;
volatile uint16_t wifiman_timeout; // Таймаут на выполнение команды
volatile uint8_t wifiman_delay; // Задержка до перехода в следующее состояние

uint16_t packet_len;

uint8_t wifiman_state;
uint8_t busys_cnt;

struct {
  uint8_t size;
  uint8_t type;
  union {
    uint8_t * ptr_ram;
    PGM_VOID_P ptr_pgm;
  };   
} wifiman_send;

ISR(TIMER1_COMPA_vect) {
  next_frame = 1;
  uint16_t to = wifiman_timeout;
  if (to) wifiman_timeout = to - 1;
  uint8_t d = wifiman_delay;
  if (d) wifiman_delay = d - 1;
}

inline void suspend_rx() {
  PORT(CTS_PORT) |= CTS;
}

inline void resume_rx() {
  PORT(CTS_PORT) &= ~CTS;
}

static uint8_t is_in_eeprom(uint16_t address) {
  return eeprom_read(address, 0xFF) != 0xFF;
}

void wifiman_init() {
  DDR(CTS_PORT) |= CTS;
  
  wifiman_state = WIFIMAN_INIT_REQUIRED;
  
  UCSR0A = (1 << U2X0);
  UBRR0 = UART_UBRR_WORK;
  UCSR0B = (1 << RXCIE0) | (1 << RXEN0) | (1 << TXEN0);
  UCSR0C = (1 << UCSZ00) | (1 << UCSZ01);
  
  TCCR1A = 0;
  OCR1A = 1249; // прервыание 50 раз в секунду
  TIMSK1 = (1 << OCIE1A);
  TCCR1B = (1 << WGM12) | (1 << CS12); // прескалер 1 к 256 (62500 тактов в секунду)
  TCNT1 = 0;
}

ISR(USART_UDRE_vect) {
  uint8_t rp = out_queue_read_pos;
  UDR0  = out_queue[rp];
  rp = (rp + 1) & (OUT_QUEUE_SIZE - 1);
  out_queue_read_pos = rp;
  if (rp == out_queue_write_pos) { // Если в очереди не осталось элементов, то отключаем это прерывание
    UCSR0B &= ~(1 << UDRIE0);
  }
}

ISR(USART_RX_vect) {
  uint8_t d = UDR0;
  uint8_t wp = in_queue_write_pos;
  in_queue[wp] = d;
  wp = (wp + 1) & (IN_QUEUE_SIZE - 1);
  in_queue_write_pos = wp;
  if (((wp - in_queue_read_pos) & (IN_QUEUE_SIZE - 1)) >= IN_QUEUE_OFF_THRESHOLD) {
    suspend_rx();
  }
}

static void send(uint8_t b) {
  uint8_t wp = out_queue_write_pos;
  uint8_t nwp = (wp + 1) & (OUT_QUEUE_SIZE - 1);
  while (nwp == out_queue_read_pos) { // Если очередь забита, то ждём
    UCSR0B |= (1 << UDRIE0); // На этом месте прерывание должно быть уже разрешено, но на всякий случай установим флаг ещё раз
  }
  out_queue[wp] = b; // Байтик помещается в очередь
  cli(); // Прерывания блокируются, чтобы исключить ситуацию когда out_queue_write_pos обновлён после чего происходит прерывание, после чего прерывание разрешается при уже пустой очереди
  out_queue_write_pos = nwp;
  UCSR0B |= (1 << UDRIE0); // Безусловное разрешение прерывания в конце
  sei();
}

// Отправляет строку из флеш-памяти
static void send_pgmz(PGM_VOID_P ptr_pgm) {
  uint8_t b;
  while ((b = pgm_read_byte(ptr_pgm++))) {
    send(b);
  }
}

// Отправляет строку из еепром с маскировкой символов
static void send_eepromz(uint16_t ee_addr, uint8_t maxlen) {
  uint8_t b;
  while (maxlen-- && (b = eeprom_read(ee_addr++, 0xFF))) {
    if ((b == '\\') || (b == ',') || (b == '\"')) send('\\');
    send(b);
  }
}

// Отправляет строку из ОЗУ. содержимое буфера должно оставаться неизменным до завершения отправки
static void send_buf(uint8_t * data, uint8_t len) {
  while (len--) {
    send(*(data++));
  }
}

static void send_crlf() {
  send('\r');
  send('\n');
}

static const PROGMEM uint16_t _text_num_scalers[] = {1, 10, 100, 1000, 10000};
  
// Отправляет десятичное представление числа 
void send_num(uint16_t num) {
  uint8_t outme = 0;
  for (int8_t d = 4; d >= 0; d--) {
    uint8_t r = 0;
    uint16_t scaler = pgm_read_word(&_text_num_scalers[d]);
    while (num >= scaler) {
      r++;
      num -= scaler;
    }
    if (r) 
      outme = 1;
    if (outme || !d) send(r + '0');
  }
}

static const PROGMEM uint8_t str_at[] = "AT\r\n";
static const PROGMEM uint8_t str_rst[] = "AT+RST\r\n";
static const PROGMEM uint8_t str_init_uart[] = "AT+UART_CUR=1000000,8,1,0,2\r\n";
static const PROGMEM uint8_t str_ate0[] = "ATE0\r\n";
static const PROGMEM uint8_t str_cipclose_a[] = "AT+CIPCLOSE=";
static const PROGMEM uint8_t str_cipmux[] = "AT+CIPMUX=1\r\n";
static const PROGMEM uint8_t str_cwdhcp_st[] = "AT+CWDHCP_CUR=1,1\r\n";
static const PROGMEM uint8_t str_cwdhcp_ap[] = "AT+CWDHCP_CUR=0,1\r\n";
static const PROGMEM uint8_t str_cipsta_a[] = "AT+CIPSTA_CUR=\"";
static const PROGMEM uint8_t str_cwmode_a[] = "AT+CWMODE_CUR=";
static const PROGMEM uint8_t str_cipap_a[] = "AT+CIPAP_CUR=\"";
static const PROGMEM uint8_t str_cwjap_q[] = "AT+CWJAP_CUR?\r\n";
static const PROGMEM uint8_t str_cwjap_a[] = "AT+CWJAP_CUR=\"";
static const PROGMEM uint8_t str_cwsap_a[] = "AT+CWSAP_CUR=\"";
static const PROGMEM uint8_t str_cipsend_a[] = "AT+CIPSENDBUF=";
static const PROGMEM uint8_t str_cipserver_a[] = "AT+CIPSERVER=1,";
static const PROGMEM uint8_t str_cipserver0[] = "AT+CIPSERVER=0\r\n";
static const PROGMEM uint8_t str_quot_comma_quot[] = "\",\"";
static const PROGMEM uint8_t str_quot_crlf[] = "\"\r\n";

static void to_state(uint8_t new_state, uint16_t timeout) {
  wifiman_state = new_state;
  wifiman_timeout = timeout;
}

static uint8_t check_or_restart(uint8_t parsed) {
  if (parsed == PARSED_OK) return 1;
  if (!parsed || (parsed == PARSED_ERROR) || (parsed == PARSED_FAIL)) { // Сброс только если таймаут, или ошибка
    wifiman_delay = DELAY_ONE_SECOND * 2; 
    wifiman_state = WIFIMAN_INIT_REQUIRED;
  }
  return 0;
}

static uint8_t check_or_init_ap(uint8_t parsed) {
  if (parsed == PARSED_OK) return 1;
  if (!parsed || (parsed == PARSED_ERROR) || (parsed == PARSED_FAIL)) { // Сброс только если таймаут, или ошибка
    send_pgmz(str_cwmode_a);
    send((CWMODE_AP | CWMODE_ST) + '0');
    send_crlf();
    to_state(WIFIMAN_INIT_CWMODE_AFTER_ST_ERROR, DELAY_ONE_SECOND * 5);
  }
  return 0;
}

static void ee_or_def(PGM_VOID_P prefix, uint16_t eeoff, uint8_t len, PGM_VOID_P def) {
  send_pgmz(prefix);
  if (is_in_eeprom(eeoff)) {
    send_eepromz(eeoff, len);
  } else {
    send_pgmz(def);
  }
}

static uint8_t read() {
  uint8_t rp = in_queue_read_pos;
  while (rp == in_queue_write_pos) {}; // Ждём пока в очереди появится байт
  uint8_t r = in_queue[rp];
  rp = (rp + 1) & (IN_QUEUE_SIZE - 1);
  in_queue_read_pos = rp;
  if (((in_queue_write_pos - rp) & (IN_QUEUE_SIZE - 1)) == IN_QUEUE_ON_THRESHOLD) {
    resume_rx();
  }
  return r;
}

static uint8_t check_pgm(PGM_VOID_P p_pgm) {
  uint8_t b;
  while ((b = pgm_read_byte(p_pgm++))) {
    if (read() != b) {
      parser_state = PARSER_STATE_WAIT_LINE_FEED;
      return 0;
    }
  }
  return 1;
}

static inline uint8_t check(uint8_t b) {
  return (read() == b);
}  

static uint8_t parse() {
  while (packet_len) { // Если у нас на очереди непрочитанный пакет, то игнорируем его
    if (in_queue_read_pos == in_queue_write_pos) return 0; // Если нет данных в буфере - выходим
    read();
    packet_len--;
  }
  if (in_queue_read_pos == in_queue_write_pos) {
    return 0;
  }
  uint8_t b = read();
  if (parser_state == PARSER_STATE_WAIT_LINE_FEED) {
    if ((b == '\r') || (b == '\n')) parser_state =  PARSER_STATE_WAIT_START;
    return 0;
  }
  switch (b) {
    case '>': return PARSED_PROMPT;
    case 'O': 
      if (check_pgm(PSTR("K\r"))) return PARSED_OK;
      break;
    case 'E': 
      if (check_pgm(PSTR("RROR\r"))) return PARSED_ERROR;
      break;
    case 'F': 
      if (check_pgm(PSTR("AIL\r"))) return PARSED_FAIL;
      break;
    case 'S': 
      if (check_pgm(PSTR("END OK\r"))) return PARSED_SEND_OK;
      break;
    case 'b':
      if (check_pgm(PSTR("usy "))) {
        parser_state = PARSER_STATE_WAIT_LINE_FEED;
        return PARSED_BUSY;
      }        
      break;
    case '0' ... '9': 
      if (check(',')) {
        uint8_t d = read();
        if (d == 'C') {
          switch (read()) {
            case 'O': // CONNECT и CONNECT FAIL
              if (check_pgm(PSTR("NNECT"))) {
                switch (read()) {
                  case ' ': 
                    if (check_pgm(PSTR("FAIL\r"))) return EVENT_DISCONNECT | (b - '0');
                    break;
                  case '\r': 
                    return EVENT_CONNECT | (b - '0');
                }
              }
              break;
            case 'L': // CLOSED
              if (check_pgm(PSTR("OSED\r"))) return EVENT_DISCONNECT | (b - '0');
              break;
          }
        } else if ((d >= '0') && (d <= '9')) {
          do {
            d = read();
          } while ((d >= '0') && (d <= '9'));
          if ((d == ',') && check_pgm(PSTR("SEND "))) {
            d = read();
            if ((d == 'O') && check('K')) {
              return PARSED_SEND_OK; 
            } else if ((d == 'F') && check_pgm(PSTR("AIL\r"))) {
              return PARSED_FAIL; 
            }
          }
        } 
      }
      parser_state = PARSER_STATE_WAIT_LINE_FEED;
      break;
    case '+': 
      switch (read()) {
        case 'I': // +IPD
          if (check_pgm(PSTR("PD,"))) {
            uint8_t linkid = read() - '0';
            if ((linkid > 9) || !check(',')) break;
            uint16_t len = 0;
            uint8_t b = read();
            while ((b >= '0') && (b <= '9')) {
              len = len * 10 + (b - '0');
              b = read();
            }
            if ((b != ':') || !len) break;
            packet_len = len;
            return EVENT_PACKET | linkid;
          }
          break;
        case 'C': // +CWJAP_CUR
          if (check_pgm(PSTR("WJAP_CUR:\""))) {
            uint16_t eea = EE_ST_SSID;
            for(;;) {
              uint8_t b = read();
              if (b == '\"') { // Если дошли до конца
                if ((eea == (EE_ST_SSID + 64)) || !eeprom_read(eea, 0xFF)) { // И если в eeprom тоже дошли до конца
                  parser_state = PARSER_STATE_WAIT_LINE_FEED;
                  return PARSED_CWJAP_MATCH;
                }
              }
              if (b == '\\') b = read(); // Если маскирующий символ, просто возьмём следующий
              if (eeprom_read(eea++, 0xFF) != b) break;
            }            
          }            
          break;
      }
    case '\r': case '\n': return 0; // Перевод строки оставляет в этом же состоянии
  }
  parser_state = PARSER_STATE_WAIT_LINE_FEED; // Все остальные случаи, если не вернули что-то осмысленное, значит заканчиваются ожиданием перевода строки
  return 0;
}

/* Обрабатывает вай-фай. Необходимо вызывать циклически.
 * Возвращает код события, или 0, если событие не произошло.
 * Старшие 4ре бита кодирует тип события, младшие 4ре - linkid.
 * */
uint8_t wifiman_pull() {
  uint8_t parsed = parse();
  if (parsed & PARSED_EVENT_MASK) return parsed;
  if (wifiman_state == WIFIMAN_READY) {
    return 0;
  }
  if (parsed == PARSED_BUSY) {
    busys_cnt++;
    if (busys_cnt >= 3) {
      if (wifiman_state != WIFIMAN_INIT_REQUIRED) {
        send_pgmz(&str_rst); // Не помогает, один фиг. Обойти баг помогла замена CIPSEND на CIPSENDBUF
      }
      wifiman_delay = 100;
      wifiman_state = WIFIMAN_INIT_REQUIRED;
    }
    return 0;
  } else if (parsed) {
    busys_cnt = 0;
  }
  if (wifiman_delay) return 0;
  if (!parsed && wifiman_timeout) return 0;
  switch (wifiman_state) {
    case WIFIMAN_INIT_REQUIRED: // Запрошена переиницаилазия. Выполняем команду AT на рабочей скорости
      UBRR0 = UART_UBRR_WORK;
      send_pgmz(&str_at);
      to_state(WIFIMAN_INIT_UART_TEST, DELAY_ONE_SECOND / 2);
      break;
    case WIFIMAN_INIT_UART_TEST:
      if (!parsed) { // Время ожидания ответа вышло, настраиваем скорость порта на скорости по-умолчанию
//        wifiman_delay = DELAY_ONE_SECOND * 2; 
//        wifiman_state = WIFIMAN_INIT_REQUIRED;
        UBRR0 = UART_UBRR_DEFAULT;
        send_pgmz(str_init_uart);
        to_state(WIFIMAN_INIT_UART_DEF_SET, DELAY_ONE_SECOND / 2);
      } else { // Получили ответ на рабочей скорости
        send_pgmz(str_ate0);
        to_state(WIFIMAN_INIT_ATE0, DELAY_ONE_SECOND / 2);
      }
      break;
    case WIFIMAN_INIT_UART_DEF_SET: 
      wifiman_delay = DELAY_ONE_SECOND / 4;
      wifiman_state = WIFIMAN_INIT_UART_DEF_DELAY;
      break;
    case WIFIMAN_INIT_UART_DEF_DELAY: 
      UBRR0 = UART_UBRR_WORK; // Не важно, получили ответ, или нет, настраиваем порт на новой скорости
      send_pgmz(str_init_uart);
      to_state(WIFIMAN_INIT_UART_WORK_SET, DELAY_ONE_SECOND / 2);
      break;
    case WIFIMAN_INIT_UART_WORK_SET:
      if (check_or_restart(parsed)) { 
        wifiman_delay = DELAY_ONE_SECOND / 4;
        wifiman_state = WIFIMAN_INIT_UART_WORK_DELAY;
      }
      break;
    case WIFIMAN_INIT_UART_WORK_DELAY:
      send_pgmz(str_ate0);
      to_state(WIFIMAN_INIT_ATE0, DELAY_ONE_SECOND / 2);
      break;
    case WIFIMAN_INIT_ATE0:
      if (check_or_restart(parsed)) { 
        send_pgmz(str_cipclose_a);
        send('5');
        send_crlf();
        to_state(WIFIMAN_INIT_CIPCLOSE, DELAY_ONE_SECOND);
      }       
      break;
    case WIFIMAN_INIT_CIPCLOSE:
      if ((parsed == PARSED_OK) || (parsed == PARSED_ERROR)) { 
        send_pgmz(str_cipmux);
        to_state(WIFIMAN_INIT_CIPMUX, DELAY_ONE_SECOND / 2);
      } else {
        wifiman_delay = DELAY_ONE_SECOND * 2; 
        wifiman_state = WIFIMAN_INIT_REQUIRED;
      }
      break;       
    case WIFIMAN_INIT_CIPMUX:
      if (check_or_restart(parsed)) { 
        send_pgmz(str_cipserver0);
        to_state(WIFIMAN_INIT_CIPSERVERSTOP, DELAY_ONE_SECOND * 5);
      }
      break;
    case WIFIMAN_INIT_CIPSERVERSTOP:
      if (!parsed || (parsed == PARSED_OK) || (parsed == PARSED_ERROR)) {  // Не важно, что получили в ответ
        send_pgmz(str_cwmode_a);
        send(is_in_eeprom(EE_ST_SSID) ? (CWMODE_ST + '0') : (CWMODE_AP + '0'));
        send_crlf();
        to_state(WIFIMAN_INIT_CWMODE, DELAY_ONE_SECOND * 5);
      }
      break;
    case WIFIMAN_INIT_CWMODE:
      if (check_or_restart(parsed)) { 
        if (is_in_eeprom(EE_ST_SSID)) { // Если режим - станция
          if (is_in_eeprom(EE_ST_IP)) { // Если задан IP, то настроим IP
            send_pgmz(&str_cipsta_a);
            send_eepromz(EE_ST_IP, 32);
            send_pgmz(&str_quot_comma_quot);
            send_eepromz(EE_ST_GATE, 32);
            send_pgmz(&str_quot_comma_quot);
            send_eepromz(EE_ST_MASK, 32);
            send_pgmz(&str_quot_crlf);             
          } else { // Если IP не задан, то включим DHCP для точки
            send_pgmz(&str_cwdhcp_st);
          }
          to_state(WIFIMAN_INIT_CIPSTA_DHCP, DELAY_ONE_SECOND);
        } else { 
          // Если текущий режим - точка доступа, настроим DHCP для точки доступа
          send_pgmz(&str_cwdhcp_ap);
          to_state(WIFIMAN_INIT_CWDHCP_AP, DELAY_ONE_SECOND);
        }        
      }        
      break;
    case WIFIMAN_INIT_CWMODE_AFTER_ST_ERROR:
      if (check_or_restart(parsed)) {
        send_pgmz(&str_cwdhcp_ap);
        to_state(WIFIMAN_INIT_CWDHCP_AP, DELAY_ONE_SECOND);
      }
      break;
    case WIFIMAN_INIT_CWDHCP_AP:
      if (check_or_restart(parsed)) {
        ee_or_def(&str_cipap_a, EE_AP_IP, 32, &str_default_ap_ip);
        ee_or_def(&str_quot_comma_quot, EE_AP_GATE, 32, &str_default_ap_gate);
        ee_or_def(&str_quot_comma_quot, EE_AP_MASK, 32, &str_default_ap_mask);
        send_pgmz(&str_quot_crlf);
        to_state(WIFIMAN_INIT_CIPAP, DELAY_ONE_SECOND * 5);
      }
      break;
    case WIFIMAN_INIT_CIPAP:
      if (check_or_restart(parsed)) {
        ee_or_def(&str_cwsap_a, EE_AP_SSID, 64, &str_default_ap_ssid);
        ee_or_def(&str_quot_comma_quot, EE_AP_PWD, 64,  &str_default_ap_pwd);
        send('\"');
        send(',');
        send_num(eeprom_read(EE_AP_CHAN, DEFAULT_AP_CHAN));
        send_pgmz(PSTR(",3\r\n")); // шифрование WPA2_PSK
        to_state(WIFIMAN_INIT_CWSAP, DELAY_ONE_SECOND * 5);
      }
      break;
    case WIFIMAN_INIT_CIPSTA_DHCP:
      if (check_or_init_ap(parsed)) {
        send_pgmz(&str_cwjap_q); // Запрос текущего подключения к точке
        to_state(WIFIMAN_INIT_CWJAP_TEST, DELAY_ONE_SECOND * 5);
      }
      break;
    case WIFIMAN_INIT_CWJAP_TEST:
      if (parsed == PARSED_CWJAP_MATCH) { // Если было совпадение, то следующе состояние WIFIMAN_INIT_CWJAP_SET, которое дождётся строки OK
        wifiman_state = WIFIMAN_INIT_CWJAP_SET;
        to_state(WIFIMAN_INIT_CWJAP_SET, DELAY_ONE_SECOND / 2); 
      } else  if (check_or_init_ap(parsed)) {
        send_pgmz(&str_cwjap_a);
        send_eepromz(EE_ST_SSID, 64);
        send_pgmz(&str_quot_comma_quot);
        send_eepromz(EE_ST_PWD, 64);
        send_pgmz(&str_quot_crlf);
        to_state(WIFIMAN_INIT_CWJAP_SET, DELAY_ONE_SECOND * 60); // На установку подключения может уходить уйма времени
      }
      break;
    case WIFIMAN_INIT_CWSAP:
    case WIFIMAN_INIT_CWJAP_SET:
      if (((wifiman_state == WIFIMAN_INIT_CWSAP) && check_or_restart(parsed)) || ((wifiman_state != WIFIMAN_INIT_CWSAP) && check_or_init_ap(parsed))) {
        uint16_t port = eeprom_read_int16(EE_PORT, DEFAULT_PORT);
        send_pgmz(&str_cipserver_a);
        send_num(port);
        send_crlf();
        to_state(WIFIMAN_INIT_CIPSERVER, DELAY_ONE_SECOND * 5);
      }
      break;
    case WIFIMAN_INIT_CIPSERVER:
      if (check_or_restart(parsed)) { // Если удалось запустить порт на прослушку, то всё готово.
        wifiman_state = WIFIMAN_READY;
        wifiman_timeout = 0;
        wifiman_delay = 0;
      }
      break;
      
    case WIFIMAN_CIPSEND:
      if (parsed == PARSED_PROMPT) {
        if (wifiman_send.type == SEND_TYPE_PGM) {
          send_pgmz(wifiman_send.ptr_pgm);
        } else {
          send_buf(wifiman_send.ptr_ram, wifiman_send.size);
        }
        to_state(WIFIMAN_CIPSEND_DATA, DELAY_ONE_SECOND * 5);
      } else if ((parsed != PARSED_OK) || !wifiman_timeout) { // Сначала приходит строка OK, поэтому проигнорируем её.
        wifiman_state = WIFIMAN_READY;
        wifiman_timeout = 0;
        wifiman_delay = 0;
      }      
      break;
    case WIFIMAN_CIPSEND_DATA: 
      if (parsed == PARSED_SEND_OK) { 
        wifiman_state = WIFIMAN_READY;
        wifiman_timeout = 0;
        wifiman_delay = 0;
        return EVENT_SENT_OK;
      } else if ((parsed == PARSED_FAIL) || (parsed == PARSED_ERROR) || !parsed) {
        wifiman_state = WIFIMAN_READY;
        wifiman_timeout = 0;
        wifiman_delay = 0;
        return EVENT_SENT_ERROR;
      }
      break;
    case WIFIMAN_DISCONNECT:
      wifiman_state = WIFIMAN_READY;
      wifiman_timeout = 0;
      wifiman_delay = 0;
      break;
  }
  return 0;
}

/* В цикле вызывает wifiman_pull() пока таймер не переполнится.
 * Если произошло событие, то немедленно возвращает код события */

uint8_t wifiman_wait_frame() {
  do {
    uint8_t r = wifiman_pull();
    if (r) return r;
  } while (!next_frame);
  next_frame = 0;
  return 0;
}


/* Размер оставшихся для чтения данных текущего пакета */
uint16_t wifiman_packet_len() {
  return packet_len;
}

/* Читает очередной байт из пакета. Если достигнут конец пакета, будет возвращать нули */
uint8_t wifiman_read() {
  if (!packet_len) return 0;
  packet_len--;
  return read();
}

uint8_t wifiman_ready() {
  return wifiman_state == WIFIMAN_READY;
}

void start_send(uint8_t linkid, uint8_t size) {
  send_pgmz(&str_cipsend_a);
  send(linkid + '0');
  send(',');
  send_num(size);
  send_crlf();
  to_state(WIFIMAN_CIPSEND, DELAY_ONE_SECOND / 2);
}

void wifiman_send_pgmz(uint8_t linkid, PGM_VOID_P p_pgm) {
  if (wifiman_state != WIFIMAN_READY) return;
  wifiman_send.ptr_pgm = p_pgm;
  uint8_t c = 0;
  while (pgm_read_byte(p_pgm++)) c++;
  wifiman_send.size = c;
  wifiman_send.type = SEND_TYPE_PGM;
  start_send(linkid, c);
}

void wifiman_send_buf(uint8_t linkid, void * buf, uint8_t len) {
  if (wifiman_state != WIFIMAN_READY) return;
  wifiman_send.ptr_ram = buf;
  wifiman_send.size = len;
  wifiman_send.type = SEND_TYPE_RAM;
  start_send(linkid, len);
}

void wifiman_request_reinit() {
  if (wifiman_state != WIFIMAN_READY) return;
  wifiman_state = WIFIMAN_INIT_REQUIRED;
}

uint8_t wifiman_suspend_cts() {
  uint8_t r = !(PORT(CTS_PORT) & CTS);
  suspend_rx();
  return r;
}

/* Закрывает имеющееся подключение */
void wifiman_close(uint8_t linkid) {
  if (wifiman_state != WIFIMAN_READY) return;
  send_pgmz(&str_cipclose_a);
  send(linkid + '0');
  send_crlf();
  to_state(WIFIMAN_DISCONNECT, DELAY_ONE_SECOND);
}

void wifiman_restore_cts(uint8_t prev) {
  if (prev) resume_rx();
}

void wifiman_wait_outbuf() {
  while (out_queue_write_pos != out_queue_read_pos);
  while (!(UCSR0A & (1 <<UDRE0)));
  return;
}



