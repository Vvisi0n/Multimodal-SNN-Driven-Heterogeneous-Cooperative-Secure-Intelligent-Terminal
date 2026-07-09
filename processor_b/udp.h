#ifndef UDP_H
#define UDP_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

void startUdpStream(void);
void set_camera_mutex(SemaphoreHandle_t mutex);

bool get_shared_jpeg(uint8_t **out_buf, size_t *out_len);
void release_shared_jpeg(void);

#ifdef __cplusplus
}
#endif

#endif