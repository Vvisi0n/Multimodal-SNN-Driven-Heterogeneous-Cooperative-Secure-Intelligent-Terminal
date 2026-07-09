#include "llm_cloud.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cjson.h"
#include "../utils.h"

static const char *TAG = "LLM_CLOUD";

#define API_KEY      "sk-hvoeshiybspcqxlzmvzzoaywliaynruloesyewtowmpnsgub"
#define API_URL      "https://api.siliconflow.cn/v1/chat/completions"
#define MODEL_NAME   "Qwen/Qwen2.5-7B-Instruct"
#define SYSTEM_PROMPT "Helpful, English only, ≤25 words, ultra-concise."

// 扩大接收缓冲区到 8KB，完全容纳 SSE 流式大包，彻底消除溢出
#define HTTP_BUF_SIZE 8192

QueueHandle_t llm_reply_queue = NULL;
static char *s_user_prompt = NULL;

// 将 line_buf 移入 llm_task 内部或作为任务私有，
// 避免多线程重入风险。并保证全局清理安全。
static char *line_buf = NULL;
static int line_len = 0;

static esp_err_t local_http_event_handler(esp_http_client_event_t *evt) {
    switch (evt->event_id) {
        case HTTP_EVENT_ON_CONNECTED: 
            ESP_LOGI(TAG, "HTTP connected to server");
            line_len = 0;
            break;

        case HTTP_EVENT_ON_DATA:
            if (evt->data_len > 0 && line_buf != NULL) {
                // 严格的长效溢出安全检查
                if (line_len + evt->data_len >= HTTP_BUF_SIZE - 1) {
                    ESP_LOGE(TAG, "Buffer overflow! Emergency reset, discarding partial packet to prevent memory corruption!");
                    line_len = 0;
                }

                memcpy(line_buf + line_len, evt->data, evt->data_len);
                line_len += evt->data_len;
                line_buf[line_len] = '\0';

                char *line_start = line_buf;
                char *next_line = NULL;

                while ((next_line = strchr(line_start, '\n')) != NULL) {
                    *next_line = '\0';
                    
                    char *r_tail = strchr(line_start, '\r');
                    if (r_tail) *r_tail = '\0';

                    if (strncmp(line_start, "data: ", 6) == 0) {
                        char *json_data = line_start + 6;
                        
                        if (strncmp(json_data, "[DONE]", 6) == 0) {
                            ESP_LOGI(TAG, "[AI streaming end marker]");
                            char *end_token = strdup("[DONE]");
                            if (llm_reply_queue && end_token) {
                                if (xQueueSend(llm_reply_queue, &end_token, 0) != pdPASS) {
                                    free(end_token);
                                }
                            }
                        } else {
                            // 极为严格的 cJSON 解析安全防御
                            cJSON *root = cJSON_Parse(json_data);
                            if (root) {
                                cJSON *choices = cJSON_GetObjectItem(root, "choices");
                                if (choices && cJSON_GetArraySize(choices) > 0) {
                                    cJSON *choice = cJSON_GetArrayItem(choices, 0);
                                    cJSON *delta = cJSON_GetObjectItem(choice, "delta");
                                    if (delta) {
                                        cJSON *content = cJSON_GetObjectItem(delta, "content");
                                        if (content && content->valuestring && strlen(content->valuestring) > 0) {
                                            
                                            // 使用安全且配对的堆分配传递给 UI 层
                                            char *chunk_msg = strdup(content->valuestring);
                                            if (chunk_msg) {
                                                if (llm_reply_queue && xQueueSend(llm_reply_queue, &chunk_msg, 0) != pdPASS) {
                                                    free(chunk_msg); // 队列若满，立刻释放防止泄漏
                                                }
                                            }
                                        }
                                    }
                                }
                                cJSON_Delete(root); // 必须在解析完分支内释放
                            }
                        }
                    }
                    line_start = next_line + 1;
                }

                int consumed = line_start - line_buf;
                if (consumed > 0) {
                    memmove(line_buf, line_start, line_len - consumed);
                    line_len -= consumed;
                    line_buf[line_len] = '\0';
                }
            }
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP disconnected");
            line_len = 0;
            break;
            
        // 显式注册所有常规网络事件，消除 "no handlers have been registered" 的隐性消耗
        case HTTP_EVENT_ERROR:
        case HTTP_EVENT_HEADER_SENT:
        case HTTP_EVENT_ON_HEADER:
        case HTTP_EVENT_ON_FINISH:
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void llm_task(void *pvParameters) {
    if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    if (s_user_prompt == NULL) {
        if (g_data_mutex) xSemaphoreGive(g_data_mutex);
        vTaskDelete(NULL);
        return;
    }
    // 复制到本地变量后立即释放锁，避免长时间持锁阻塞 Core 1
    char *local_prompt = strdup(s_user_prompt);
    if (g_data_mutex) xSemaphoreGive(g_data_mutex);
    if (local_prompt == NULL) {
        vTaskDelete(NULL);
        return;
    }

    // 动态分配任务级私有大缓冲区（从堆上申请，避免栈溢出风险）
    line_buf = malloc(HTTP_BUF_SIZE);
    if (line_buf == NULL) {
        ESP_LOGE(TAG, "Failed to allocate heap memory for line_buf!");
        if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        free(s_user_prompt);
        s_user_prompt = NULL;
        if (g_data_mutex) xSemaphoreGive(g_data_mutex);
        free(local_prompt);
        vTaskDelete(NULL);
        return;
    }
    
    line_len = 0;
    memset(line_buf, 0, HTTP_BUF_SIZE);

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", MODEL_NAME);
    cJSON_AddBoolToObject(root, "stream", true);
    
    cJSON *messages = cJSON_CreateArray();
    cJSON *sys_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(sys_msg, "role", "system");
    cJSON_AddStringToObject(sys_msg, "content", SYSTEM_PROMPT);
    cJSON_AddItemToArray(messages, sys_msg);

    cJSON *user_msg = cJSON_CreateObject();
    cJSON_AddStringToObject(user_msg, "role", "user");
    cJSON_AddStringToObject(user_msg, "content", local_prompt);
    cJSON_AddItemToArray(messages, user_msg);
    cJSON_AddItemToObject(root, "messages", messages);

    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    char auth_header[128];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", API_KEY);

    esp_http_client_config_t config = {
        .url = API_URL,
        .method = HTTP_METHOD_POST,
        .event_handler = local_http_event_handler, 
        .timeout_ms = 60000,
        .buffer_size = 4096, // 提升底层网络接收单元
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    ESP_LOGI(TAG, "[Core 0 async request]: %s", local_prompt);

    esp_err_t err = esp_http_client_perform(client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        char *err_msg = strdup("System Error: Network Failed.");
        if (llm_reply_queue && err_msg) {
            if (xQueueSend(llm_reply_queue, &err_msg, 0) != pdPASS) free(err_msg);
        }
        // 补发 [DONE]，确保 UI 层能正常结束这次会话并保存到全局链表
        char *end_token = strdup("[DONE]");
        if (llm_reply_queue && end_token) {
            if (xQueueSend(llm_reply_queue, &end_token, 0) != pdPASS) free(end_token);
        }
    }

    // 严格的退出机制，保证每一次网络通信完全、无遗漏地回收句柄与堆内存
    esp_http_client_cleanup(client);
    free(post_data);
    if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    free(s_user_prompt);
    s_user_prompt = NULL;
    if (g_data_mutex) xSemaphoreGive(g_data_mutex);
    free(local_prompt);
    
    if (line_buf) {
        free(line_buf);   // 销毁本次会话的大缓冲区
        line_buf = NULL;
    }
    
    vTaskDelete(NULL);
}

esp_err_t llm_cloud_init(void) {
    if (llm_reply_queue == NULL) {
        // 增加队列容量到 50。高频 Token（如大模型爆发式吐字）不会导致堵塞死锁
        llm_reply_queue = xQueueCreate(50, sizeof(char *)); 
    }
    return ESP_OK;
}

esp_err_t llm_cloud_request_stream(const char *user_prompt) {
    if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
    if (s_user_prompt != NULL) {
        if (g_data_mutex) xSemaphoreGive(g_data_mutex);
        return ESP_ERR_INVALID_STATE;
    }
    s_user_prompt = strdup(user_prompt);
    if (g_data_mutex) xSemaphoreGive(g_data_mutex);
    if (s_user_prompt == NULL) {
        return ESP_ERR_NO_MEM;
    }
    
    BaseType_t ret = xTaskCreatePinnedToCore(llm_task, "llm_task", 1024 * 8, NULL, 5, NULL, 0);
    if (ret != pdPASS) {
        if (g_data_mutex) xSemaphoreTake(g_data_mutex, portMAX_DELAY);
        free(s_user_prompt);
        s_user_prompt = NULL;
        if (g_data_mutex) xSemaphoreGive(g_data_mutex);
        return ESP_FAIL;
    }
    return ESP_OK;
}