import os
import numpy as np
import tensorflow as tf

# =========================
# 1. 参数配置
# =========================
np.random.seed(42)
tf.random.set_seed(42)

SIGNAL_LEN = 32          # 每路信号长度
NUM_CHANNELS = 3         # 三种一维信号
INPUT_DIM = SIGNAL_LEN * NUM_CHANNELS
NUM_CLASSES = 4
SAMPLES_PER_CLASS = 800

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
MODEL_DIR = os.path.join(OUT_DIR, ".")
TFLITE_PATH = os.path.join(MODEL_DIR, "fault_model.tflite")
HEADER_PATH = os.path.join(MODEL_DIR, "include", "model_data.h")


def estimate_model_flops(model, input_shape, batch_size=1, dtype=tf.float32, print_result=True):
    """Estimate FLOPs for one forward pass of a TensorFlow/Keras model.

    Args:
        model: A tf.keras.Model or Keras-compatible callable model.
        input_shape: Sample shape, for example (96,), or model input shape,
            for example (None, 96). A list/tuple of shapes is also supported
            for multi-input models.
        batch_size: Batch size used when input_shape omits the batch dimension
            or uses None as the batch dimension.
        dtype: Input tensor dtype used to trace the model graph.
        print_result: If True, print a compact FLOPs summary.

    Returns:
        The estimated total float operations for the traced forward pass.
    """
    from tensorflow.python.framework.convert_to_constants import convert_variables_to_constants_v2

    def _shape_with_batch(shape):
        shape = tuple(shape.as_list()) if isinstance(shape, tf.TensorShape) else tuple(shape)
        if not shape:
            raise ValueError("input_shape must not be empty.")
        if shape[0] is None:
            return (batch_size,) + shape[1:]
        if len(shape) == 1:
            return (batch_size,) + shape
        return shape

    is_multi_input = (
        isinstance(input_shape, (list, tuple))
        and input_shape
        and isinstance(input_shape[0], (list, tuple, tf.TensorShape))
    )
    input_shapes = input_shape if is_multi_input else [input_shape]
    input_specs = [
        tf.TensorSpec(_shape_with_batch(shape), dtype)
        for shape in input_shapes
    ]

    @tf.function
    def _forward_pass(*inputs):
        if len(inputs) == 1:
            return model(inputs[0], training=False)
        return model(list(inputs), training=False)

    concrete_func = _forward_pass.get_concrete_function(*input_specs)
    frozen_func = convert_variables_to_constants_v2(concrete_func)

    with tf.Graph().as_default() as graph:
        tf.graph_util.import_graph_def(frozen_func.graph.as_graph_def(), name="")
        run_meta = tf.compat.v1.RunMetadata()
        opts = tf.compat.v1.profiler.ProfileOptionBuilder.float_operation()
        opts["output"] = "none"
        flops = tf.compat.v1.profiler.profile(
            graph=graph,
            run_meta=run_meta,
            cmd="op",
            options=opts,
        )

    total_flops = int(flops.total_float_ops) if flops is not None else 0
    if print_result:
        print(
            f"Model FLOPs (batch_size={batch_size}): "
            f"{total_flops:,} ({total_flops / 1e6:.6f} MFLOPs)"
        )
    return total_flops


# =========================
# 2. 模拟工业信号
# =========================
def generate_base_signals(t, phase_shift=0.0):
    """
    返回三路基础信号（正常工况）
    """
    s1 = 0.8 * np.sin(2 * np.pi * 1.0 * t + phase_shift)
    s2 = 0.6 * np.sin(2 * np.pi * 1.5 * t + phase_shift * 0.8)
    s3 = 0.7 * np.sin(2 * np.pi * 0.7 * t + phase_shift * 1.2)
    return s1, s2, s3

def add_noise(x, scale=0.08):
    return x + np.random.normal(0, scale, size=x.shape)

def generate_sample(label):
    """
    label:
      0 normal
      1 imbalance
      2 misalignment
      3 bearing_fault
    """
    t = np.linspace(0, 1, SIGNAL_LEN, endpoint=False)
    phase = np.random.uniform(0, 2 * np.pi)

    ch1, ch2, ch3 = generate_base_signals(t, phase)

    if label == 0:
        # 正常：只有轻微噪声
        ch1 = add_noise(ch1, 0.05)
        ch2 = add_noise(ch2, 0.05)
        ch3 = add_noise(ch3, 0.05)

    elif label == 1:
        # 不平衡：低频幅值增大
        ch1 = 1.5 * ch1 + 0.2 * np.sin(2 * np.pi * 0.5 * t)
        ch2 = 1.3 * ch2
        ch3 = 1.2 * ch3
        ch1 = add_noise(ch1, 0.06)
        ch2 = add_noise(ch2, 0.06)
        ch3 = add_noise(ch3, 0.06)

    elif label == 2:
        # 不对中：相位差改变 + 2倍频增强
        ch1 = ch1 + 0.5 * np.sin(2 * np.pi * 2.0 * t + 0.3)
        ch2 = 0.9 * np.sin(2 * np.pi * 1.5 * t + phase + 0.8) + 0.35 * np.sin(2 * np.pi * 3.0 * t)
        ch3 = ch3 + 0.4 * np.sin(2 * np.pi * 1.4 * t + 1.2)
        ch1 = add_noise(ch1, 0.06)
        ch2 = add_noise(ch2, 0.06)
        ch3 = add_noise(ch3, 0.06)

    elif label == 3:
        # 轴承故障：叠加高频冲击/尖峰
        impulses = np.zeros_like(t)
        impulse_positions = np.random.choice(len(t), size=3, replace=False)
        impulses[impulse_positions] = np.random.uniform(0.8, 1.5, size=3)

        ch1 = ch1 + 0.3 * np.sin(2 * np.pi * 5.0 * t) + impulses
        ch2 = ch2 + 0.25 * np.sin(2 * np.pi * 6.0 * t) + 0.8 * impulses
        ch3 = ch3 + 0.2 * np.sin(2 * np.pi * 7.0 * t) + 0.6 * impulses
        ch1 = add_noise(ch1, 0.08)
        ch2 = add_noise(ch2, 0.08)
        ch3 = add_noise(ch3, 0.08)

    else:
        raise ValueError("Unknown label")

    # 拼成 [96]
    x = np.concatenate([ch1, ch2, ch3]).astype(np.float32)
    return x

