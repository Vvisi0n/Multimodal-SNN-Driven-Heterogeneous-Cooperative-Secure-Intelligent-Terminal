import os
from PIL import Image

def batch_resize(input_folder, output_folder, size=(240, 240)):
    os.makedirs(output_folder, exist_ok=True)
    for filename in os.listdir(input_folder):
        if filename.lower().endswith(('.png', '.jpg', '.jpeg', '.bmp')):
            src_path = os.path.join(input_folder, filename)
            dst_path = os.path.join(output_folder, filename)
            try:
                with Image.open(src_path) as img:
                    img.resize(size, Image.LANCZOS).save(dst_path)
            except Exception as e:
                print(f"Failed {filename}: {e}")

if __name__ == '__main__':
    # 在当前目录下的dataset中
    batch_resize('./datasets/hand', './datasets/hand')

    print("转换完成")