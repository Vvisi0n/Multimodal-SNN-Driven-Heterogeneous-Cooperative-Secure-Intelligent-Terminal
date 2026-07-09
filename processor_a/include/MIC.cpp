#include "MIC.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

uint16_t MIC_Value = 0;
uint32_t MIC_Amp  = 0;
uint32_t MIC_ADC  = 0;

#define SAMPLE_NUM 40

void MIC_Init(void)
{
    adcAttachPin(MIC_PIN);
    analogReadResolution(12);
    analogSetPinAttenuation(MIC_PIN, ADC_11db);
}

static void MIC_Task(void *parameter)
{
    (void)parameter;

    while (1)
    {
        uint32_t sum = 0;
        uint16_t samples[SAMPLE_NUM];

        for (int i = 0; i < SAMPLE_NUM; i++)
        {
            samples[i] = analogRead(MIC_PIN);
            sum += samples[i];
        }

        uint16_t mid = sum / SAMPLE_NUM;

        uint32_t abs_sum = 0;

        for (int i = 0; i < SAMPLE_NUM; i++)
        {
            int ac = (int)samples[i] - mid;
            if (ac < 0) ac = -ac;
            abs_sum += ac;
        }

        uint16_t amp = abs_sum / SAMPLE_NUM;

        MIC_ADC   = mid;
        MIC_Amp   = amp;
        MIC_Value = mid;

        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

void MIC_CreateTask(void)
{
    xTaskCreatePinnedToCore(
        MIC_Task,
        "MIC_Task",
        1536,
        NULL,
        1,
        NULL,
        0
    );
}