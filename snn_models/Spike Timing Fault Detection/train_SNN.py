import os
import csv
import numpy as np
import tensorflow as tf

# =========================
# 1. 参数配置
# =========================
np.random.seed(42)
tf.random.set_seed(42)

INPUT_DIM = 16           # 每个样本的手工特征维度
LABEL_NAMES = [
    "正常",
    "载重下正常",
    "重量突变",
    "正向冲撞",
    "背向冲撞",
    "卡顿",
    "超重",
]
LABEL_TO_ID = {name: idx for idx, name in enumerate(LABEL_NAMES)}
NUM_CLASSES = len(LABEL_NAMES)
TRAIN_RATIO = 0.8
SNN_STEPS = 1
LEAK_MEM = 0.5
V_THRESHOLD = 0.1
SURROGATE_ALPHA = 4.0

OUT_DIR = os.path.dirname(os.path.abspath(__file__))
DATA_CSV_PATH = os.path.abspath(
    os.path.join(OUT_DIR, "..", "data", "data", "industry_features.csv")
)
MODEL_DIR = os.path.join(OUT_DIR, ".")
TFLITE_PATH = os.path.join(MODEL_DIR, "industry_fault_snn_model.tflite")
HEADER_PATH = os.path.join(MODEL_DIR, "include", "industry_fault_snn_model_data.h")
BEST_WEIGHTS_PATH = os.path.join(MODEL_DIR, "industry_fault_snn_best.weights.h5")


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
# 2. 读取真实工业特征数据
# =========================
def parse_feature_value(cell):
    cell = cell.strip()
    if cell == "":
        return 0.0
    return float(cell)


def load_industry_dataset(csv_path):
    if not os.path.exists(csv_path):
        raise FileNotFoundError(f"未找到特征文件: {csv_path}")

    xs = []
    ys = []

    with open(csv_path, "r", encoding="utf-8-sig", newline="") as f:
        reader = csv.reader(f)
        header = next(reader)

        try:
            label_col = [name.strip() for name in header].index("label")
        except ValueError:
            label_col = len(header) - 1

        feature_cols = [i for i in range(len(header)) if i != label_col]
        if len(feature_cols) != INPUT_DIM:
            raise ValueError(
                f"CSV特征列数量为 {len(feature_cols)}，但模型输入维度配置为 {INPUT_DIM}"
            )

        for line_no, row in enumerate(reader, start=2):
            if not row or all(cell.strip() == "" for cell in row):
                continue

            if len(row) < len(header):
                row = row + [""] * (len(header) - len(row))

            label_name = row[label_col].strip()
            if label_name not in LABEL_TO_ID:
                raise ValueError(f"第 {line_no} 行存在未知类别: {label_name}")

            features = [parse_feature_value(row[i]) for i in feature_cols]
            xs.append(features)
            ys.append(LABEL_TO_ID[label_name])

    if not xs:
        raise ValueError("CSV中没有读取到任何有效样本")

    return np.array(xs, dtype=np.float32), np.array(ys, dtype=np.int32)


def stratified_train_test_split(x, y, train_ratio=TRAIN_RATIO, seed=42):
    rng = np.random.default_rng(seed)
    total_train = int(round(len(y) * train_ratio))
    class_parts = []

    for class_id in np.unique(y):
        indices = np.flatnonzero(y == class_id)
        rng.shuffle(indices)
        raw_train = len(indices) * train_ratio
        base_train = int(np.floor(raw_train))
        class_parts.append(
            {
                "class_id": class_id,
                "indices": indices,
                "base_train": base_train,
                "remainder": raw_train - base_train,
            }
        )

    remaining = total_train - sum(part["base_train"] for part in class_parts)
    class_parts.sort(key=lambda part: (-part["remainder"], part["class_id"]))

    train_indices = []
    test_indices = []
    for rank, part in enumerate(class_parts):
        indices = part["indices"]
        train_count = part["base_train"] + (1 if rank < remaining else 0)
        if len(indices) > 1:
            train_count = min(max(train_count, 1), len(indices) - 1)

        train_indices.extend(indices[:train_count])
        test_indices.extend(indices[train_count:])

    train_indices = np.array(train_indices, dtype=np.int32)
    test_indices = np.array(test_indices, dtype=np.int32)
    rng.shuffle(train_indices)
    rng.shuffle(test_indices)

    return x[train_indices], x[test_indices], y[train_indices], y[test_indices]


# =========================
# 3. 加载数据
# =========================
x, y = load_industry_dataset(DATA_CSV_PATH)
x_train, x_test, y_train, y_test = stratified_train_test_split(x, y)

# 简单归一化：按训练集整体缩放到 [-1, 1] 左右
scale = np.max(np.abs(x_train))
if scale == 0:
    scale = 1.0
x_train = x_train / scale
x_test = x_test / scale

