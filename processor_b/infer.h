/**
 * @file    infer.h
 * @brief   TFLite-Micro 四分类检测推理接口
 *
 * 模型输入：96x96x3 RGB
 * 输出：四分类（CopperSpacer / background / hand / wire）
 * 量化：INT8 对称量化
 */

#ifndef INFER_H
#define INFER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 初始化 TFLite 模型、分配张量空间 */
bool infer_init(void);

/*
 * 执行单次推理
 * rgb_buf:      输入 RGB888 图像缓冲区
 * src_w/src_h:  原图宽高
 * out_prob:     输出置信度（[0,1]）
 * out_class:    输出分类索引 (0=CopperSpacer, 1=background, 2=hand, 3=wire)
 * out_infer_ms: 输出推理耗时（毫秒）
 * 返回值：true=成功，false=失败
 */
bool infer_run(const uint8_t* rgb_buf, int src_w, int src_h,
               float* out_prob, int* out_class,
               unsigned long* out_infer_ms);

/* 根据分类索引获取类别名称 */
const char* infer_class_name(int class_id);

#ifdef __cplusplus
}
#endif

#endif