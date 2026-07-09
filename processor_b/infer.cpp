/**
 * @file    infer.cpp
 * @brief   TFLite-Micro 四分类检测推理引擎实现
 *
 * 硬件内存分配：张量 arena 分配在 PSRAM
 * 预处理：双线性插值缩放 + 归一化 + INT8 量化
 * 后处理：反量化 + softmax + argmax
 */

#include "infer.h"
#include "Arduino.h"
#include "quad_det.h"
#include <math.h>
#include <stdio.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/schema/schema_generated.h"

/* --------------------------------------------------------------------------
   模型常量
   -------------------------------------------------------------------------- */
static constexpr int kModelWidth    = 96;
static constexpr int kModelHeight   = 96;
static constexpr int kModelChannels = 3;
static constexpr int kNumClasses    = 4;
static constexpr int kTensorArenaSize = 600 * 1024;

static const char* kClassNames[] = {"CopperSpacer", "background", "hand", "wire"};

/* --------------------------------------------------------------------------
   TFLite 全局状态
   -------------------------------------------------------------------------- */
static const tflite::Model* tflite_model = nullptr;
static tflite::MicroInterpreter* interpreter = nullptr;
static TfLiteTensor* model_input = nullptr;
static TfLiteTensor* model_output = nullptr;
static uint8_t* tensor_arena = nullptr;

static float input_scale;
static int   input_zero_point;
static float output_scale;
static int   output_zero_point;

/* --------------------------------------------------------------------------
   预处理：RGB888 → 模型输入 INT8 张量
   算法：双线性插值缩放 → [-1, 1] 归一化 → INT8 量化
   -------------------------------------------------------------------------- */
static void preprocess_rgb_to_int8(const uint8_t* src, int src_w, int src_h,
                                    int8_t* dst, int dst_w, int dst_h) {
    float x_ratio = (float)(src_w - 1) / (float)(dst_w - 1);
    float y_ratio = (float)(src_h - 1) / (float)(dst_h - 1);
    float inv_scale = 1.0f / input_scale;

    // 遍历目标像素，逐点插值
    for (int y = 0; y < dst_h; y++) {
        float src_y = y * y_ratio;
        int y0 = (int)src_y;
        int y1 = (y0 + 1 < src_h) ? (y0 + 1) : y0;
        float y_frac = src_y - y0;

        for (int x = 0; x < dst_w; x++) {
            float src_x = x * x_ratio;
            int x0 = (int)src_x;
            int x1 = (x0 + 1 < src_w) ? (x0 + 1) : x0;
            float x_frac = src_x - x0;

            // 双线性插值：对四个邻域像素加权
            for (int c = 0; c < 3; c++) {
                float p00 = src[(y0 * src_w + x0) * 3 + c];
                float p01 = src[(y0 * src_w + x1) * 3 + c];
                float p10 = src[(y1 * src_w + x0) * 3 + c];
                float p11 = src[(y1 * src_w + x1) * 3 + c];

                float top    = p00 * (1.0f - x_frac) + p01 * x_frac;
                float bottom = p10 * (1.0f - x_frac) + p11 * x_frac;
                float pixel  = top  * (1.0f - y_frac) + bottom * y_frac;

                // 归一化到 [-1, 1] 再按量化参数反量化到 INT8
                float normalized = (pixel / 127.5f) - 1.0f;
                int quantized = (int)roundf(normalized * inv_scale) + input_zero_point;

                // 钳位到 INT8 范围
                if (quantized > 127)  quantized = 127;
                if (quantized < -128) quantized = -128;

                dst[(y * dst_w + x) * 3 + c] = (int8_t)quantized;
            }
        }
    }
}

/* --------------------------------------------------------------------------
   模型初始化
   流程：加载模型 → 分配 PSRAM → 创建解释器 → 分配张量 → 缓存量化参数
   返回：true=成功，false=失败
   -------------------------------------------------------------------------- */
