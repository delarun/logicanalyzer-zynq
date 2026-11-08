#pragma once

#define LA_TCP_PORT 5555

void la_server_init(void);
void la_server_poll(void);     // вызывать из главного цикла
void la_set_led(unsigned bit, int on);
