/*
 * YolkaBootloader.c
 *
 * Created: 24.11.2015 1:40:50
 *  Author: Дмитрий
 */ 


#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>

#define BOOTLOADER_OFFSET (FLASHEND - 4095)

#define LED_DATA_DDR DDRB // Регистр направления порта подключения линейки (линеек) светодиодов
#define LED_DATA_PORT PORTB // Регистр данных порта подключения линейки (линеек) светодиодов
#define LED_DATA_PIN_NUM 0 // Номер пина порта, к которому подключен DIN первой линейки
#define LED_DATA (1 << LED_DATA_PIN_NUM)

#define FORCE_PORT PORTB
#define FORCE_DDR DDRB
#define FORCE_PIN PINB
#define FORCE_A_PIN_NUM 3 // MOSI
#define FORCE_B_PIN_NUM 5 // SCK
#define FORCE_A (1 << FORCE_A_PIN_NUM)
#define FORCE_B (1 << FORCE_B_PIN_NUM)

#define CTR_PORT PORTD
#define CTR_DDR DDRD
#define CTR_PIN_NUM 2
#define CTR (1 << CTR_PIN_NUM)

#define COLOR_BLUE 1
#define COLOR_GREEN 2
#define COLOR_RED 4
#define COLOR_YELLOW (COLOR_RED | COLOR_GREEN)
#define COLOR_CYAN (COLOR_BLUE | COLOR_GREEN)
#define COLOR_MAGENTA (COLOR_RED | COLOR_BLUE)
#define COLOR_WHITE (COLOR_BLUE | COLOR_GREEN | COLOR_RED)

#define CALC_UBRR(osc) ((F_CPU + (osc * 4)) / (osc * 8) - 1) // формула для вычисления значения регистра UBRR для 2x скорости

#define UART_UBRR_DEFAULT CALC_UBRR(115200)
#define UART_UBRR_WORK CALC_UBRR(1000000)

#define EE_MAGIC 0 // позиция в EEPROM, где хранится код, означающий что прошивка встала нормально (устанавливается самой прошивкой)
#define MAGIC_PROGRAMMED 0x83 // сам код, который должен быть установлен прошивкой

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

#define DEFAULT_AP_CHAN 6
#define DEFAULT_PORT 3388

static const PROGMEM uint8_t str_default_ap_ssid[] = "WIFI_YOLKA";
static const PROGMEM uint8_t str_default_ap_pwd[] = "0123456789";
static const PROGMEM uint8_t str_default_ap_ip[] = "192.168.10.1";
#define str_default_ap_gate str_default_ap_ip // Сэкономим пару байт
static const PROGMEM uint8_t str_default_ap_mask[] = "255.255.255.0";
static const PROGMEM uint8_t str_greeting[] = "Yolka Boot Loader 1.0";
static const PROGMEM uint8_t str_startrequest[] = "YOLKA_BLST";


#define BRIGHTNESS 0xFF

uint8_t uart_inbuf[256];
uint8_t uart_inbuf_readpos = 0; // Позиция чтения
uint8_t uart_inbuf_cnt = 0; // Количество байт, доступных для чтения
uint8_t read_timeout;
uint8_t led_blink_counter;
uint8_t led_blink_mask;
uint8_t load_defaults;
uint8_t led_blink_if0, led_blink_if1; 
uint8_t force_defaults;


// Гасит все светодиоды (150 штук), выводит 150 * 24 импульсов соответствующих нулю
static void __attribute__((optimize("O1"))) clear_leds() {
  for (uint16_t i = 150 * 24; i; i--) {
    LED_DATA_PORT |= LED_DATA;
    asm volatile ("rjmp .+0");
    asm volatile ("nop");
    LED_DATA_PORT &= ~LED_DATA;
    asm volatile ("rjmp .+0");
    asm volatile ("rjmp .+0");
    asm volatile ("rjmp .+0");
    asm volatile ("rjmp .+0");
    asm volatile ("nop");
  }
}


