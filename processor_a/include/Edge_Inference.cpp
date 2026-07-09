#include "Edge_Inference.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ---- 模型数据 ---- */
#include "model_param.h"

/* ---- TensorFlow Lite Micro ---- */
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* ---- FreeRTOS ---- */
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

/*==================================================================
 *  静态全局变量（文件作用域，外部不可见）
 *==================================================================*/

/* TFLM 相关 */
static uint8_t                          s_tensor_arena[EDGE_TENSOR_ARENA_SIZE];
static tflite::ErrorReporter*           s_error_reporter = NULL;
static const tflite::Model*             s_model = NULL;
static tflite::MicroInterpreter*        s_interpreter = NULL;
static TfLiteTensor*                    s_input_tensor = NULL;
static TfLiteTensor*                    s_output_tensor = NULL;

/* 窗口缓冲 */
static float  s_window_buffer[EDGE_WINDOW_SIZE];
static int    s_window_count = 0;

/* FreeRTOS 资源 */
static QueueHandle_t  s_window_queue = NULL;
static QueueHandle_t  s_result_queue = NULL;
static TaskHandle_t   s_model_task_handle = NULL;

/* 模拟相关 */
static int            s_sim_index = 0;
static unsigned long  s_last_sim_ms = 0;

/* 全局结果（外部可见） */
EdgeInference_Result g_edge_result;

/* 类别名称 */
const char* g_edge_class_names[EDGE_NUM_CLASSES] = {
    "normal",
    "loaded_normal",
    "weight_step",
    "forward_collision",
    "backward_collision",
    "stuck",
    "overload"
};

/*==================================================================
 *  模拟数据（与测试文件完全一致）
 *==================================================================*/
typedef struct {
    int    expected_label;
    float  values[EDGE_WINDOW_SIZE];
} SimSample;

static const SimSample s_sim_samples[] = {
    /* class 0: normal */
    { 0, {  127.0f,  115.0f,  133.0f,  167.0f, -103.0f, -126.0f, -168.0f, -153.0f, -183.0f,  146.0f } },
    { 0, {   96.0f,  111.0f,  141.0f,  157.0f,  -94.0f, -125.0f, -159.0f, -159.0f, -213.0f,  137.0f } },
    { 0, { -163.0f, -142.0f, -128.0f, -154.0f, -215.0f,  151.0f,  138.0f,  125.0f,  116.0f,  116.0f } },
    /* class 1: loaded_normal */
    { 1, { -220.0f,  160.0f,  118.0f,  122.0f,  167.0f,  116.0f,  157.0f, -125.0f, -128.0f, -191.0f } },
    { 1, { -171.0f, -127.0f,  149.0f,  113.0f,  129.0f,  138.0f, -129.0f, -128.0f, -133.0f, -153.0f } },
    { 1, { -158.0f,  103.0f,  112.0f,  131.0f,  150.0f,  132.0f, -173.0f, -115.0f, -154.0f, -159.0f } },
    /* class 2: weight_step */
    { 2, {  202.0f,  112.0f,  132.0f,  102.0f,   95.0f, -115.0f, -141.0f, -135.0f, -153.0f, -203.0f } },
    { 2, {  147.0f,  112.0f,  106.0f,  131.0f, -105.0f, -109.0f, -135.0f, -148.0f,   98.0f,  104.0f } },
    { 2, { -121.0f,  -99.0f, -159.0f, -122.0f, -292.0f,  111.0f,  127.0f,  105.0f,  149.0f,  130.0f } },
    /* class 3: forward_collision */
    { 3, { 1501.0f, 1654.0f, 1484.0f, 1643.0f, 1543.0f, 1577.0f, 1596.0f, 1473.0f, 1593.0f, 1594.0f } },
    { 3, { 1668.0f, 1443.0f, 1756.0f, 1611.0f, 1423.0f, 1689.0f, 1642.0f, 1612.0f, 1727.0f, 1708.0f } },
    { 3, { 1551.0f, 1743.0f, 1636.0f, 1500.0f, 1986.0f, 1670.0f, 1438.0f, 1706.0f, 1839.0f, 1550.0f } },
    /* class 4: backward_collision */
    { 4, { -176.0f,  -174.0f,  -178.0f, -1150.0f, -1721.0f, -2035.0f, -2601.0f, -2552.0f, -1632.0f, -1355.0f } },
    { 4, { -1790.0f,-2376.0f, -2597.0f, -1702.0f, -1057.0f,  -891.0f, -2331.0f, -1916.0f, -2389.0f, -2032.0f } },
    { 4, { -2214.0f,-1944.0f, -1323.0f,   260.0f, -2309.0f, -1629.0f, -1090.0f, -2262.0f, -1948.0f,  -770.0f } },
    /* class 5: stuck */
    { 5, {  153.0f,  657.0f,   108.0f,  140.0f,   725.0f,  1979.0f, -1544.0f, -248.0f,  -346.0f,  -269.0f } },
    { 5, { -349.0f, -209.0f,  1422.0f,  118.0f,   283.0f,   672.0f,  1113.0f, 2910.0f,  -781.0f,  -510.0f } },
    { 5, {  710.0f, 1319.0f,  -728.0f,-1457.0f,  -277.0f,  -175.0f,  -623.0f, 1053.0f,  1284.0f,   861.0f } },
    /* class 6: overload */
    { 6, {  146.0f,  165.0f,  -123.0f, -129.0f,  -157.0f,  -159.0f,   120.0f,  126.0f,   112.0f,   139.0f } },
    { 6, { -153.0f, -147.0f,   125.0f,  134.0f,   137.0f,   170.0f,  -115.0f, -105.0f,  -156.0f,  -137.0f } },
    { 6, {  124.0f, -146.0f,  -156.0f, -150.0f,    98.0f,   120.0f,   104.0f,  378.0f,  -131.0f,  -158.0f } },
};

