#ifndef LINK_UART_TAKE_H
#define LINK_UART_TAKE_H

#include <stdint.h>

typedef enum { node_first, node_second } node_slot;

void app_node_link_init( uint16_t node_id);
void app_node_link_run(void);
void app_node_sample_update(node_slot slot, const char *line);

#endif