// Вывод значений на первый светодиод
void __attribute__((optimize("O1"))) out_led(uint8_t pix) {
  uint8_t t = CTR_PORT;
  CTR_PORT |= CTR;  // Поскольку вывод на 1 светодиод занимает 480 тактов, притормозим загрузку с UART
  union {
    uint8_t a[4];
    uint32_t l;
  } d ;
  d.a[0] = pix & 1 ? BRIGHTNESS : 0;
  d.a[1] = pix & 2 ? BRIGHTNESS : 0;
  d.a[2] = pix & 4 ? BRIGHTNESS : 0;
  for (uint8_t i = 24; i; i--) {
    LED_DATA_PORT |= LED_DATA;
    if (!(d.l & 0x800000)) {
      asm volatile ("nop");
      LED_DATA_PORT &= ~LED_DATA;
      d.l <<= 1;
      asm volatile ("rjmp .+0");
      asm volatile ("nop");
    } else {
      asm volatile ("nop");
      asm volatile ("rjmp .+0");
      asm volatile ("rjmp .+0");
      d.l <<= 1;
      LED_DATA_PORT &= ~LED_DATA;
    }
    asm volatile ("rjmp .+0");
    asm volatile ("rjmp .+0");
  }
  CTR_PORT = t;
}


// Проверяет, поступили ли новые данные в UART, и начитывает их в очередь
static __attribute__((optimize("s"))) void pull_uart() {
  if (UCSR0A & (1 << RXC0)) { // Если данные таки есть
    uint8_t d = UDR0; 
    if (uart_inbuf_cnt < 255) { // Начитывает только если есть место
      uint8_t rp = uart_inbuf_readpos + uart_inbuf_cnt; 
      uart_inbuf[rp] = d;
      uart_inbuf_cnt++;
      if (uart_inbuf_cnt > 250) {
        CTR_PORT |= CTR; // Буфер близок к заполнению, запрещаем передачу
      }
    }
  }
}

// Проверяет таймер и UART, выводит мигание на светодиод
static void pull() { 
  pull_uart();
  if (TIFR1 & (1 << OCF1A)) { // Если таймер переполнился
    TIFR1 |= (1 << OCF1A); // Сбрасываем флаг
    if (read_timeout) read_timeout--;
    out_led((led_blink_counter++ & led_blink_mask) ? led_blink_if1 : led_blink_if0);
    pull_uart();
  }
}


static void eeprom_wait() {
  while(EECR & (1<<EEPE)) pull(); 
}

static uint8_t eeprom_read(uint16_t address) {
  EEAR = address;
  EECR |= (1<<EERE);
  return EEDR;
}
  
static void eeprom_write(uint16_t address, uint8_t data) {
  EEAR = address;
  EECR |= (1<<EERE);  
  uint8_t d = EEDR;
  if (d != data) {
    EEDR = data;
    EECR |= (1<<EEMPE);
    EECR |= (1<<EEPE);
    eeprom_wait();
  }
}

static uint8_t is_in_eeprom(uint16_t address) {
  return !force_defaults && (eeprom_read(address) != 0xFF);
}


// Возвращает байт, или -1 - если таймаут
static int16_t uart_read() {
  pull();
  while (!uart_inbuf_cnt) { 
    pull();
    if (!read_timeout) 
      return -1;
  }    
  uart_inbuf_cnt--;
  if (uart_inbuf_cnt == 128) {
    CTR_PORT &= ~CTR; // Когда буфер опустошается, снова разрешаем передачу
  }
  return uart_inbuf[uart_inbuf_readpos++];
}

// Отправялет один байт
static void uart_send(uint8_t byte) {
  while (!(UCSR0A & (1 << UDRE0))) pull();
  UDR0 = byte;
}

// Отправляет строку, завершённую нулём, из флеш-памяти
static void uart_send_pgm(PGM_VOID_P ptr) {
  for(;;) {
    uint8_t b = pgm_read_byte(ptr++);
    if (!b) return;
    uart_send(b);
    pull();
  }
}

// Отправляет перевод строки
static void uart_crlf() {
  uart_send('\r');
  uart_send('\n');
}

// Отправляет строку, завершённую нулём, или ограниченную по длине, из EEPROM
static void uart_send_ee(uint16_t offset, uint8_t limit) {
  for (;limit;limit--) {
    uint8_t b = eeprom_read(offset++);
    if (!b) return;
    if ((b == '"') || (b == '\\') || (b == ','))
      uart_send('\\');
    uart_send(b);
    pull();
  }
}

static const PROGMEM uint16_t _text_num_scalers[] = {1, 10, 100, 1000, 10000};

// Отправляет десятичное представление числа в UART
static void uart_send_num(uint16_t num) {
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
    if (outme || !d) uart_send(r + 48);
    pull();
  }
}

