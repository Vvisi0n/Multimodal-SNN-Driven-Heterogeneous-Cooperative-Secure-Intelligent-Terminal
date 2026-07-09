import os
import random as python_random
import numpy as np
import tensorflow as tf
import warnings

warnings.filterwarnings("ignore")

# 配置GPU内存增长
gpus = tf.config.experimental.list_physical_devices('GPU')
if gpus:
    try:
        for gpu in gpus:
            tf.config.experimental.set_memory_growth(gpu, True)
    except RuntimeError as e:
        print(e)

SEED = 42
python_random.seed(SEED)
np.random.seed(SEED)
tf.random.set_seed(SEED)
os.environ["PYTHONHASHSEED"] = str(SEED)

BATCH_SIZE = 32
IMG_SIZE = (96, 96)
NUM_CLASSES = 4
DATA_DIR = "./datasets_4class"
MODEL_NAME = "quad_det_snn"
EPOCHS_SNN = 80  # SNN需要更多epochs
LEARNING_RATE = 0.001
LABEL_SMOOTHING = 0.1

# SNN 参数
TIMESTEPS = 8  # 时间步数
V_THRESHOLD = 0.5  # 阈值电压
V_RESET = 0.0  # 复位电压
TAU_MEM = 0.5  # 膜电位时间常数


def get_datasets():
    """加载数据集，添加SNN特有的数据增强"""
    train_ds = tf.keras.preprocessing.image_dataset_from_directory(
        os.path.join(DATA_DIR, "train"),
        validation_split=None,
        subset=None,
        seed=42,
        image_size=IMG_SIZE,
        batch_size=BATCH_SIZE,
        label_mode="int",
    )

    val_ds = tf.keras.preprocessing.image_dataset_from_directory(
        os.path.join(DATA_DIR, "val"),
        validation_split=None,
        subset=None,
        seed=42,
        image_size=IMG_SIZE,
        batch_size=BATCH_SIZE,
        label_mode="int",
    )

    class_names = train_ds.class_names
    print(f"类别: {class_names}")

    # SNN的数据增强需要保持时间维度
    data_augmentation = tf.keras.Sequential([
        tf.keras.layers.RandomFlip("horizontal"),
        tf.keras.layers.RandomRotation(0.05),
        tf.keras.layers.RandomZoom(0.05),
        tf.keras.layers.RandomBrightness(0.1),
    ])

    def preprocess_train(x, y):
        # 对每个时间步应用相同的数据增强
        x = data_augmentation(x, training=True)
        x = tf.cast(x, tf.float32) / 255.0
        return x, y

    def preprocess_val(x, y):
        x = tf.cast(x, tf.float32) / 255.0
        return x, y

    AUTOTUNE = tf.data.AUTOTUNE
    train_ds = train_ds.map(preprocess_train, num_parallel_calls=AUTOTUNE)
    train_ds = train_ds.prefetch(buffer_size=AUTOTUNE)
    val_ds = val_ds.map(preprocess_val, num_parallel_calls=AUTOTUNE)
    val_ds = val_ds.prefetch(buffer_size=AUTOTUNE)

    return train_ds, val_ds, class_names


