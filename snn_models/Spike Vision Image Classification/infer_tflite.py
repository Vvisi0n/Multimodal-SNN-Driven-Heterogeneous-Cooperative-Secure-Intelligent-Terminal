import os
import numpy as np
import tensorflow as tf

import warnings

warnings.filterwarnings("ignore")

DATA_DIR = "./datasets_hand/test"
MODEL_PATH = "hand_det.tflite"
IMG_SIZE = (96, 96)
BATCH_SIZE = 1

rescaling = tf.keras.layers.Rescaling(1.0 / 127.5, offset=-1)


def load_test_dataset():
    test_ds = tf.keras.preprocessing.image_dataset_from_directory(
        DATA_DIR,
        validation_split=None,
        subset=None,
        seed=42,
        image_size=IMG_SIZE,
        batch_size=BATCH_SIZE,
        label_mode="int",
        shuffle=False,
    )
    class_names = test_ds.class_names
    test_ds = test_ds.map(lambda x, y: (rescaling(x), y))
    return test_ds, class_names


def run_inference():
    print(f"加载模型: {MODEL_PATH}")
    interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    print(f"输入信息: {input_details[0]}")
    print(f"输出信息: {output_details[0]}")

    input_scale, input_zero_point = input_details[0]["quantization"]
    output_scale, output_zero_point = output_details[0]["quantization"]

    test_ds, class_names = load_test_dataset()

    correct = 0
    total = 0

    print(f"\n类别: {class_names}")
    print("-" * 40)

    for images, labels in test_ds:
        for i in range(images.shape[0]):
            img = images[i]
            label = labels[i].numpy()

            if input_details[0]["dtype"] == tf.int8:
                img_float = tf.cast(img, tf.float32)
                img_int8 = tf.cast(img_float / input_scale + input_zero_point, tf.int8)
                interpreter.set_tensor(input_details[0]["index"], tf.expand_dims(img_int8, 0))
            else:
                interpreter.set_tensor(input_details[0]["index"], tf.expand_dims(img, 0))

            interpreter.invoke()
            output = interpreter.get_tensor(output_details[0]["index"])

            if output_details[0]["dtype"] == tf.int8:
                output = (output.astype(np.float32) - output_zero_point) * output_scale

            pred = np.argmax(output[0])
            conf = tf.nn.softmax(output[0]).numpy()

            if pred == label:
                correct += 1
            total += 1

    acc = correct / total if total > 0 else 0
    print(f"\n准确率 (Accuracy): {acc:.4f} ({correct}/{total})")

    return acc


def predict_single_image(image_path):
    interpreter = tf.lite.Interpreter(model_path=MODEL_PATH)
    interpreter.allocate_tensors()

    input_details = interpreter.get_input_details()
    output_details = interpreter.get_output_details()

    input_scale, input_zero_point = input_details[0]["quantization"]
    output_scale, output_zero_point = output_details[0]["quantization"]

    img = tf.keras.preprocessing.image.load_img(image_path, target_size=IMG_SIZE)
    img_array = tf.keras.preprocessing.image.img_to_array(img)
    img_array = tf.expand_dims(img_array, 0)
    img_array = rescaling(img_array)

    if input_details[0]["dtype"] == tf.int8:
        img_float = tf.cast(img_array, tf.float32)
        img_int8 = tf.cast(img_float / input_scale + input_zero_point, tf.int8)
        interpreter.set_tensor(input_details[0]["index"], img_int8)
    else:
        interpreter.set_tensor(input_details[0]["index"], img_array)

    interpreter.invoke()
    output = interpreter.get_tensor(output_details[0]["index"])

    if output_details[0]["dtype"] == tf.int8:
        output = (output.astype(np.float32) - output_zero_point) * output_scale

    pred = np.argmax(output[0])
    conf = tf.nn.softmax(output[0]).numpy()

    print(f"图片: {image_path}")
    print(f"预测: 类别 {pred}, 置信度 {conf}")

    return pred, conf


if __name__ == "__main__":
    if not os.path.exists(MODEL_PATH):
        print(f"错误: 找不到模型文件 {MODEL_PATH}")
        print("请先运行 train_tflite.py 训练模型")
        exit(1)

    if os.path.exists(DATA_DIR):
        run_inference()
    else:
        print(f"测试集目录不存在: {DATA_DIR}")
        print("请先运行 split_data.py 划分数据集")