static void setblink_x(uint16_t data) {
  led_blink_if0 = data >> 12; //decode_color(data >> 12);// pgm_read_dword(&colors[color1]);
  led_blink_if1 = data >> 8; //decode_color(data >> 8);//pgm_read_dword(&colors[color2]);
  led_blink_mask = data;
}

// Экономим память на вызовах, пакуя три константы в один параметр
#define setblink(mask, color1, color2) setblink_x((color1 << 12) | (color2 << 8) | mask)

static void pause(uint8_t tm) {
  read_timeout = tm;
  do {
    pull();
    uart_inbuf_cnt = 0;
    CTR_PORT &= ~CTR;
  } while (read_timeout);
}

#define REPLY_OK 1
#define REPLY_ERROR 0 
#define REPLY_TIMEOUT -1
#define REPLY_FAIL -2


#define CHECK_TIMEOUT -1
#define CHECK_OK 256

// Сверяет приходящие символы с заданной строкой
// Возвращает -1 - таймаут 256 - совпадение со строкой, иначе - последний полученный символ
static int16_t check_string(PGM_VOID_P str) {
  for(;;) {
    uint8_t b = pgm_read_byte(str++);
    if (!b) return CHECK_OK;
    int16_t d = uart_read();
    if (d < 0) return CHECK_TIMEOUT;
    if ((uint8_t)d != b) return d;
  }    
}

int16_t check_string_ee(uint16_t offset, uint8_t limit) {
  for (;limit;limit--) {
    uint8_t b = eeprom_read(offset++);
    if (!b) return CHECK_OK;
    if ((b == '"') || (b == '\\') || (b == ',')) {
      int16_t d = uart_read();
      if (d < 0) return CHECK_TIMEOUT;
      if ((uint8_t)d != '\\') return d;
    }
    int16_t d = uart_read();
    if (d < 0) return CHECK_TIMEOUT;
    if ((uint8_t)d != b) return d;
  }   
  return CHECK_OK; 
}

static const PROGMEM uint8_t str_chk_ok[] = "K\r";
static const PROGMEM uint8_t str_chk_fail[] = "AIL\r";
static const PROGMEM uint8_t str_chk_error[] = "RROR\r";
static const PROGMEM uint8_t str_chk_send_ok[] = "END OK";

// Игнорирует все символы, пока не встретятся переводы строки. Возвращает первый символ, следующий за переводами строк
// d - последний полученный символ
static int16_t skip_to_next_line(int16_t d) {
  while (((uint8_t)d != '\r') && ((uint8_t)d != '\n')) {
    if (d < 0) return d;
    d = uart_read();
  }      
  while (((uint8_t)d == '\r') || ((uint8_t)d == '\n')) {
    d = uart_read();
  }
  return d;
}

// Дожидается одной из строк OK (возвращает 1) ERROR (возвращает 0) FAIL (-2), или возвращает -1 при таймауте
static int8_t wait_reply(uint8_t timeout) {
  read_timeout = timeout;
  int16_t d = uart_read();
  do {
    if (d < 0) {
      return REPLY_TIMEOUT;
    } else if ((uint8_t)d == 'O') {
      d = check_string(&str_chk_ok);
      if (d == CHECK_OK) return REPLY_OK;
    } else if ((uint8_t)d == 'F') {
      d = check_string(&str_chk_fail);
      if (d == CHECK_OK) return REPLY_FAIL;
    } else if ((uint8_t)d == 'E') {
      d = check_string(&str_chk_error);
      if (d == CHECK_OK) return REPLY_ERROR;
    }
    d = skip_to_next_line(d);
  } while (read_timeout);
  return REPLY_TIMEOUT;
}


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
static const PROGMEM uint8_t str_cipsend_a[] = "AT+CIPSEND=";
static const PROGMEM uint8_t str_cipserver_a[] = "AT+CIPSERVER=1,";
static const PROGMEM uint8_t str_quot_comma_quot[] = "\",\"";
static const PROGMEM uint8_t str_quot_crlf[] = "\"\r\n";


#define CWMODE_AP 2
#define CWMODE_ST 1

static void ee_or_def(PGM_VOID_P prefix, uint16_t eeoff, uint8_t len, PGM_VOID_P def) {
  uart_send_pgm(prefix);
  if (is_in_eeprom(eeoff))
    uart_send_ee(eeoff, 64);
  else
    uart_send_pgm(def);
}