#define SIM_SAMPLE_COUNT  (sizeof(s_sim_samples) / sizeof(s_sim_samples[0]))

/*==================================================================
 *  窗口消息结构体（内部队列传输用）
 *==================================================================*/
typedef struct {
    int    source;        /* 0=模拟, 1=外部 */
    int    sample_index;
    int    expected_label;
    float  values[EDGE_WINDOW_SIZE];
} WindowMsg;

/*==================================================================
 *  内部工具函数
 *==================================================================*/

/* 获取张量元素总数 */
static int tensor_element_count(const TfLiteTensor* tensor)
{
    if (tensor == NULL || tensor->dims == NULL) {
        return 0;
    }
    int count = 1;
    for (int i = 0; i < tensor->dims->size; ++i) {
        count *= tensor->dims->data[i];
    }
    return count;
}

/* 特征提取：从 EDGE_WINDOW_SIZE 个采样点提取 EDGE_FEATURE_DIM 维特征 */
static void extract_features(const float* x, int n, float* features)
{
    float sum     = 0.0f;
    float sum_sq  = 0.0f;
    float sum_abs = 0.0f;
    float min_v   = x[0];
    float max_v   = x[0];
    float peak_abs = fabsf(x[0]);
    int   pos_cnt  = 0;
    int   neg_cnt  = 0;

    for (int i = 0; i < n; ++i) {
        const float v     = x[i];
        const float abs_v = fabsf(v);
        sum     += v;
        sum_sq  += v * v;
        sum_abs += abs_v;
        if (v < min_v)  min_v = v;
        if (v > max_v)  max_v = v;
        if (abs_v > peak_abs) peak_abs = abs_v;
        if (v > 0.0f)       ++pos_cnt;
        else if (v < 0.0f)  ++neg_cnt;
    }

    const float inv_n = 1.0f / (float)n;
    const float mean  = sum * inv_n;

    float var_sum  = 0.0f;
    float slope_num = 0.0f;
    float slope_den = 0.0f;
    const float mean_t = 0.5f * (float)(n - 1);

    for (int i = 0; i < n; ++i) {
        const float centered = x[i] - mean;
        const float t = (float)i - mean_t;
        var_sum   += centered * centered;
        slope_num += t * centered;
        slope_den += t * t;
    }

    const float std_v   = sqrtf(var_sum * inv_n);
    const float rms     = sqrtf(sum_sq * inv_n);
    const float slope_v = (slope_den > 0.0f) ? (slope_num / slope_den) : 0.0f;

    const int diff_cnt = n - 1;
    float diff_sum     = 0.0f;
    float diff_abs_sum = 0.0f;
    float diff_max_abs = 0.0f;

    for (int i = 1; i < n; ++i) {
        const float diff     = x[i] - x[i - 1];
        const float abs_diff = fabsf(diff);
        diff_sum     += diff;
        diff_abs_sum += abs_diff;
        if (abs_diff > diff_max_abs) diff_max_abs = abs_diff;
    }

    float diff_mean_abs  = 0.0f;
    float large_jump_ratio = 0.0f;

    if (diff_cnt > 0) {
        diff_mean_abs = diff_abs_sum / (float)diff_cnt;
        const float diff_mean = diff_sum / (float)diff_cnt;
        float diff_var_sum = 0.0f;

        for (int i = 1; i < n; ++i) {
            const float cd = (x[i] - x[i - 1]) - diff_mean;
            diff_var_sum += cd * cd;
        }

        const float diff_std = sqrtf(diff_var_sum / (float)diff_cnt);
        const float threshold = diff_mean_abs + diff_std;
        int large_cnt = 0;

        for (int i = 1; i < n; ++i) {
            if (fabsf(x[i] - x[i - 1]) > threshold) {
                ++large_cnt;
            }
        }
        large_jump_ratio = (float)large_cnt / (float)diff_cnt;
    }

    features[0]  = mean;
    features[1]  = std_v;
    features[2]  = min_v;
    features[3]  = max_v;
    features[4]  = max_v - min_v;
    features[5]  = rms;
    features[6]  = sum_abs * inv_n;
    features[7]  = peak_abs;
    features[8]  = (float)pos_cnt * inv_n;
    features[9]  = (float)neg_cnt * inv_n;
    features[10] = slope_v;
    features[11] = x[n - 1] - x[0];
    features[12] = diff_mean_abs;
    features[13] = diff_max_abs;
    features[14] = large_jump_ratio;
}