class LIFSpikingLayer(tf.keras.layers.Layer):
    """
    Leaky Integrate-and-Fire (LIF) 神经元层
    用于SNN的脉冲发放层
    """
    
    def __init__(self, units, tau_mem=0.5, v_threshold=0.5, v_reset=0.0, 
                 trainable_tau=False, **kwargs):
        super(LIFSpikingLayer, self).__init__(**kwargs)
        self.units = units
        self.tau_mem = tau_mem
        self.v_threshold = v_threshold
        self.v_reset = v_reset
        self.trainable_tau = trainable_tau
        
        # 权重初始化
        self.kernel = None
        self.recurrent_kernel = None
        self.bias = None
        self.tau = None
        
    def build(self, input_shape):
        input_dim = input_shape[-1]
        
        # 输入权重
        self.kernel = self.add_weight(
            name='kernel',
            shape=(input_dim, self.units),
            initializer='glorot_uniform',
            trainable=True
        )
        
        # 循环权重（递归连接）
        self.recurrent_kernel = self.add_weight(
            name='recurrent_kernel',
            shape=(self.units, self.units),
            initializer='glorot_uniform',
            trainable=True
        )
        
        # 偏置
        self.bias = self.add_weight(
            name='bias',
            shape=(self.units,),
            initializer='zeros',
            trainable=True
        )
        
        # 时间常数（可训练或固定）
        if self.trainable_tau:
            self.tau = self.add_weight(
                name='tau',
                shape=(self.units,),
                initializer=tf.constant_initializer(self.tau_mem),
                trainable=True,
                constraint=lambda x: tf.clip_by_value(x, 0.1, 1.0)
            )
        else:
            self.tau = tf.constant(self.tau_mem, dtype=tf.float32)
            
        super(LIFSpikingLayer, self).build(input_shape)
    
    def call(self, inputs, states=None, return_state=False):
        """
        前向传播：按时间步展开
        inputs: [batch, timesteps, features] 或 [batch, features]
        """
        # 如果输入没有时间维度，添加一个
        if len(inputs.shape) == 2:
            inputs = tf.expand_dims(inputs, axis=1)
            
        batch_size = tf.shape(inputs)[0]
        timesteps = tf.shape(inputs)[1]
        
        # 初始化状态
        if states is None:
            v = tf.zeros([batch_size, self.units], dtype=tf.float32) + self.v_reset
            s = tf.zeros([batch_size, self.units], dtype=tf.float32)
        else:
            v, s = states
        
        outputs_spikes = []
        outputs_voltage = []
        
        for t in range(timesteps):
            # 当前时间步输入
            x_t = inputs[:, t, :]
            
            # 输入电流 = 输入权重 * 输入 + 递归权重 * 前一时刻输出（脉冲）
            i_in = tf.matmul(x_t, self.kernel)
            i_rec = tf.matmul(s, self.recurrent_kernel)
            i_total = i_in + i_rec + self.bias
            
            # LIF动态：膜电位更新 (leaky integrate)
            # v = v * (1 - dt/tau) + i * (dt/tau)
            # 使用欧拉方法，dt=1简化
            decay = tf.exp(-1.0 / self.tau)
            v = v * decay + i_total * (1.0 - decay)
            
            # 发放脉冲
            spike = tf.cast(v >= self.v_threshold, dtype=tf.float32)
            
            # 复位电压（软复位：减去阈值）
            v = v - spike * self.v_threshold
            
            # 保存输出
            outputs_spikes.append(spike)
            outputs_voltage.append(v)
            
            # 更新状态
            s = spike
        
        # 堆叠时间步
        spike_output = tf.stack(outputs_spikes, axis=1)  # [batch, timesteps, units]
        voltage_output = tf.stack(outputs_voltage, axis=1)  # [batch, timesteps, units]
        
        if return_state:
            return spike_output, (v, s)
        return spike_output
    
    def compute_output_shape(self, input_shape):
        if len(input_shape) == 2:
            return (input_shape[0], 1, self.units)
        return (input_shape[0], input_shape[1], self.units)