static void wifi_init(uint8_t needwait) {
  for(;;) {
    if (needwait) {
      pause(10);
    }
    needwait = 1;
    UBRR0 = UART_UBRR_DEFAULT;
    uart_crlf();
    wait_reply(2);
    uart_send_pgm(&str_init_uart);
    if (wait_reply(3) != REPLY_TIMEOUT) {
      pause(2);
    }      
    UBRR0 = UART_UBRR_WORK;
    uart_crlf();
    wait_reply(2);
    uart_send_pgm(&str_init_uart);
    int8_t r;
    r = wait_reply(5);
    if (r == REPLY_TIMEOUT) {
      setblink(8, COLOR_RED, 0);
      continue;
    } 
    pause(2);
    // Отключаем эхо
    uart_send_pgm(&str_ate0);
    wait_reply(2);

    // Закрываем все подключения, если есть
    uart_send_pgm(&str_cipclose_a);
    uart_send('5');
    uart_crlf();
    wait_reply(5);

    // Режим множественных подключений    
    uart_send_pgm(&str_cipmux);
    if (wait_reply(10) != REPLY_OK) {
      setblink(8, COLOR_RED, COLOR_YELLOW);
      continue;
    } 

    uint8_t mode = is_in_eeprom(EE_ST_SSID) ? CWMODE_ST : CWMODE_AP;
    // 1 - станция, 2 - точка доступа, 3 - и то и другое
    
    // Настройка текущего режима   
    uart_send_pgm(&str_cwmode_a);
    uart_send(mode + 48);
    uart_crlf();
    if (wait_reply(10) != REPLY_OK) {
      setblink(8, COLOR_RED, COLOR_GREEN);
      continue;
    } 
    
    if (mode & CWMODE_ST) { // Если работаем в режиме станции (подключаемся к точке доступа)
      if (is_in_eeprom(EE_ST_IP)) { // Если адрес задан, то загрузим его
        uart_send_pgm(&str_cipsta_a);
        uart_send_ee(EE_ST_IP, 32);
        uart_send_pgm(&str_quot_comma_quot);
        uart_send_ee(EE_ST_GATE, 32);
        uart_send_pgm(&str_quot_comma_quot);
        uart_send_ee(EE_ST_MASK, 32);
        uart_send_pgm(&str_quot_crlf);
      } else { // Если адрес не задан, попробуем включить DHCP
        uart_send_pgm(&str_cwdhcp_st);
      }
      if (wait_reply(10) != REPLY_OK) {
        mode |= CWMODE_AP; // Если не получается в режиме станции, запустим режим и станции и точки
      } else {
        // Затем проверим, есть ли подключение?
        uart_send_pgm(&str_cwjap_q);
        read_timeout = 10;
        int16_t d = uart_read();
        uint8_t addrok = 0;
        while (d >= 0) {
          if ((uint8_t)d == '+') {
            d = check_string(PSTR("CWJAP_CUR:\""));
            if (d == CHECK_OK) {
              d = check_string_ee(EE_ST_SSID, 64);
              if (uart_read() == '\"') {
                addrok = 1;
              }
            }
          } else if ((uint8_t)d == 'E') {
            d = check_string(&str_chk_error);
            if (d == CHECK_OK) {
              mode |= CWMODE_AP;
              break;
            }            
          } else if ((uint8_t)d == 'O') {
            d = check_string(&str_chk_error);
            if (d == CHECK_OK) break;
          }
          d = skip_to_next_line(d);
        }
        if (!addrok) { // Если текущего подключения нет, или адрес не совпадает с тем, что задан в EEPROM, то переподключимся
          setblink(2, COLOR_BLUE, COLOR_YELLOW);
          uart_send_pgm(&str_cwjap_a);
          uart_send_ee(EE_ST_SSID, 64);
          uart_send_pgm(&str_quot_comma_quot);
          uart_send_ee(EE_ST_PWD, 64);
          uart_send_pgm(&str_quot_crlf);
          if (wait_reply(255) != REPLY_OK) {
            mode |= CWMODE_AP; // Если не получается в режиме станции, запустим режим и станции и точки
          } else {
            setblink(2, COLOR_GREEN, 0);
          }
        }
      }        
    }
    if (mode & CWMODE_AP) { // Если режим подключения - точка доступа
      if (mode & CWMODE_ST) { // Если задан также и режим станции, значит была неудачная попытка подключения, переключаем режим модуля
        uart_send_pgm(&str_cwmode_a);  
        uart_send(mode + 48);
        uart_crlf();
        if (wait_reply(10) != REPLY_OK) {
          setblink(8, COLOR_RED, COLOR_MAGENTA);
          continue;
        } 
      }
      uart_send_pgm(&str_cwdhcp_ap);
      wait_reply(10);
      
      // Настройка адреса
      
      ee_or_def(&str_cipap_a, EE_AP_IP, 32, &str_default_ap_ip);
      ee_or_def(&str_quot_comma_quot, EE_AP_GATE, 32, &str_default_ap_gate);
      ee_or_def(&str_quot_comma_quot, EE_AP_MASK, 32, &str_default_ap_mask);
      uart_send_pgm(&str_quot_crlf);
      wait_reply(50);
      
      
      ee_or_def(&str_cwsap_a, EE_AP_SSID, 64, &str_default_ap_ssid);
      ee_or_def(&str_quot_comma_quot, EE_AP_PWD, 64,  &str_default_ap_pwd);
      uart_send('\"');
      uart_send(',');
      uint8_t chan = eeprom_read(EE_AP_CHAN);
      uart_send_num(((chan != 255) && !force_defaults) ? chan : DEFAULT_AP_CHAN);
      uart_send_pgm(PSTR(",3\r\n")); // шифрование WPA2_PSK
      
      if (wait_reply(50) != REPLY_OK) {
        setblink(8, COLOR_RED, COLOR_CYAN);
        continue;
      } 
      
        
    }
    
    uint16_t port = eeprom_read(EE_PORT) | (eeprom_read(EE_PORT + 1) << 8);
    if ((port == 0xFFFF) || force_defaults) port = DEFAULT_PORT;
    uart_send_pgm(&str_cipserver_a);
    uart_send_num(port);
    uart_crlf();
    if (wait_reply(50) != REPLY_OK) {
      setblink(0, COLOR_RED, COLOR_RED);
      continue;
    } 
    
    setblink(1, COLOR_WHITE, (mode == (CWMODE_ST | CWMODE_AP)) ? COLOR_RED : 0);
    return;
  }  
}

