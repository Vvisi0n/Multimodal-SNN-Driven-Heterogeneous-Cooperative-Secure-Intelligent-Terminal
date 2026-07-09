# -*- coding: utf-8 -*-
import os
import sys
# Windows全局强制UTF8
if sys.platform == "win32":
    os.environ["PYTHONUTF8"] = "1"
    os.environ["PYTHONIOENCODING"] = "utf-8"

import tkinter as tk
from tkinter import scrolledtext, ttk
import requests
import json

# ---------------------- 配置信息（已填入你的密钥与模型） ----------------------
API_KEY = "sk-hvoeshiybspcqxlzmvzzoaywliaynruloesyewtowmpnsgub"
API_URL = "https://api.siliconflow.cn/v1/chat/completions"
MODEL_NAME = "Pro/zai-org/GLM-4.7"
SYSTEM_PROMPT = "你是一个有用的助手"

headers = {
    "Content-Type": "application/json",
    "Authorization": f"Bearer {API_KEY}"
}

class ChatGUI:
    def __init__(self, root):
        self.root = root
        self.root.title("GLM-4.7 对话客户端 | SiliconFlow")
        self.root.geometry("750x550")

        # 历史消息列表
        self.msg_history = [{"role": "system", "content": SYSTEM_PROMPT}]

        # 1. 对话显示框
        self.chat_box = scrolledtext.ScrolledText(root, wrap=tk.WORD, font=("微软雅黑", 11))
        self.chat_box.pack(fill=tk.BOTH, expand=True, padx=8, pady=(5,8))
        self.chat_box.config(state=tk.DISABLED)

        # 颜色标签配置
        self.chat_box.tag_config("user", foreground="#2E86AB")    # 用户蓝色
        self.chat_box.tag_config("assist", foreground="#D35400") # AI橙色

        # 2. 底部输入容器
        frame_bottom = ttk.Frame(root)
        frame_bottom.pack(fill=tk.X, padx=8, pady=(0,6))

        self.input_entry = tk.Text(frame_bottom, height=3, font=("微软雅黑",11))
        self.input_entry.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)
        self.input_entry.bind("<Shift-Return>", lambda e: None) # Shift+换行
        self.input_entry.bind("<Return>", self.send_msg) # 回车发送

        send_btn = ttk.Button(frame_bottom, text="发送", command=self.send_msg, width=8)
        send_btn.pack(side=tk.RIGHT, padx=(6,0))

    def append_text(self, text, tag):
        """追加文本到聊天框"""
        self.chat_box.config(state=tk.NORMAL)
        self.chat_box.insert(tk.END, text, tag)
        self.chat_box.config(state=tk.DISABLED)
        self.chat_box.see(tk.END)
        self.root.update_idletasks()

    def send_msg(self, event=None):
        user_text = self.input_entry.get("1.0", tk.END).strip()
        if not user_text:
            return
        # 清空输入框
        self.input_entry.delete("1.0", tk.END)

        # 写入用户消息
        user_show = f"\n【用户】：{user_text}\n"
        self.append_text(user_show, "user")
        self.msg_history.append({"role": "user", "content": user_text})

        # 准备请求体
        payload = {
            "model": MODEL_NAME,
            "messages": self.msg_history,
            "stream": True
        }

        # AI回复起始占位
        ai_start = "【AI】："
        self.append_text(ai_start, "assist")
        ai_full_content = ""
        buf = b""  # 字节缓存，处理TCP半包、UTF8断字

        try:
            # 流式SSE请求，禁用自动解码，原生bytes接收
            resp = requests.post(API_URL, headers=headers, json=payload, stream=True, timeout=120)
            resp.raise_for_status()

            for raw_chunk in resp.iter_content(chunk_size=64):
                if not raw_chunk:
                    continue
                buf += raw_chunk
                # 按换行切分完整行
                while b"\n" in buf:
                    line_bytes, buf = buf.split(b"\n", 1)
                    if not line_bytes.startswith(b"data: "):
                        continue
                    # 截取data:后面的数据
                    data_bytes = line_bytes[6:]
                    if data_bytes == b"[DONE]":
                        buf = b""
                        break
                    try:
                        # 强制UTF-8解码，彻底解决乱码
                        data_str = data_bytes.decode("utf-8")
                        chunk = json.loads(data_str)
                        delta = chunk["choices"][0]["delta"]
                        content = delta.get("content", "")
                        if content:
                            ai_full_content += content
                            self.append_text(content, "assist")
                    except Exception:
                        continue
            # 保存本轮AI回复到上下文
            self.msg_history.append({"role": "assistant", "content": ai_full_content})
            self.append_text("\n", "assist")

        except Exception as err:
            err_msg = f"\n接口异常：{str(err)}\n"
            self.append_text(err_msg, "assist")

if __name__ == "__main__":
    window = tk.Tk()
    app = ChatGUI(window)
    window.mainloop()