print("data csv:", DATA_CSV_PATH)
print("labels  :", LABEL_TO_ID)
print("x_train:", x_train.shape)
print("x_test :", x_test.shape)
print("scale  :", scale)
for label_id, label_name in enumerate(LABEL_NAMES):
    train_count = int(np.sum(y_train == label_id))
    test_count = int(np.sum(y_test == label_id))
    print(f"{label_id} {label_name}: train={train_count}, test={test_count}")

# =========================
# 4. 定义模型
# =========================
def spike_with_surrogate_grad(x, alpha=SURROGATE_ALPHA):
    alpha = tf.cast(alpha, x.dtype)

    @tf.custom_gradient
    def _spike(z):
        # Avoid BOOL -> float CAST in the exported TFLite graph. Some
        # TensorFlow Lite Micro builds do not support that cast on device.
        spike = tf.clip_by_value(alpha * z + 0.5, 0.0, 1.0)

        def grad(dy):
            s = tf.sigmoid(alpha * z)
            return dy * alpha * s * (1.0 - s)

        return spike, grad

    return _spike(x)


class ForLoopSNNModel(tf.keras.Model):
    def __init__(self, num_steps, leak_mem, v_threshold, **kwargs):
        super().__init__(**kwargs)
        self.num_steps = num_steps
        self.leak_mem = leak_mem
        self.v_threshold = v_threshold

        self.fc1 = tf.keras.layers.Dense(64)
        self.fc2 = tf.keras.layers.Dense(32)
        self.fc3 = tf.keras.layers.Dense(NUM_CLASSES)

    def build(self, input_shape):
        self.fc1.build(input_shape)
        self.fc2.build((input_shape[0], 64))
        self.fc3.build((input_shape[0], 32))
        super().build(input_shape)

    def call(self, inputs, training=None):
        batch_size = tf.shape(inputs)[0]
        static_input1 = self.fc1(inputs)

        mem1 = tf.zeros_like(static_input1)
        mem2 = tf.zeros((batch_size, 32), dtype=inputs.dtype)
        out_voltage = tf.zeros((batch_size, NUM_CLASSES), dtype=inputs.dtype)

        for _ in range(self.num_steps):
            mem1 = self.leak_mem * mem1 + (1.0 - self.leak_mem) * static_input1
            spike1 = spike_with_surrogate_grad(mem1 - self.v_threshold)
            mem1 = mem1 - spike1 * self.v_threshold

            mem2 = self.leak_mem * mem2 + (1.0 - self.leak_mem) * self.fc2(spike1)
            spike2 = spike_with_surrogate_grad(mem2 - self.v_threshold)
            mem2 = mem2 - spike2 * self.v_threshold

            out_voltage = out_voltage + self.fc3(spike2)

        out_voltage = out_voltage / float(self.num_steps)
        return tf.nn.softmax(out_voltage, axis=-1)


model = ForLoopSNNModel(
    num_steps=SNN_STEPS,
    leak_mem=LEAK_MEM,
    v_threshold=V_THRESHOLD,
    name="for_loop_snn_model",
)
model.build((None, INPUT_DIM))

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
callbacks = [
    tf.keras.callbacks.ModelCheckpoint(
        BEST_WEIGHTS_PATH,
        monitor="accuracy",
        mode="max",
        save_best_only=True,
        save_weights_only=True,
        verbose=1,
    )
]

history = model.fit(
    x_train, y_train,
    epochs=5000,
    batch_size=32,
    callbacks=callbacks,
    verbose=1
)

if os.path.exists(BEST_WEIGHTS_PATH):
    model.load_weights(BEST_WEIGHTS_PATH)
    print(f"Loaded best weights from: {BEST_WEIGHTS_PATH}")

loss, acc = model.evaluate(x_test, y_test, verbose=0)
print(f"Test accuracy: {acc:.4f}")

# =========================
# 6. 转换为 int8 TFLite
# =========================
def representative_dataset():
    for i in range(min(200, len(x_train))):
        yield [x_train[i:i+1].astype(np.float32)]

converter = tf.lite.TFLiteConverter.from_keras_model(model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
converter.representative_dataset = representative_dataset

# 全整数量化
converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
converter.inference_input_type = tf.int8
converter.inference_output_type = tf.int8

tflite_model = converter.convert()

print("\n========== TFLite operators ==========")
interpreter = tf.lite.Interpreter(model_content=tflite_model)
interpreter.allocate_tensors()

ops = interpreter._get_ops_details()
for i, op in enumerate(ops):
    print(i, op["op_name"])
print("=====================================\n")

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
print("True name :", LABEL_NAMES[int(true_label)])
print("Pred probs:", pred)
pred_class = int(np.argmax(pred))
print("Pred class:", pred_class)
print("Pred name :", LABEL_NAMES[pred_class])