class SNNCell(tf.keras.layers.Layer):
    """
    SNN单元：包含卷积+批归一化+LIF脉冲层
    """
    
    def __init__(self, filters, kernel_size=3, strides=1, **kwargs):
        super(SNNCell, self).__init__(**kwargs)
        self.conv = tf.keras.layers.Conv2D(
            filters, kernel_size, strides=strides, 
            padding='same', use_bias=False
        )
        self.bn = tf.keras.layers.BatchNormalization()
        self.lif = LIFSpikingLayer(
            units=filters * IMG_SIZE[0] * IMG_SIZE[1] // (4 ** len(kwargs.get('stride_count', [1]))),
            tau_mem=TAU_MEM,
            v_threshold=V_THRESHOLD
        )
        self.filters = filters
        
    def call(self, inputs, training=False):
        # 输入: [batch, timesteps, h, w, c]
        # 对每个时间步处理
        timesteps = tf.shape(inputs)[1]
        outputs = []
        
        for t in range(timesteps):
            x_t = inputs[:, t, :, :, :]
            x_t = self.conv(x_t)
            x_t = self.bn(x_t, training=training)
            # 展平后通过LIF层
            x_t = tf.reshape(x_t, [tf.shape(x_t)[0], -1])
            x_t = self.lif(x_t, training=training)
            x_t = tf.reshape(x_t, [tf.shape(x_t)[0], 1, self.filters, IMG_SIZE[0]//2, IMG_SIZE[1]//2])
            outputs.append(x_t)
            
        return tf.concat(outputs, axis=1)  # [batch, timesteps, filters, h, w]


def build_snn_model():
    """
    构建SNN模型
    使用时间维度的展开处理
    """
    # 输入: [batch, timesteps, height, width, channels]
    inputs = tf.keras.Input(shape=(TIMESTEPS, IMG_SIZE[0], IMG_SIZE[1], 3))
    
    # 第一个卷积层 + LIF
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.Conv2D(32, 3, strides=2, padding='same', use_bias=False)
    )(inputs)
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.BatchNormalization()
    )(x)
    # 展平并使用LIF
    x = tf.reshape(x, [-1, TIMESTEPS, 32 * IMG_SIZE[0] * IMG_SIZE[1] // 4])
    x = LIFSpikingLayer(units=256, tau_mem=TAU_MEM, v_threshold=V_THRESHOLD)(x)
    x = tf.reshape(x, [-1, TIMESTEPS, 16, 16, 1])  # 近似reshape
    
    # 第二个卷积层 + LIF
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.Conv2D(64, 3, strides=2, padding='same', use_bias=False)
    )(x)
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.BatchNormalization()
    )(x)
    x = tf.reshape(x, [-1, TIMESTEPS, 64 * 8 * 8])
    x = LIFSpikingLayer(units=128, tau_mem=TAU_MEM, v_threshold=V_THRESHOLD)(x)
    x = tf.reshape(x, [-1, TIMESTEPS, 8, 8, 1])
    
    # 第三个卷积层 + LIF
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.Conv2D(128, 3, strides=2, padding='same', use_bias=False)
    )(x)
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.BatchNormalization()
    )(x)
    x = tf.reshape(x, [-1, TIMESTEPS, 128 * 4 * 4])
    x = LIFSpikingLayer(units=64, tau_mem=TAU_MEM, v_threshold=V_THRESHOLD)(x)
    x = tf.reshape(x, [-1, TIMESTEPS, 4, 4, 1])
    
    # 全局平均池化（跨时间步）
    x = tf.keras.layers.TimeDistributed(
        tf.keras.layers.GlobalAveragePooling2D()
    )(x)
    
    # 展平时间步和特征
    x = tf.reshape(x, [-1, TIMESTEPS * 64])
    
    # 输出层（不使用脉冲）
    x = tf.keras.layers.Dropout(0.3)(x)
    x = tf.keras.layers.Dense(NUM_CLASSES)(x)
    
    # 跨时间步平均输出
    outputs = tf.reduce_mean(
        tf.reshape(x, [-1, TIMESTEPS, NUM_CLASSES]), 
        axis=1
    )
    
    model = tf.keras.Model(inputs=inputs, outputs=outputs)
    return model


def build_snn_model_alternative():
    """
    替代SNN模型：使用更简单的结构
    每个时间步独立处理，最后聚合
    """
    inputs = tf.keras.Input(shape=(TIMESTEPS, IMG_SIZE[0], IMG_SIZE[1], 3))
    
    # 使用TimeDistributed包装标准CNN层
    # 然后使用LIF层处理时间信息
    
    # 共享权重的时间步处理
    def snn_block(filters, kernel_size=3, pooling=True):
        def apply(x):
            x = tf.keras.layers.Conv2D(filters, kernel_size, padding='same', use_bias=False)(x)
            x = tf.keras.layers.BatchNormalization()(x)
            x = tf.keras.layers.ReLU()(x)
            if pooling:
                x = tf.keras.layers.MaxPooling2D(2)(x)
            return x
        return apply
    
    # 对每个时间步应用相同的处理
    x = tf.keras.layers.TimeDistributed(
        tf.keras.Sequential([
            tf.keras.layers.Conv2D(32, 3, strides=1, padding='same', use_bias=False),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.ReLU(),
            tf.keras.layers.MaxPooling2D(2),
            tf.keras.layers.Conv2D(64, 3, padding='same', use_bias=False),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.ReLU(),
            tf.keras.layers.MaxPooling2D(2),
            tf.keras.layers.Conv2D(128, 3, padding='same', use_bias=False),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.ReLU(),
            tf.keras.layers.MaxPooling2D(2),
            tf.keras.layers.Conv2D(256, 3, padding='same', use_bias=False),
            tf.keras.layers.BatchNormalization(),
            tf.keras.layers.ReLU(),
            tf.keras.layers.GlobalAveragePooling2D(),
        ])
    )(inputs)
    
    # 使用LIF层处理时间信息
    x = LIFSpikingLayer(units=128, tau_mem=TAU_MEM, v_threshold=V_THRESHOLD)(x)
    
    # 时间维度上的聚合
    x = tf.reduce_mean(x, axis=1)  # [batch, units]
    
    x = tf.keras.layers.Dropout(0.3)(x)
    outputs = tf.keras.layers.Dense(NUM_CLASSES)(x)
    
    model = tf.keras.Model(inputs=inputs, outputs=outputs)
    return model