/* 获取输出张量第 index 个元素的浮点值 */
static float output_value(int index)
{
    if (s_output_tensor->type == kTfLiteInt8) {
        return ((float)s_output_tensor->data.int8[index] -
                (float)s_output_tensor->params.zero_point) *
               s_output_tensor->params.scale;
    }
    if (s_output_tensor->type == kTfLiteFloat32) {
        return s_output_tensor->data.f[index];
    }
    return -1.0e9f;
}

/* argmax */
static int argmax_output(int output_len)
{
    int   best_idx = 0;
    float best_val = output_value(0);
    for (int i = 1; i < output_len; ++i) {
        const float val = output_value(i);
        if (val > best_val) {
            best_val = val;
            best_idx = i;
        }
    }
    return best_idx;
}

/* 将特征填充到模型输入张量 */
static int fill_model_input(const float* features, char* error, size_t error_len)
{
    const int input_len = tensor_element_count(s_input_tensor);

    if (input_len < EDGE_FEATURE_DIM) {
        snprintf(error, error_len, "Model input too small: %d < %d",
                 input_len, EDGE_FEATURE_DIM);
        return 0;
    }

    if (s_input_tensor->type == kTfLiteInt8) {
        const float in_scale = s_input_tensor->params.scale;
        const int   in_zero  = s_input_tensor->params.zero_point;

        for (int i = 0; i < input_len; ++i) {
            const float raw  = (i < EDGE_FEATURE_DIM) ? features[i] : 0.0f;
            const float norm = raw / EDGE_INPUT_SCALE;
            int32_t q = (int32_t)roundf(norm / in_scale + (float)in_zero);

            if (q > 127)       q = 127;
            else if (q < -128) q = -128;

            s_input_tensor->data.int8[i] = (int8_t)q;
        }
        return 1;
    }

    if (s_input_tensor->type == kTfLiteFloat32) {
        for (int i = 0; i < input_len; ++i) {
            const float raw = (i < EDGE_FEATURE_DIM) ? features[i] : 0.0f;
            s_input_tensor->data.f[i] = raw / EDGE_INPUT_SCALE;
        }
        return 1;
    }

    snprintf(error, error_len, "Unsupported input type: %d", s_input_tensor->type);
    return 0;
}

