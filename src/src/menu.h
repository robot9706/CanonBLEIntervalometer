#ifndef __MENU__
#define __MENU__

#include <stdint.h>
#include <string.h>

#define MENU_MAIN 0

#define MENU_PAIR 1
#define MENU_PAIR_CONNECT 2

#define MENU_CONNECT 3
#define MENU_CONNECT_DO 4

#define MENU_CAMERA_MAIN 5
#define MENU_CAMERA_TIMER 6

void menu_init();
void menu_set(uint8_t index);
void menu_input(uint8_t button);

#endif