uint8_t linkid = 255;

uint8_t start_send(uint8_t lkid, uint8_t len) {
  read_timeout = 5;
  if (lkid == 255) return 0;
  uart_send_pgm(&str_cipsend_a);
  uart_send(lkid + '0');
  uart_send(',');
  uart_send_num(len);
  uart_crlf();
  read_timeout = 5;
  int16_t d = uart_read();
  while (d >= 0) {
    if ((uint8_t)d == '>') return 1;
    d = skip_to_next_line(d);
  }   
  return 0;
}

static uint8_t finish_send() {
  read_timeout = 5;
  int16_t d = uart_read();
  while (d >= 0) {
    if ((uint8_t)d == 'E') {
      d = check_string(&str_chk_error);
      if (d == CHECK_OK) return 0;
    } else if ((uint8_t)d == 'S') {
      d = check_string(&str_chk_send_ok);
      if (d == CHECK_OK) return 1;
    }
    d = skip_to_next_line(d);
  }   
  return 0;
}

static uint8_t ip_send_pgm(uint8_t lkid, PGM_VOID_P ptr) {
  PGM_VOID_P p = ptr;
  uint8_t l = 0;
  while (pgm_read_byte(p++) != 0) {
    l++;
  }    
  if (start_send(lkid, l)) {
    for(;;) {
      uint8_t b = pgm_read_byte(ptr++);
      if (b == 0) break;
      uart_send(b);
    }
    return finish_send();  
  } else {
    return 0;
  }
}

static void uart_skip(uint16_t cnt) {
  read_timeout = 5;
  while (cnt && (uart_read() >= 0)) cnt--;
}