def prepare_snn_data(dataset, timesteps=TIMESTEPS):
    """
    将标准数据集转换为SNN所需的时间序列格式
    """
    def expand_timesteps(images, labels):
        # 重复图像到时间步维度
        images = tf.expand_dims(images, axis=1)  # [batch, 1, h, w, c]
        images = tf.repeat(images, timesteps, axis=1)  # [batch, timesteps, h, w, c]
        # 添加微小的噪声作为时间差异（可选）
        noise = tf.random.normal(tf.shape(images), mean=0.0, stddev=0.01)
        images = tf.clip_by_value(images + noise, 0.0, 1.0)
        return images, labels
    
    return dataset.map(expand_timesteps, num_parallel_calls=tf.data.AUTOTUNE)


def train_snn(model, train_ds, val_ds):
    """训练SNN模型"""
    
    # 准备SNN数据
    train_ds_snn = prepare_snn_data(train_ds, TIMESTEPS)
    val_ds_snn = prepare_snn_data(val_ds, TIMESTEPS)
    
    model.compile(
        optimizer=tf.keras.optimizers.Adam(learning_rate=LEARNING_RATE),
        loss=tf.keras.losses.SparseCategoricalCrossentropy(from_logits=True),
        metrics=['accuracy']
    )
    
    callbacks = [
        tf.keras.callbacks.EarlyStopping(
            monitor='val_accuracy', patience=20, restore_best_weights=True
        ),
        tf.keras.callbacks.ReduceLROnPlateau(
            monitor='val_loss', patience=10, factor=0.5, min_lr=1e-7, verbose=1
        ),
    ]
    
    print(f"[SNN训练] 开始训练，时间步={TIMESTEPS}")
    history = model.fit(
        train_ds_snn,
        validation_data=val_ds_snn,
        epochs=EPOCHS_SNN,
        callbacks=callbacks,
        verbose=1
    )
    
    return model, history


class ConvertToTFLiteSNN:
    """SNN到TFLite的转换器"""
    
    def __init__(self, model, timesteps=TIMESTEPS):
        self.model = model
        self.timesteps = timesteps
    
    def convert(self):
        # 对于SNN，需要展开时间步
        # 这里使用标准转换
        converter = tf.lite.TFLiteConverter.from_keras_model(self.model)
        converter.optimizations = [tf.lite.Optimize.DEFAULT]
        
        # 支持FP16量化（如果需要）
        # converter.target_spec.supported_types = [tf.float16]
        
        tflite_model = converter.convert()
        return tflite_model


def convert_to_c_header(tflite_model, model_name=MODEL_NAME, timesteps=TIMESTEPS):
    """导出C头文件（包含时间步信息）"""
    c_path = f"{model_name}.h"
    hex_array = ", ".join([f"0x{b:02x}" for b in tflite_model])
    
    with open(c_path, "w", encoding="utf-8") as f:
        f.write(f"#ifndef {model_name.upper()}_MODEL_H_\n")
        f.write(f"#define {model_name.upper()}_MODEL_H_\n\n")
        f.write(f"// 模型: {model_name} (SNN范式)\n")
        f.write(f"// 大小: {len(tflite_model)} bytes\n")
        f.write(f"// 输入: [timesteps={timesteps}, {IMG_SIZE[0]}x{IMG_SIZE[1]}x3] 归一化到[0,1]\n")
        f.write(f"// 输出: logits [{NUM_CLASSES}]\n")
        f.write(f"// 注意: 需要运行多个时间步并在时间维度平均\n\n")
        f.write(f"#define SNN_TIMESTEPS {timesteps}\n")
        f.write(f"#define SNN_INPUT_SIZE {IMG_SIZE[0] * IMG_SIZE[1] * 3}\n")
        f.write(f"#define SNN_NUM_CLASSES {NUM_CLASSES}\n\n")
        f.write(f"alignas(16) const unsigned char {model_name}_model_data[] = {{\n  {hex_array}\n}};\n\n")
        f.write(f"const unsigned int {model_name}_model_len = {len(tflite_model)};\n\n")
        f.write(f"#endif  // {model_name.upper()}_MODEL_H_\n")
    
    print(f"[C Header] 已导出SNN头文件: {c_path}")


