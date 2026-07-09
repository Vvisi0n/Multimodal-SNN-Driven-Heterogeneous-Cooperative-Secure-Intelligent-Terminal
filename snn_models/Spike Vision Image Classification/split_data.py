import os
import random
import numpy as np
from PIL import Image


TARGET_SIZE = (96, 96)


def bilinear_resize(img_array, target_size):
    src_h, src_w = img_array.shape[:2]
    dst_h, dst_w = target_size

    y_src = np.arange(dst_h, dtype=np.float32) * (src_h - 1) / max(dst_h - 1, 1)
    x_src = np.arange(dst_w, dtype=np.float32) * (src_w - 1) / max(dst_w - 1, 1)

    y0 = np.floor(y_src).astype(np.int32)
    x0 = np.floor(x_src).astype(np.int32)
    y1 = np.minimum(y0 + 1, src_h - 1)
    x1 = np.minimum(x0 + 1, src_w - 1)

    wy = (y_src - y0).reshape(dst_h, 1, 1).astype(np.float32)
    wx = (x_src - x0).reshape(1, dst_w, 1).astype(np.float32)

    p00 = img_array[y0[:, None], x0[None, :], :].astype(np.float32)
    p01 = img_array[y0[:, None], x1[None, :], :].astype(np.float32)
    p10 = img_array[y1[:, None], x0[None, :], :].astype(np.float32)
    p11 = img_array[y1[:, None], x1[None, :], :].astype(np.float32)

    top = p00 * (1 - wx) + p01 * wx
    bottom = p10 * (1 - wx) + p11 * wx
    dst = (top * (1 - wy) + bottom * wy).astype(img_array.dtype)

    return dst


def split_dataset(src_dir, target_dir, split_ratio=(0.8, 0.1, 0.1), seed=42):

    assert sum(split_ratio) == 1.0, "划分比例总和必须为 1.0"

    random.seed(seed)

    classes = [d for d in os.listdir(src_dir) if os.path.isdir(os.path.join(src_dir, d))]

    splits = ['train', 'val', 'test']
    for split in splits:
        for cls in classes:
            os.makedirs(os.path.join(target_dir, split, cls), exist_ok=True)

    print(f"开始划分数据集，目标路径: {target_dir}")
    print(f"统一缩放至: {TARGET_SIZE[0]}x{TARGET_SIZE[1]} (双线性插值, 与ESP32一致)")
    print("-" * 30)

    for cls in classes:
        cls_dir = os.path.join(src_dir, cls)
        images = [f for f in os.listdir(cls_dir) if f.lower().endswith(('.png', '.jpg', '.jpeg', '.bmp'))]

        random.shuffle(images)

        total_count = len(images)
        train_point = int(total_count * split_ratio[0])
        val_point = int(total_count * (split_ratio[0] + split_ratio[1]))

        train_imgs = images[:train_point]
        val_imgs = images[train_point:val_point]
        test_imgs = images[val_point:]

        def copy_and_resize_files(file_list, split_name):
            for img in file_list:
                src_path = os.path.join(cls_dir, img)
                pil_img = Image.open(src_path).convert("RGB")
                img_array = np.array(pil_img)

                if img_array.shape[:2] != TARGET_SIZE:
                    img_array = bilinear_resize(img_array, TARGET_SIZE)

                dest_path = os.path.join(target_dir, split_name, cls, img)
                Image.fromarray(img_array).save(dest_path)

        copy_and_resize_files(train_imgs, 'train')
        copy_and_resize_files(val_imgs, 'val')
        copy_and_resize_files(test_imgs, 'test')

        print(
            f"类别 [{cls}]: 总计 {total_count} 张 | 训练集 {len(train_imgs)} 张 | 验证集 {len(val_imgs)} 张 | 测试集 {len(test_imgs)} 张")


if __name__ == "__main__":
    SOURCE_DIRECTORY = "./datasets/hand"
    TARGET_DIRECTORY = "./datasets_hand"

    split_dataset(src_dir=SOURCE_DIRECTORY, target_dir=TARGET_DIRECTORY, split_ratio=(0.6, 0.2, 0.2))
    print("\n数据集划分完成！")