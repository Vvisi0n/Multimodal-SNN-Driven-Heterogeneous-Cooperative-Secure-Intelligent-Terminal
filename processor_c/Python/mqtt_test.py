import tkinter as tk
from tkinter import ttk, messagebox
import paho.mqtt.client as mqtt
from datetime import datetime

# ==================== MQTT 基础配置 ====================
MQTT_HOST = "4d291f35b96f49e0aaa91e929f888eb5.s1.eu.hivemq.cloud"
MQTT_PORT = 8883
MQTT_USER = "WGT_QIANSAI"
MQTT_PASS = "Qq112211!"
DEFAULT_TOPIC = "ceshi_mqtt"
# =====================================================

class MqttGuiApp:
    def __init__(self, root):
        self.root = root
        self.root.title("ESP32 MQTT 原始数据接收与发送终端")
        self.root.geometry("750x600")
        
        # 当前订阅的主题变量
        self.current_topic = DEFAULT_TOPIC
        
        # 初始化 MQTT 客户端
        self.client = mqtt.Client()
        self.client.username_pw_set(MQTT_USER, MQTT_PASS)
        self.client.tls_set()  # 必须开启 TLS 加密
        
        # 绑定回调
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        
        # 建立 UI 界面
        self.setup_ui()
        
        # 连接服务器并启动后台循环
        self.log_to_ui("系统消息: 正在连接 HiveMQ 远程服务器...")
        try:
            self.client.connect(MQTT_HOST, MQTT_PORT, 60)
            self.client.loop_start()  # 后台非阻塞循环
        except Exception as e:
            self.log_to_ui(f"系统错误: 无法连接服务器: {e}")
            messagebox.showerror("连接错误", f"连接失败: {e}")

    def setup_ui(self):
        """构建 Tkinter 界面布局"""
        # ---- 顶部状态与主题控制栏 ----
        control_frame = ttk.LabelFrame(self.root, text=" 远程连接与主题控制 ", padding=10)
        control_frame.pack(fill="x", padx=10, pady=10)
        
        # 状态标签
        self.status_label = ttk.Label(control_frame, text="🔴 未连接", foreground="red", font=("Helvetica", 10, "bold"))
        self.status_label.grid(row=0, column=0, padx=5, pady=5, sticky="w")
        
        # 主题输入框
        ttk.Label(control_frame, text="订阅主题:").grid(row=0, column=1, padx=5, pady=5)
        self.topic_entry = ttk.Entry(control_frame, width=35)
        self.topic_entry.insert(0, self.current_topic)
        self.topic_entry.grid(row=0, column=2, padx=5, pady=5)
        
        # 订阅/切换按钮
        self.sub_button = ttk.Button(control_frame, text="切换并订阅", command=self.change_topic)
        self.sub_button.grid(row=0, column=3, padx=5, pady=5)
        
        # ---- 发送区域 ----
        send_frame = ttk.LabelFrame(self.root, text=" 发送消息 (JSON/文本/二进制) ", padding=10)
        send_frame.pack(fill="x", padx=10, pady=5)
        
        # 发送主题输入
        ttk.Label(send_frame, text="发送主题:").grid(row=0, column=0, padx=5, pady=5)
        self.send_topic_entry = ttk.Entry(send_frame, width=20)
        self.send_topic_entry.insert(0, self.current_topic)
        self.send_topic_entry.grid(row=0, column=1, padx=5, pady=5)
        
        # QoS 选择
        ttk.Label(send_frame, text="QoS:").grid(row=0, column=2, padx=5, pady=5)
        self.qos_var = tk.IntVar(value=0)
        qos_combo = ttk.Combobox(send_frame, textvariable=self.qos_var, values=[0, 1, 2], width=3, state="readonly")
        qos_combo.grid(row=0, column=3, padx=5, pady=5)
        
        # 消息输入框
        self.send_entry = ttk.Entry(send_frame, width=50)
        self.send_entry.grid(row=1, column=0, columnspan=3, padx=5, pady=5, sticky="ew")
        self.send_entry.bind("<Return>", lambda event: self.send_message())  # 回车发送
        
        # 发送按钮
        self.send_button = ttk.Button(send_frame, text="发送", command=self.send_message)
        self.send_button.grid(row=1, column=3, padx=5, pady=5)
        
        # ---- 中部数据显示区域 ----
        display_frame = ttk.LabelFrame(self.root, text=" 接收原始消息数据 (格式: [时间] [主题] -> 原始数据) ", padding=10)
        display_frame.pack(fill="both", expand=True, padx=10, pady=5)
        
        # 滚动文本框（用于显示接收到的内容）
        self.scrollbar = ttk.Scrollbar(display_frame)
        self.scrollbar.pack(side="right", fill="y")
        
        self.log_listbox = tk.Listbox(
            display_frame, 
            font=("Consolas", 10), 
            yscrollcommand=self.scrollbar.set,
            bg="#f4f4f5",
            selectbackground="#2563eb"
        )
        self.log_listbox.pack(fill="both", expand=True, side="left")
        self.scrollbar.config(command=self.log_listbox.yview)
        
        # ---- 底部清空按钮 ----
        clear_button = ttk.Button(self.root, text="清空日志", command=self.clear_logs)
        clear_button.pack(pady=10)
        
        # 窗口关闭事件
        self.root.protocol("WM_DELETE_WINDOW", self.on_closing)

    # ==================== MQTT 异步回调函数 ====================
    def on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            self.status_label.config(text="🟢 已连接至 HiveMQ", foreground="green")
            self.log_to_ui("系统消息: 成功连接到 HiveMQ 服务器！")
            
            # 首次连接，自动订阅默认主题
            self.client.subscribe(self.current_topic)
            self.log_to_ui(f"系统消息: 已订阅主题: [{self.current_topic}]，正在等待原始数据...")
        else:
            self.status_label.config(text="❌ 连接失败", foreground="red")
            self.log_to_ui(f"系统消息: 连接失败，错误码: {rc}")

    def on_message(self, client, userdata, msg):
        try:
            # 💡 核心修改：直接解码为字符串，不加任何数据转换或单位修饰
            raw_payload = msg.payload.decode('utf-8')
            time_str = datetime.now().strftime("%H:%M:%S")
            
            # 仅加上时间戳和主题前缀做区分，后面紧跟原封不动的内容
            display_text = f"[{time_str}] [接收] [{msg.topic}] -> {raw_payload}"
                
            self.log_to_ui(display_text)
        except Exception as e:
            self.log_to_ui(f"数据解析错误: {e}")

    # ==================== UI 交互业务逻辑 ====================
    def send_message(self):
        """发送消息到 MQTT 主题"""
        topic = self.send_topic_entry.get().strip()
        message = self.send_entry.get().strip()
        
        if not topic:
            messagebox.showwarning("提示", "发送主题不能为空！")
            return
        
        if not message:
            messagebox.showwarning("提示", "消息内容不能为空！")
            return
        
        try:
            qos = self.qos_var.get()
            self.client.publish(topic, message, qos=qos)
            time_str = datetime.now().strftime("%H:%M:%S")
            self.log_to_ui(f"[{time_str}] [发送] [{topic}] -> {message}")
            self.send_entry.delete(0, tk.END)  # 清空输入框
        except Exception as e:
            self.log_to_ui(f"发送失败: {e}")
            messagebox.showerror("发送错误", f"消息发送失败: {e}")

    def change_topic(self):
        """切换订阅主题的逻辑"""
        new_topic = self.topic_entry.get().strip()
        if not new_topic:
            messagebox.showwarning("提示", "主题不能为空！")
            return
            
        # 退订旧主题，订阅新主题
        self.client.unsubscribe(self.current_topic)
        self.log_to_ui(f"系统消息: 已退订旧主题: [{self.current_topic}]")
        
        self.current_topic = new_topic
        self.client.subscribe(self.current_topic)
        self.log_to_ui(f"系统消息: 已成功切换并订阅新主题: [{self.current_topic}]")

    def log_to_ui(self, message):
        """向界面的 Listbox 安全插入一行日志，并自动滚动到底部"""
        self.root.after(0, self._safe_log, message)

    def _safe_log(self, message):
        self.log_listbox.insert(tk.END, message)
        self.log_listbox.yview(tk.END)  # 自动滚动到最新一行

    def clear_logs(self):
        """清空日志框"""
        self.log_listbox.delete(0, tk.END)

    def on_closing(self):
        """关闭窗口时优雅地退出进程"""
        self.client.loop_stop()
        self.client.disconnect()
        self.root.destroy()

# ==================== 主程序入口 ====================
if __name__ == "__main__":
    window = tk.Tk()
    app = MqttGuiApp(window)
    window.mainloop()