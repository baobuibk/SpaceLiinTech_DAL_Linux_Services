#ifndef DAL_PORT_CONFIG_H
#define DAL_PORT_CONFIG_H

#include <stddef.h>

/* Console-only configuration of the service environment file. */
int port_config_get(char *out, size_t capacity);
int port_config_set(const char *key, const char *value, char *error, size_t capacity);

#endif
