#ifndef __WIFI_H__
#define __WIFI_H__

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_SSID        "JIGE666"
#define WIFI_PASS        "1234567890"

extern volatile uint8_t is_wifi;

void wifi_manager_init(void);

#ifdef __cplusplus
}
#endif

#endif // __WIFI_H__