/* 执行一次推理，结果写入 g_edge_result */
static void run_inference(const WindowMsg* msg)
{
    /* 清空全局结果 */
    memset((void*)&g_edge_result, 0, sizeof(g_edge_result));
    g_edge_result.predicted_label = -1;
    g_edge_result.source          = msg->source;
    g_edge_result.sample_index    = msg->sample_index;
    g_edge_result.expected_label  = msg->expected_label;
    memcpy(g_edge_result.window_data, msg->values, sizeof(msg->values));

    /* 特征提取 */
    extract_features(msg->values, EDGE_WINDOW_SIZE, g_edge_result.features);

    #if EDGE_FEATURE_DEBUG
        /* 打印特征值用于调试量化范围（仅在开启 EDGE_FEATURE_DEBUG 宏时才打印） */
        Serial.printf("[EdgeInf] Features (raw): ");
        for (int i = 0; i < EDGE_FEATURE_DIM; i++) {
            Serial.printf("%.1f ", g_edge_result.features[i]);
        }
        Serial.println();
        Serial.printf("[EdgeInf] Features (qnt): ");
        for (int i = 0; i < EDGE_FEATURE_DIM; i++) {
            const float raw = g_edge_result.features[i];
            const float norm = raw / EDGE_INPUT_SCALE;
            int32_t q = (int32_t)roundf(norm / s_input_tensor->params.scale + (float)s_input_tensor->params.zero_point);
            if (q > 127) q = 127;
            else if (q < -128) q = -128;
            Serial.printf("%d ", (int)q);
        }
        Serial.println();
    #endif

    /* 填充模型输入 */
    if (!fill_model_input(g_edge_result.features,
                          g_edge_result.error_msg,
                          sizeof(g_edge_result.error_msg))) {
        g_edge_result.ok = 0;
        g_edge_result.fresh = 1;
        return;
    }

    /* 执行推理 */
    const unsigned long t0 = millis();
    const TfLiteStatus  status = s_interpreter->Invoke();
    g_edge_result.inference_time_ms = millis() - t0;

    if (status != kTfLiteOk) {
        snprintf(g_edge_result.error_msg, sizeof(g_edge_result.error_msg),
                 "Invoke failed (status=%d)", (int)status);
        g_edge_result.ok = 0;
        g_edge_result.fresh = 1;
        return;
    }

    /* 提取输出 */
    int out_len = tensor_element_count(s_output_tensor);
    if (out_len > EDGE_NUM_CLASSES) out_len = EDGE_NUM_CLASSES;
    if (out_len <= 0) {
        snprintf(g_edge_result.error_msg, sizeof(g_edge_result.error_msg),
                 "Invalid output shape");
        g_edge_result.ok = 0;
        g_edge_result.fresh = 1;
        return;
    }

    for (int i = 0; i < out_len; ++i) {
        g_edge_result.outputs[i] = output_value(i);
    }

    g_edge_result.predicted_label = argmax_output(out_len);
    g_edge_result.ok    = 1;
    g_edge_result.fresh = 1;
}