def verify_snn_tflite(tflite_model, val_ds):
    """验证SNN TFLite模型"""
    interpreter = tf.lite.Interpreter(model_content=tflite_model)
    interpreter.allocate_tensors()
    
    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()
    
    correct = 0
    total = 0
    
    for images, labels in val_ds:
        # 扩展时间步
        images = tf.expand_dims(images, axis=1)
        images = tf.repeat(images, TIMESTEPS, axis=1)
        
        for i in range(images.shape[0]):
            img = images[i].numpy()
            
            # 确保输入形状正确
            if input_details[0]['shape'][0] is None:
                input_shape = [1] + list(input_details[0]['shape'][1:])
            else:
                input_shape = input_details[0]['shape']
            
            # 处理不同的输入类型
            if input_details[0]['dtype'] == np.float32:
                interpreter.set_tensor(
                    input_details[0]['index'],
                    np.expand_dims(img, axis=0).astype(np.float32)
                )
            elif input_details[0]['dtype'] == np.float16:
                interpreter.set_tensor(
                    input_details[0]['index'],
                    np.expand_dims(img, axis=0).astype(np.float16)
                )
            else:
                # 量化输入
                scale, zero_point = input_details[0]['quantization']
                img_quant = (img / scale + zero_point).astype(np.int8)
                interpreter.set_tensor(
                    input_details[0]['index'],
                    np.expand_dims(img_quant, axis=0)
                )
            
            interpreter.invoke()
            output = interpreter.get_tensor(output_details[0]['index'])
            pred = np.argmax(output[0])
            
            if pred == labels[i].numpy():
                correct += 1
            total += 1
    
    print(f"[验证] SNN TFLite模型准确率: {correct/total:.4f} ({correct}/{total})")
    return correct / total


def main():
    print(f"[配置] 输入尺寸: {IMG_SIZE}")
    print(f"[配置] 批次大小: {BATCH_SIZE}")
    print(f"[配置] 类别数: {NUM_CLASSES}")
    print(f"[配置] 数据目录: {DATA_DIR}")
    print(f"[配置] SNN时间步: {TIMESTEPS}")
    print(f"[配置] 阈值电压: {V_THRESHOLD}")
    
    print("\n" + "=" * 50)
    print("阶段 1: 加载数据集")
    print("=" * 50)
    train_ds, val_ds, class_names = get_datasets()
    
    print("\n" + "=" * 50)
    print("阶段 2: 构建SNN模型")
    print("=" * 50)
    model = build_snn_model_alternative()
    model.summary()
    
    print("\n" + "=" * 50)
    print(f"阶段 3: 训练SNN模型 ({EPOCHS_SNN} epochs)")
    print("=" * 50)
    model, history = train_snn(model, train_ds, val_ds)
    
    # 保存模型
    snn_path = f"{MODEL_NAME}_snn.keras"
    model.save(snn_path)
    print(f"[保存] SNN模型: {snn_path}")
    
    print("\n" + "=" * 50)
    print("阶段 4: 转换为TFLite")
    print("=" * 50)
    converter = ConvertToTFLiteSNN(model, TIMESTEPS)
    tflite_model = converter.convert()
    
    tflite_path = f"{MODEL_NAME}.tflite"
    with open(tflite_path, "wb") as f:
        f.write(tflite_model)
    print(f"[TFLite] 模型已保存: {tflite_path}")
    print(f"[TFLite] 模型大小: {len(tflite_model) / 1024:.1f} KB")
    
    print("\n" + "=" * 50)
    print("阶段 5: 导出ESP32 C头文件")
    print("=" * 50)
    convert_to_c_header(tflite_model)
    
    print("\n" + "=" * 50)
    print("阶段 6: 验证模型")
    print("=" * 50)
    verify_snn_tflite(tflite_model, val_ds)
    
    print("\n[完成] SNN流程结束!")
    print(f"  - SNN模型: {MODEL_NAME}.keras")
    print(f"  - TFLite: {MODEL_NAME}.tflite")
    print(f"  - C头文件: {MODEL_NAME}.h")
    print(f"  - 时间步: {TIMESTEPS}")


if __name__ == "__main__":
    main()