bool infer_init(void) {
    // 从模型数组加载 flatbuffer
    Serial.printf("[TFLite] Loading model...\n");
    tflite_model = tflite::GetModel(quad_det_model_data);

    // 版本兼容性检查
    if (tflite_model->version() != TFLITE_SCHEMA_VERSION) {
        Serial.printf("[TFLite] 错误：Schema版本不匹配！模型=%d 运行时=%d\n",
                      tflite_model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }
    Serial.printf("[TFLite] 模型加载完成：%u 字节\n", quad_det_model_len);

    // 在 PSRAM 分配张量 arena
    tensor_arena = (uint8_t*)heap_caps_malloc(
        kTensorArenaSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!tensor_arena) {
        Serial.printf("[TFLite] 错误：PSRAM 分配张量缓冲区失败\n");
        return false;
    }
    Serial.printf("[TFLite] 张量缓冲区：%d KB (PSRAM)\n", kTensorArenaSize / 1024);

    // 创建 TFLite 解释器
    static tflite::AllOpsResolver resolver;
    static tflite::MicroInterpreter static_interpreter(
        tflite_model, resolver, tensor_arena, kTensorArenaSize);
    interpreter = &static_interpreter;

    // 为模型张量分配内存
    TfLiteStatus alloc_status = interpreter->AllocateTensors(true);
    if (alloc_status != kTfLiteOk) {
        Serial.printf("[TFLite] 错误：张量分配失败，状态码=%d\n", alloc_status);
        return false;
    }

    // 获取输入输出张量指针
    model_input  = interpreter->input(0);
    model_output = interpreter->output(0);

    // 缓存量化参数（推理时每次都要用，提前存好）
    input_scale       = model_input->params.scale;
    input_zero_point  = model_input->params.zero_point;
    output_scale      = model_output->params.scale;
    output_zero_point = model_output->params.zero_point;

    // 打印调试信息，确认模型结构正确
    Serial.printf("[TFLite] 输入维度：[%d,%d,%d,%d] 类型=%d\n",
                  model_input->dims->data[0], model_input->dims->data[1],
                  model_input->dims->data[2], model_input->dims->data[3],
                  model_input->type);
    Serial.printf("[TFLite] 输出维度：[%d,%d] 类型=%d\n",
                  model_output->dims->data[0], model_output->dims->data[1],
                  model_output->type);
    Serial.printf("[TFLite] 输入量化：scale=%.6f zero_point=%d\n", input_scale, input_zero_point);
    Serial.printf("[TFLite] 输出量化：scale=%.6f zero_point=%d\n", output_scale, output_zero_point);

    // 维度校验
    if (model_input->dims->data[1] != kModelHeight ||
        model_input->dims->data[2] != kModelWidth  ||
        model_input->dims->data[3] != kModelChannels) {
        Serial.printf("[TFLite] 错误：输入维度不匹配，期望 %dx%dx%d\n",
                      kModelHeight, kModelWidth, kModelChannels);
        return false;
    }

    Serial.printf("[TFLite] 初始化完成\n");
    return true;
}

/* --------------------------------------------------------------------------
   执行一次完整推理
   流程：预处理 → 调用解释器 → 反量化 → softmax → argmax
   -------------------------------------------------------------------------- */
bool infer_run(const uint8_t* rgb_buf, int src_w, int src_h,
               float* out_prob, int* out_class,
               unsigned long* out_infer_ms) {
    // 预处理：缩放到模型尺寸，量化到 INT8
    preprocess_rgb_to_int8(rgb_buf, src_w, src_h,
                           model_input->data.int8,
                           kModelWidth, kModelHeight);

    // 推理计时
    unsigned long t0 = millis();

    // 调用模型
    TfLiteStatus invoke_status = interpreter->Invoke();
    if (invoke_status != kTfLiteOk) {
        return false;
    }

    *out_infer_ms = millis() - t0;

    // 输出反量化 → logits
    int8_t* out_data = model_output->data.int8;
    float logits[kNumClasses];
    float max_logit = -INFINITY;
    for (int i = 0; i < kNumClasses; i++) {
        logits[i] = (out_data[i] - output_zero_point) * output_scale;
        if (logits[i] > max_logit) max_logit = logits[i];
    }

    // softmax 归一化得到概率
    float probs[kNumClasses];
    float sum_exp = 0.0f;
    for (int i = 0; i < kNumClasses; i++) {
        probs[i] = expf(logits[i] - max_logit);
        sum_exp += probs[i];
    }
    for (int i = 0; i < kNumClasses; i++) {
        probs[i] /= sum_exp;
    }

    // argmax 找最大概率类别
    int predicted = 0;
    float max_prob = probs[0];
    for (int i = 1; i < kNumClasses; i++) {
        if (probs[i] > max_prob) {
            max_prob = probs[i];
            predicted = i;
        }
    }

    *out_prob  = max_prob;
    *out_class = predicted;
    return true;
}

/* --------------------------------------------------------------------------
   获取分类名称字符串
   -------------------------------------------------------------------------- */
const char* infer_class_name(int class_id) {
    if (class_id < 0 || class_id >= kNumClasses) return "unknown";
    return kClassNames[class_id];
}