/* 发送窗口到推理队列 */
static void queue_window(const float* values, int source, int sample_index, int expected_label)
{
    if (s_window_queue == NULL) return;

    WindowMsg msg;
    msg.source        = source;
    msg.sample_index  = sample_index;
    msg.expected_label = expected_label;
    memcpy(msg.values, values, sizeof(msg.values));

    xQueueOverwrite(s_window_queue, &msg);
}

/*==================================================================
 *  Core 1 推理任务
 *==================================================================*/
static void model_task(void* parameter)
{
    (void)parameter;
    WindowMsg msg;

    for (;;) {
        /* 阻塞等待新窗口 */
        if (xQueueReceive(s_window_queue, &msg, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        /* 排空队列，只取最新的窗口（丢弃积压的旧数据） */
        WindowMsg latest;
        while (xQueueReceive(s_window_queue, &latest, 0) == pdTRUE) {
            msg = latest;
        }

        /* 执行推理，结果直接写入全局变量 */
        run_inference(&msg);

        /* 将结果副本送入队列（供 EdgeInference_ConsumeResult 消费） */
        if (s_result_queue != NULL) {
            EdgeInference_Result copy;
            memcpy(&copy, (const void*)&g_edge_result, sizeof(copy));
            xQueueOverwrite(s_result_queue, &copy);
        }

        /* 短延迟，给其他任务喘息 */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/*==================================================================
 *  模型初始化
 *==================================================================*/
static int setup_model(void)
{
    static tflite::MicroErrorReporter micro_reporter;
    s_error_reporter = &micro_reporter;

    s_model = tflite::GetModel(model_data);
    if (s_model == NULL || s_model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.println("[EdgeInf] Model schema version mismatch!");
        return 0;
    }

    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interp(
        s_model,
        resolver,
        s_tensor_arena,
        EDGE_TENSOR_ARENA_SIZE,
        s_error_reporter);

    s_interpreter = &static_interp;

    if (s_interpreter->AllocateTensors() != kTfLiteOk) {
        Serial.println("[EdgeInf] AllocateTensors() failed!");
        return 0;
    }

    s_input_tensor  = s_interpreter->input(0);
    s_output_tensor = s_interpreter->output(0);

    if (s_input_tensor == NULL || s_output_tensor == NULL) {
        Serial.println("[EdgeInf] NULL tensor pointer!");
        return 0;
    }

    /* 打印模型信息 */
    Serial.println("[EdgeInf] Model loaded OK.");
    Serial.print("[EdgeInf] Input  type: ");
    Serial.println(s_input_tensor->type == kTfLiteInt8 ? "int8" : "float32");
    Serial.print("[EdgeInf] Input  scale: ");
    Serial.println(s_input_tensor->params.scale, 8);
    Serial.print("[EdgeInf] Input  zero:  ");
    Serial.println(s_input_tensor->params.zero_point);
    Serial.print("[EdgeInf] Input  dims:  [");
    if (s_input_tensor->dims != NULL) {
        for (int i = 0; i < s_input_tensor->dims->size; ++i) {
            if (i > 0) Serial.print(", ");
            Serial.print(s_input_tensor->dims->data[i]);
        }
    }
    Serial.println("]");

    Serial.print("[EdgeInf] Output dims:  [");
    if (s_output_tensor->dims != NULL) {
        for (int i = 0; i < s_output_tensor->dims->size; ++i) {
            if (i > 0) Serial.print(", ");
            Serial.print(s_output_tensor->dims->data[i]);
        }
    }
    Serial.println("]");

    return 1;
}

/*==================================================================
 *  公开 API 实现
 *==================================================================*/

void EdgeInference_Init(void)
{
    Serial.println("[EdgeInf] ========================================");
    Serial.println("[EdgeInf] Edge Inference Module Initializing...");
    Serial.println("[EdgeInf] ========================================");

    /* 初始化全局结果 */
    memset((void*)&g_edge_result, 0, sizeof(g_edge_result));

    /* 加载模型 */
    if (!setup_model()) {
        Serial.println("[EdgeInf] FATAL: Model setup failed! Inference disabled.");
        g_edge_result.ok = 0;
        snprintf(g_edge_result.error_msg, sizeof(g_edge_result.error_msg),
                 "Model init failed");
        return;
    }

    /* 创建队列 */
    s_window_queue = xQueueCreate(EDGE_WINDOW_QUEUE_LEN, sizeof(WindowMsg));
    s_result_queue = xQueueCreate(EDGE_RESULT_QUEUE_LEN, sizeof(EdgeInference_Result));

    if (s_window_queue == NULL || s_result_queue == NULL) {
        Serial.println("[EdgeInf] FATAL: Queue creation failed!");
        return;
    }

    /* 创建 Core 1 推理任务（大堆栈） */
    BaseType_t ret = xTaskCreatePinnedToCore(
        model_task,
        "EdgeModel",
        EDGE_MODEL_TASK_STACK,
        NULL,
        EDGE_MODEL_TASK_PRIO,
        &s_model_task_handle,
        EDGE_MODEL_TASK_CORE);

    if (ret != pdPASS) {
        Serial.println("[EdgeInf] FATAL: Task creation failed!");
        return;
    }

    Serial.printf("[EdgeInf] Model task created on Core %d, stack=%d bytes\n",
                  (int)EDGE_MODEL_TASK_CORE, (int)EDGE_MODEL_TASK_STACK);
    Serial.printf("[EdgeInf] Simulation mode: %s\n",
                  EDGE_ENABLE_SIMULATION ? "ON" : "OFF");
    Serial.println("[EdgeInf] ========================================");

    /* 模拟模式下立即触发第一次推理 */
    if (EDGE_ENABLE_SIMULATION) {
        s_last_sim_ms = millis() - EDGE_SIM_INTERVAL_MS;
    }
}

int EdgeInference_UploadData(float value)
{
    if (s_window_count < EDGE_WINDOW_SIZE) {
        s_window_buffer[s_window_count] = value;
        ++s_window_count;
    }

    if (s_window_count >= EDGE_WINDOW_SIZE) {
        queue_window(s_window_buffer, 1, -1, -1);  /* source=1 (external) */
        s_window_count = 0;
        return 1;
    }
    return 0;
}

void EdgeInference_RunSimulation(void)
{
    if (s_sim_index < 0 || s_sim_index >= (int)SIM_SAMPLE_COUNT) {
        s_sim_index = 0;
    }

    const SimSample* sp = &s_sim_samples[s_sim_index];

    Serial.printf("[EdgeInf] Sim sample %d/%d  expected=%d (%s)\n",
                  s_sim_index + 1, (int)SIM_SAMPLE_COUNT,
                  sp->expected_label,
                  g_edge_class_names[sp->expected_label]);

    queue_window(sp->values, 0, s_sim_index, sp->expected_label);

    s_sim_index = (s_sim_index + 1) % SIM_SAMPLE_COUNT;
}

int EdgeInference_IsBusy(void)
{
    if (s_window_queue == NULL) return 0;
    return (uxQueueMessagesWaiting(s_window_queue) > 0) ? 1 : 0;
}

void EdgeInference_ConsumeResult(EdgeInference_Result* out)
{
    if (s_result_queue == NULL) return;

    EdgeInference_Result tmp;
    if (xQueueReceive(s_result_queue, &tmp, 0) == pdTRUE) {
        if (out != NULL) {
            memcpy(out, &tmp, sizeof(EdgeInference_Result));
        }
        g_edge_result.fresh = 0;
    }
}

void EdgeInference_Process(void)
{
#if EDGE_ENABLE_SIMULATION
    const unsigned long now = millis();
    if (s_window_queue != NULL && (now - s_last_sim_ms) >= (unsigned long)EDGE_SIM_INTERVAL_MS) {
        s_last_sim_ms = now;
        EdgeInference_RunSimulation();
    }
#endif
}