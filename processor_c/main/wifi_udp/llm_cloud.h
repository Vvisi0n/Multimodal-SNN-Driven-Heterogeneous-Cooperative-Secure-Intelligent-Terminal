#ifndef LLM_CLOUD_H
#define LLM_CLOUD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// 声明一个全局队列句柄，用于把 Core 0 的文本传给 Core 1
extern QueueHandle_t llm_reply_queue;

esp_err_t llm_cloud_init(void);
esp_err_t llm_cloud_request_stream(const char *user_prompt);

#ifdef __cplusplus
}
#endif

#endif // LLM_CLOUD_H