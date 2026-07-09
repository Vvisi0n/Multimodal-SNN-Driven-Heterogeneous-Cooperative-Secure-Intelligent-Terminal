#include "DHT11.h"
#include <DHT.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

float DHT11_Temp = 0.0f;
float DHT11_Humi = 0.0f;

static DHT dht(DHT11_PIN, DHT11);

void DHT11_Init(void)
{
    dht.begin();
}

static void DHT11_Task(void *parameter)
{
    (void)parameter;

    for (int attempt = 0; attempt < 3; attempt++) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();

        if (!isnan(t) && !isnan(h)) {
            DHT11_Temp = t;
            DHT11_Humi = h;
            Serial.println("DHT11: Sensor OK, task running.");
            break;
        }

        if (attempt == 2) {
            Serial.println("DHT11: Sensor NOT found! Task exiting.");
            vTaskDelete(NULL);
            return;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    while (1) {
        float t = dht.readTemperature();
        float h = dht.readHumidity();

        if (!isnan(t) && !isnan(h)) {
            DHT11_Temp = t;
            DHT11_Humi = h;
        }

        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

void DHT11_CreateTask(void)
{
    xTaskCreatePinnedToCore(
        DHT11_Task,
        "DHT11_Task",
        2048,
        NULL,
        1,
        NULL,
        0
    );
}