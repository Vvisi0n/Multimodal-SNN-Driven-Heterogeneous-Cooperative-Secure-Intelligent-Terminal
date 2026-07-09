#ifndef EDGE_INFERENCE_H
#define EDGE_INFERENCE_H

#include <Arduino.h>

#ifdef __cplusplus
extern "C" {
#endif

/*==================================================================
 *  防御性配置宏 —— 所有参数均有默认值，可在编译时通过 -D 覆盖
 *==================================================================*/
#ifndef EDGE_WINDOW_SIZE
#define EDGE_WINDOW_SIZE            10
#endif

#ifndef EDGE_FEATURE_DIM
#define EDGE_FEATURE_DIM            15
#endif

#ifndef EDGE_NUM_CLASSES
#define EDGE_NUM_CLASSES            7
#endif

#ifndef EDGE_TENSOR_ARENA_SIZE
#define EDGE_TENSOR_ARENA_SIZE      (40 * 1024)
#endif

#ifndef EDGE_INPUT_SCALE
#define EDGE_INPUT_SCALE            5000.0f /* 训练时的输入缩放因子 */
#endif

#ifndef EDGE_ENABLE_SIMULATION
#define EDGE_ENABLE_SIMULATION      1       /* 1=模拟数据模式, 0=外部输入模式 */
#endif

#ifndef EDGE_SIM_INTERVAL_MS
#define EDGE_SIM_INTERVAL_MS        2000
#endif

#ifndef EDGE_MODEL_TASK_STACK
#define EDGE_MODEL_TASK_STACK       32768   /* 大堆栈，推理吃性能 */
#endif

#ifndef EDGE_MODEL_TASK_PRIO
#define EDGE_MODEL_TASK_PRIO        3
#endif

#ifndef EDGE_MODEL_TASK_CORE
#define EDGE_MODEL_TASK_CORE        1       /* 核1 */
#endif

#ifndef EDGE_RESULT_QUEUE_LEN
#define EDGE_RESULT_QUEUE_LEN       1
#endif

#ifndef EDGE_WINDOW_QUEUE_LEN
#define EDGE_WINDOW_QUEUE_LEN       1
#endif

/* 模型数据头文件 —— 后期换模型只需改此宏 */
#ifndef EDGE_MODEL_INCLUDE
#define EDGE_MODEL_INCLUDE          "industry_fault_snn_model_data.h"
#endif

/*==================================================================
 *  类别名称
 *==================================================================*/
extern const char* g_edge_class_names[EDGE_NUM_CLASSES];

/*==================================================================
 *  全局推理结果结构体
 *==================================================================*/
typedef struct {
    int            predicted_label;                     /* 预测类别索引 */
    float          outputs[EDGE_NUM_CLASSES];           /* 各类别输出概率 */
    float          features[EDGE_FEATURE_DIM];          /* 提取的特征向量 */
    float          window_data[EDGE_WINDOW_SIZE];       /* 本次推理的输入窗口 */
    unsigned long  inference_time_ms;                   /* 推理耗时(ms) */
    int            ok;                                  /* 1=推理成功, 0=失败 */
    volatile int   fresh;                               /* 1=有新结果待读取 */
    char           error_msg[64];                       /* 错误信息 */
    int            source;                              /* 0=模拟, 1=外部输入 */
    int            sample_index;                        /* 模拟样本索引 */
    int            expected_label;                      /* 模拟时的期望标签 */
} EdgeInference_Result;

extern EdgeInference_Result g_edge_result;

/*==================================================================
 *  API 函数
 *==================================================================*/

/**
 * @brief 初始化端侧推理模型并创建 Core1 推理任务
 * @note  必须在 FreeRTOS 调度器启动后调用（setup 中调用即可）
 */
void EdgeInference_Init(void);

/**
 * @brief 上传一个采样数据点（外部输入路径）
 * @param value 单个传感器采样值
 * @return 1=窗口已满并已送入推理队列, 0=窗口未满
 */
int EdgeInference_UploadData(float value);

/**
 * @brief 手动触发一次模拟推理（模拟模式或调试用）
 */
void EdgeInference_RunSimulation(void);

/**
 * @brief 查询推理任务是否繁忙
 * @return 1=正在推理中, 0=空闲
 */
int EdgeInference_IsBusy(void);

/**
 * @brief 获取结果并标记为已读
 * @param out 传出结果结构体指针，传 NULL 则仅标记已读
 */
void EdgeInference_ConsumeResult(EdgeInference_Result* out);

void EdgeInference_Process(void);

#ifdef __cplusplus
}
#endif

#endif /* EDGE_INFERENCE_H */