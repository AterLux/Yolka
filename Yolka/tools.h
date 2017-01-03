/*
 * tools.h
 * Полезняшки. 
 *
 * Author: Погребняк Дмитрий
 * 
 * Помещённый в этом файле код является свободным. Т.е. допускается его свободное использование для любых целей, включая коммерческие, при условии указания ссылки на автора (Погребняк Дмитрий, http://aterlux.ru/).
 * The code in this file is free to use. It can be freely used for any purpose, including commercial, as long as link to author (Pogrebnyak Dmitry, http://aterlux.ru/) is provided.
 */ 


#ifndef TOOLS_H_
#define TOOLS_H_

#define _GET_DDR(_X) DDR ## _X
#define _GET_PIN(_X) PIN ## _X
#define _GET_PORT(_X) PORT ## _X

#define DDR(_X) _GET_DDR(_X)
#define PIN(_X) _GET_PIN(_X)
#define PORT(_X) _GET_PORT(_X)

#define arraysize(array) (sizeof(array) / sizeof(array[0]))

#endif /* TOOLS_H_ */