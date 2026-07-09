#include "uart.h"
#include "esp_log.h"
#include "string.h"
#include "driver/uart.h"
#include "../utils.h"
#include "../ui/ui.h"

static const char *TAG = "UART_APP";

// 串口接收开关：默认开启，所有页面均可接收数据
static volatile bool g_uart_rx_enabled = true;

void uart_rx_enable(void)
{
    g_uart_rx_enabled = true;
    ESP_LOGI(TAG, "UART RX enabled");
}

void uart_rx_disable(void)
{
    g_uart_rx_enabled = false;
    ESP_LOGI(TAG, "UART RX disabled");
}

// 栈守卫阈值：当前已用栈超过此值，暂存命令延迟处理
#define STACK_GUARD_THRESHOLD  1536

// 全局暂存区：栈紧张时缓存命令，等栈恢复再处理（时间换空间）
static char g_deferred_cmd[UART_BUF_SIZE];
static bool g_has_deferred_cmd = false;

// 获取当前栈指针（Xtensa内联汇编）
static inline uint32_t uart_get_sp(void)
{
    uint32_t sp;
    __asm__ __volatile__("mov %0, sp" : "=r"(sp));
    return sp;
}

void uart_custom_init(void)
{
    const uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 安装驱动
    ESP_ERROR_CHECK(uart_driver_install(UART_NUM_1, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(UART_NUM_1, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(UART_NUM_1, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    ESP_LOGI(TAG, "UART1 initialized (TX:17, RX:18)");
}

void uart_custom_send(const char *data)
{
    uart_write_bytes(UART_NUM_1, data, strlen(data));
}

void uart_rx_task(void *pv)
{
    uint8_t *data = (uint8_t *)malloc(UART_BUF_SIZE);
    uint32_t sp_baseline;

    while (1) {
        if (!g_uart_rx_enabled) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }

        // 记录基线SP：此时栈最深的就是task本身，是最安全的处理时机
        sp_baseline = uart_get_sp();

        // 优先处理上一轮暂存的命令（此时栈处于最浅状态，安全）
        if (g_has_deferred_cmd) {
            ESP_LOGI(TAG, "Proc deferred: %s", g_deferred_cmd);
            Command_Analysis(g_deferred_cmd);
            g_has_deferred_cmd = false;
        }

        int len = uart_read_bytes(UART_NUM_1, data, UART_BUF_SIZE - 1, pdMS_TO_TICKS(50));

        if (len > 0) {
            data[len] = '\0';

            // 只有 '[' 开头的命令才处理
            if (data[0] == '[') {
                ESP_LOGI(TAG, "Cmd: %s", (char*)data);

                // 栈守卫：检测当前栈深度是否超过安全阈值
                uint32_t sp_now = uart_get_sp();
                uint32_t stack_used = sp_baseline - sp_now;

                if (stack_used > STACK_GUARD_THRESHOLD) {
                    // 栈太深！暂存到全局缓冲区，下轮再处理（时间换空间）
                    strncpy(g_deferred_cmd, (char*)data, UART_BUF_SIZE - 1);
                    g_deferred_cmd[UART_BUF_SIZE - 1] = '\0';
                    g_has_deferred_cmd = true;
                    ESP_LOGW(TAG, "Stack used %lu > %u, deferred", stack_used, STACK_GUARD_THRESHOLD);
                } else {
                    // 栈空间充足，直接处理
                    Command_Analysis((char*)data);
                }
            }
        }
    }
    free(data);
}