// Функция обеспечивает приём входящих подключений, начальную инициализацию (приветствие-запрос-ответ) и возвращает управление только когда идёт приём пакета от клиента.
// Возвращаемое значение - количество байт в пакете, которые нужно вычитать из UART
static uint16_t wait_packet() {
  int16_t d;
  read_timeout = 255;
  d = uart_read();
  for(;;) {
    while (d < 0) {
      read_timeout = 255;
      d = uart_read();
    }
    if (((uint8_t)d >= '0') && ((uint8_t)d <= '9')) {
      uint8_t cn = d - '0';
      if (((uint8_t)(d = uart_read()) == ',') && ((uint8_t)(d = uart_read()) == 'C')) {
        d = uart_read();
        if ((uint8_t)d == 'L') {
          if ((d = check_string(PSTR("OSED\r")) == CHECK_OK)) {
            if (cn == linkid) {
              linkid = 255;
              setblink(1, COLOR_WHITE, 0);
            }              
            d = uart_read();
          }           
        } else if ((uint8_t)d == 'O') {
          if ((d = check_string(PSTR("NNECT\r")) == CHECK_OK)) {
            ip_send_pgm(cn, &str_greeting);
            read_timeout = 255;
            d = uart_read();
          }           
        }
      }        
    } else if ((uint8_t)d == '+') {
      if ((d = check_string(PSTR("IPD,")) == CHECK_OK)) {
        d = uart_read();
        if (((uint8_t)d >= '0') && ((uint8_t)d <= '9')) {
          uint8_t cn = (uint8_t)d - '0';
          if ((uint8_t)(d = uart_read()) == ',') {
            uint16_t c = 0;
            d = uart_read();
            while (((uint8_t)d >= '0') && ((uint8_t)d <= '9')) {
              c = c * 10 + d - '0';
              d = uart_read();
            }              
            if ((c > 0) && ((uint8_t)d == ':')) {
              if (cn == linkid) {
                return c;
              } else if ((d = check_string(&str_startrequest)) == CHECK_OK) {
                setblink(1, COLOR_BLUE, 0);
                if (linkid < 5) {
                  uart_send_pgm(&str_cipclose_a);
                  uart_send(linkid + 48);
                  uart_crlf();
                  wait_reply(5);
                }
                linkid = cn;
                ip_send_pgm(cn, PSTR("READY"));
                read_timeout = 255;
                d = uart_read();
              }
            }
          }
        }
      }        
    }
    d = skip_to_next_line(d);
  }
}

static void spm_busy_wait() {
  while (boot_spm_busy()) pull();
}

uint8_t reply[264];

static uint8_t ip_send_reply(uint8_t size) {
  if (start_send(linkid, size)) {
    uint8_t * buf = &reply[0];
    while (size--) {
      uart_send(*(buf++));
    }
    return finish_send();  
  } else {
    return 0;
  }
}


static void bler() {
  reply[2] = 'e';
  reply[3] = 'r';
  ip_send_reply(4);
}

static void micropause() {
  asm volatile (
    "ldi r24, 0\n" //~48us 
    "dec r24\n"
    "brne .-4\n"
    :::"r24"
    );
}

#define jump_to_app() ((void (*)(void))(0))()

