"""
video_processor.py
使用 UDPFECReceiver 库获取视频流，并用 OpenCV 显示/处理。
"""

import os
import uuid
import cv2
import numpy as np
from udp_fec_receiver import UDPFECReceiver

def main():
    # 创建接收器并启动
    receiver = UDPFECReceiver(port=5555)
    if not receiver.start():
        print("无法启动接收器")
        return
    
    print("UDP 接收器已启动，等待视频流...")
    cv2.namedWindow("Video Stream", cv2.WINDOW_AUTOSIZE)
    
    # 创建 img 目录（如果不存在）
    img_dir = os.path.join(os.path.dirname(__file__), "img")
    os.makedirs(img_dir, exist_ok=True)
    
    try:
        while True:
            # 获取一帧 JPEG 数据（超时1秒）
            jpeg_bytes = receiver.get_frame(timeout=1.0)
            if jpeg_bytes is None:
                # 无新帧，可选显示提示
                continue
            
            # 解码
            img_array = np.frombuffer(jpeg_bytes, dtype=np.uint8)
            frame = cv2.imdecode(img_array, cv2.IMREAD_COLOR)
            if frame is None:
                print("解码失败，跳过")
                continue
            
            # ---- 在这里添加你的 OpenCV 处理任务 ----
            # 例如：上下翻转、灰度化、边缘检测等
            frame = cv2.flip(frame, 0)          # 上下翻转
            
            # 显示
            cv2.imshow("Video Stream", frame)
            
            key = cv2.waitKey(1) & 0xFF
            # 按空格键拍照
            if key == ord(' '):
                filename = os.path.join(img_dir, f"{uuid.uuid4().hex}.png")
                cv2.imwrite(filename, frame)
                print(f"已拍照: {filename}")
            # 按 'q' 退出
            elif key == ord('q'):
                break
    except KeyboardInterrupt:
        print("用户中断")
    finally:
        receiver.stop()
        cv2.destroyAllWindows()
        print("已停止")

if __name__ == "__main__":
    main()