def build_dataset():
    xs = []
    ys = []
    for label in range(NUM_CLASSES):
        for _ in range(SAMPLES_PER_CLASS):
            xs.append(generate_sample(label))
            ys.append(label)
    x = np.array(xs, dtype=np.float32)
    y = np.array(ys, dtype=np.int32)

    # 打乱
    idx = np.random.permutation(len(x))
    x = x[idx]
    y = y[idx]
    return x, y

# =========================
# 3. 生成数据
# =========================
x, y = build_dataset()

# 训练/测试切分
split = int(0.8 * len(x))
x_train, x_test = x[:split], x[split:]
y_train, y_test = y[:split], y[split:]

# 简单归一化：按训练集整体缩放到 [-1, 1] 左右
scale = np.max(np.abs(x_train))
x_train = x_train / scale
x_test = x_test / scale

print("x_train:", x_train.shape)
print("x_test :", x_test.shape)
print("scale  :", scale)

# =========================
# 4. 定义模型
# =========================
model = tf.keras.Sequential([
    tf.keras.layers.Input(shape=(INPUT_DIM,)),
    tf.keras.layers.Dense(64, activation="relu"),
    tf.keras.layers.Dense(32, activation="relu"),
    tf.keras.layers.Dense(NUM_CLASSES, activation="softmax"),
])

model.compile(
    optimizer=tf.keras.optimizers.Adam(1e-3),
    loss="sparse_categorical_crossentropy",
    metrics=["accuracy"]
)

model.summary()
model_flops = estimate_model_flops(model, (None, INPUT_DIM), batch_size=1)
# =========================
# 5. 训练
# =========================
history = model.fit(
    x_train, y_train,
    validation_split=0.2,
    epochs=30,
    batch_size=32,
    verbose=1
)

loss, acc = model.evaluate(x_test, y_test, verbose=0)
print(f"Test accuracy: {acc:.4f}")

# =========================
# 6. 转换为 int8 TFLite
# =========================
def representative_dataset():
    for i in range(200):
        yield [x_train[i:i+1].astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset

# 全整数量化
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

with open(TFLITE_PATH, "wb") as f:
    f.write(tflite_model)

print(f"TFLite model saved to: {TFLITE_PATH}")

# =========================
# 7. 导出 model_data.h
# =========================
os.makedirs(os.path.dirname(HEADER_PATH), exist_ok=True)

with open(HEADER_PATH, "w") as f:
    f.write("#ifndef MODEL_DATA_H\n")
    f.write("#define MODEL_DATA_H\n\n")
    f.write("#include <stdint.h>\n\n")
    f.write("const unsigned char model_data[] = {\n")

    for i, b in enumerate(tflite_model):
        if i % 12 == 0:
            f.write("  ")
        f.write(f"0x{b:02x}, ")
        if i % 12 == 11:
            f.write("\n")

    f.write("\n};\n")
    f.write(f"const unsigned int model_data_len = {len(tflite_model)};\n\n")
    f.write("// 训练时使用的缩放系数，部署端保持一致\n")
    f.write(f"const float kInputScaleFromTraining = {scale:.8f}f;\n\n")
    f.write("#endif\n")

print(f"Header saved to: {HEADER_PATH}")

# =========================
# 8. 测试一下量化模型
# =========================
interpreter = tf.lite.Interpreter(model_content=tflite_model)
interpreter.allocate_tensors()

input_details = interpreter.get_input_details()
output_details = interpreter.get_output_details()

print("Input details :", input_details)
print("Output details:", output_details)

# 随机抽一个样本测试
sample = x_test[0:1].astype(np.float32)
true_label = y_test[0]

in_scale, in_zero = input_details[0]["quantization"]
out_scale, out_zero = output_details[0]["quantization"]

sample_q = np.round(sample / in_scale + in_zero).astype(np.int8)
interpreter.set_tensor(input_details[0]["index"], sample_q)
interpreter.invoke()
pred_q = interpreter.get_tensor(output_details[0]["index"])
pred = (pred_q.astype(np.float32) - out_zero) * out_scale

print("True label:", true_label)
print("Pred probs:", pred)
print("Pred class:", np.argmax(pred))