int main(void) {
  asm volatile ("cli");
  uint8_t mcusr = MCUSR;
  MCUSR = 0;
  
  LED_DATA_PORT &= ~LED_DATA;
  LED_DATA_DDR |= LED_DATA;

  FORCE_DDR &= ~FORCE_A; 
  FORCE_PORT |= FORCE_A; // На одном пине включем подтяжку 
  FORCE_PORT &= ~FORCE_B;
  FORCE_DDR |= FORCE_B; // На другом жётский низкий уровень
  micropause();
  if (!(FORCE_PIN & FORCE_A)) { // Если на первом пине низкий уровень, то проверяем дальше
    FORCE_DDR &= ~FORCE_B; // Включим подтяжку на втором
    FORCE_PORT |= FORCE_B; 
    micropause();
    if ((FORCE_PIN & FORCE_A) && (FORCE_PIN & FORCE_B)) { // Если на обоих высокий уровень, едем дальше
      FORCE_PORT &= ~FORCE_A; // Жёсткий низкий уровень теперь на первом
      FORCE_DDR |= FORCE_A;
      micropause();
      if (!(FORCE_PIN & FORCE_B)) { // Если и на втором пине теперь низкий уровень, то значит они соединены
        force_defaults = 1;
      }      
    }
  }
  FORCE_DDR = LED_DATA; // Зная что это один и тот же порт
  FORCE_PORT = 0;
  
  CTR_DDR |= CTR;
  CTR_PORT &= ~CTR;
  
  if (!force_defaults && mcusr && (eeprom_read(EE_MAGIC) == MAGIC_PROGRAMMED)) { // Если у нас программа встала нормально, это не вызов из программы и не принудительный запуск
    jump_to_app(); // То запускаем наше ПО
  }
  
  uint8_t i = 255;
  while (i--) micropause(); // Пауза около 12мс
  clear_leds();
  
  UCSR0A = (1 << U2X0);
  UBRR0 = UART_UBRR_DEFAULT;
  UCSR0B = (1 << RXEN0) | (1 << TXEN0);
  UCSR0C = (1 << UCSZ00) | (1 << UCSZ01);
  
  TCCR1A = 0;
  OCR1A = 6249; // прервыание 10 раз в секунду
  TIMSK1 = 0;
  TCCR1B = (1 << WGM12) | (1 << CS12); // прескалер 1 к 256 (62500 тактов в секунду)
  TCNT1 = 0;

  setblink(2, COLOR_GREEN, 0);
  wifi_init(mcusr);
  uint16_t next_page = 0;
  reply[0] = 'b';
  reply[1] = 'l';
  for(;;) {
     uint16_t l16 = wait_packet();
     read_timeout = 5;
     if ((l16 < 4) || (l16 >= 256)) {
       uart_skip(l16);
       bler();
       continue;
     }       
     uint8_t l = l16;
     if (((uint8_t)uart_read() != 'B') || ((uint8_t)uart_read() != 'L')) {
       uart_skip(l - 2);
       bler();
       continue;
     } 
     uint8_t b1 = uart_read();
     uint8_t b2 = uart_read();
     if ((b1 == 'P') && (b2 == 'G')) {
       uint16_t pn = uart_read() | (uart_read() << 8);
       if ((pn != 0) && ((pn != next_page) || (pn >= (BOOTLOADER_OFFSET / SPM_PAGESIZE)))) {
         uart_skip(l - 6);
         bler();
       } else {
         if (!pn) {
           if (next_page) { // Если вдруг начался новый цикл программированния, то сбросим всё что было до этого
             boot_rww_enable();
           }
           setblink(1, COLOR_BLUE, COLOR_RED);
           eeprom_write(EE_MAGIC, 0xFF); // Сброс маркера об успешном программировании
         }
         uint16_t offset = pn * SPM_PAGESIZE;
         for (uint8_t i = 0; i < SPM_PAGESIZE; i += 2) {
           boot_page_fill(offset + i, uart_read() | (uart_read() << 8)); // Загрузка страницы
         }
         spm_busy_wait(); // Ожидание готовности
         boot_page_erase(offset); // Стирание страницы
         spm_busy_wait();
         boot_page_write(offset); // Запись загруженной страницы
         spm_busy_wait(); 
         next_page = pn + 1;
         reply[2] = 'p';
         reply[3] = 'g';
         reply[4] = pn;
         reply[5] = pn >> 8;
         ip_send_reply(6);
      }
    } else if ((l == 4) && (b1 == 'C') && (b2 == 'M')) {
      if (next_page) {
        boot_rww_enable();
        next_page = 0;
      }
      reply[2] = 'c';
      reply[3] = 'm';
      ip_send_reply(4);
      setblink(1, COLOR_GREEN, 0);
    } else if ((l == 4) && (b1 == 'R') && (b2 == 'S')) {
      reply[2] = 'r';
      reply[3] = 's';
      ip_send_reply(4);
      TCCR1A = 0;
      TCCR1B = 0;
      jump_to_app();
    } else if ((l == 7) && (b1 == 'R') && (b2 == 'E')) {
      uint16_t off = uart_read() | (uart_read() << 8);
      uint8_t el = uart_read();
      if ((off < 2) || ((off + l) > E2END)) {
        bler();
      } else {
        reply[2] = 'r';
        reply[3] = 'e';
        reply[4] = off;
        reply[5] = off >> 8;
        reply[6] = el;
        uint8_t * r = &reply[7];
        while (el--) {
          *(r++) = eeprom_read(off++);
        }
        ip_send_reply(7 + l);
      }
    } else if ((l >= 7) && (b1 == 'W') && (b2 == 'E')) {
      uint16_t off = uart_read() | (uart_read() << 8);
      uint8_t el = uart_read();
      if (((uint8_t)(l - 7) != el) || ((off < 2) || ((off + l) > E2END))) {
        uart_skip(l - 7);
        bler();
      } else {
        reply[2] = 'w';
        reply[3] = 'e';
        reply[4] = off;
        reply[5] = off >> 8;
        reply[6] = el;
        while (el--) {
          eeprom_write(off++, uart_read());
        }
        ip_send_reply(7);
      }        
    } else {
      bler();
      uart_skip(l - 4);
    }
  }
}