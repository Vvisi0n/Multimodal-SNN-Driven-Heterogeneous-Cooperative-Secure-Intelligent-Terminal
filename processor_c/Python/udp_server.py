import socket
import threading
import tkinter as tk
from tkinter import messagebox, ttk

# ==================== UDP 配置 ====================
UDP_PORT = 8888
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

try:
    sock.bind(("0.0.0.0", UDP_PORT))
except Exception as e:
    messagebox.showerror("错误", f"端口绑定失败：{str(e)}")
    exit()

# 用于存储已连接设备的字典
# 结构: { "192.168.137.77:8888": {"name": "dev_1", "addr": ("192.168.137.77", 8888)} }
devices = {}
device_counter = 0  # 设备自动计数器

# ==================== TK 界面构建 ====================
window = tk.Tk()
window.title("UDP 多设备管理主机")
window.geometry("650x400")  # 加宽界面以容纳设备列表

# --- 样式调整 ---
style = ttk.Style()
style.theme_use("clam")

# --- 左右分栏 ---
main_frame = ttk.Frame(window, padding=10)
main_frame.pack(fill=tk.BOTH, expand=True)

# 左侧：日志区
left_frame = ttk.Frame(main_frame)
left_frame.pack(side=tk.LEFT, fill=tk.BOTH, expand=True, padx=(0, 5))

ttk.Label(left_frame, text="通信日志", font=("Helvetica", 10, "bold")).pack(anchor=tk.W)
msg_list = tk.Listbox(left_frame, bg="#f5f5f5", selectbackground="#cce6ff")
msg_list.pack(fill=tk.BOTH, expand=True, pady=5)

# 右侧：设备列表区
right_frame = ttk.Frame(main_frame, width=200)
right_frame.pack(side=tk.RIGHT, fill=tk.Y, padx=(5, 0))
right_frame.pack_propagate(False) # 固定宽度

ttk.Label(right_frame, text="设备列表 (目标选择)", font=("Helvetica", 10, "bold")).pack(anchor=tk.W)

# 设备选择列表框（支持单选）
dev_box = tk.Listbox(right_frame, bg="#fafafa", exportselection=False)
dev_box.pack(fill=tk.BOTH, expand=True, pady=5)
# 默认添加一个“所有人（广播）”的选项
dev_box.insert(tk.END, "📢 所有人 (广播)")
dev_box.select_set(0) # 默认选中广播

# --- 底部：发送控制区 ---
bottom_frame = ttk.Frame(window, padding=10)
bottom_frame.pack(fill=tk.X, side=tk.BOTTOM)

entry = ttk.Entry(bottom_frame, font=("Helvetica", 11))
entry.pack(side=tk.LEFT, fill=tk.X, expand=True, padx=(0, 10))

# ==================== 核心逻辑函数 ====================

def add_log(msg):
    """向日志框添加一条记录"""
    try:
        msg_list.insert(tk.END, msg)
        msg_list.see(tk.END)
    except:
        pass

def update_device_listbox():
    """刷新右侧的设备 UI 列表"""
    # 记住当前选中的索引，防止刷新时重置选择
    current_select = dev_box.curselection()
    
    dev_box.delete(0, tk.END)
    dev_box.insert(tk.END, "📢 所有人 (广播)")
    
    # 重新填充所有在线设备
    for dev_id, info in devices.items():
        dev_box.insert(tk.END, f"📱 {info['name']} ({dev_id.split(':')[0]})")
    
    # 恢复之前的选中状态
    if current_select:
        dev_box.select_set(current_select[0])
    else:
        dev_box.select_set(0)

def send_msg():
    """发送消息逻辑"""
    msg = entry.get().strip()
    if not msg:
        return
        
    # 获取当前选中的目标
    selected_indices = dev_box.curselection()
    if not selected_indices:
        add_log("⚠️ 请先在右侧选择发送目标！")
        return
        
    idx = selected_indices[0]
    
    try:
        if idx == 0:
            # 广播发送
            sock.sendto(msg.encode(), ("255.255.255.255", UDP_PORT))
            add_log(f"【群发广播】: {msg}")
        else:
            # 私发给指定设备
            # 根据索引获取设备字典里的 key
            dev_keys = list(devices.keys())
            target_key = dev_keys[idx - 1] # 减去第0项的广播
            target_info = devices[target_key]
            
            sock.sendto(msg.encode(), target_info["addr"])
            add_log(f"【私发给 {target_info['name']}】: {msg}")
            
        entry.delete(0, tk.END)
    except Exception as e:
        add_log(f"❌ 发送失败: {str(e)}")

# 绑定回车键发送消息
entry.bind("<Return>", lambda event: send_msg())

send_btn = ttk.Button(bottom_frame, text="发送消息", command=send_msg)
send_btn.pack(side=tk.RIGHT)

# ==================== 接收线程逻辑 ====================
def recv_thread():
    global device_counter
    while True:
        try:
            data, addr = sock.recvfrom(1024)
            ip, port = addr
            dev_key = f"{ip}:{port}" # 用 IP+端口 作为唯一标识
            
            # 检查是否是新设备
            is_new = False
            if dev_key not in devices:
                device_counter += 1
                dev_name = f"dev_{device_counter}"
                devices[dev_key] = {
                    "name": dev_name,
                    "addr": addr
                }
                is_new = True
                
            # 解析消息
            try:
                msg = data.decode("utf-8").strip()
            except:
                msg = f"[二进制数据] {len(data)}字节"
            
            # UI 更新必须在主线程进行，使用 after 安全调用
            if is_new:
                window.after(0, add_log, f"✨ 新设备加入组! 命名为: {dev_name} ({ip})")
                window.after(0, update_device_listbox)
                
            window.after(0, add_log, f"[{devices[dev_key]['name']}]: {msg}")
            
        except Exception:
            continue

# 启动接收线程
threading.Thread(target=recv_thread, daemon=True).start()

# --- 初始化提示 ---
add_log("🚀 UDP 多设备管理系统已启动")
add_log(f"📡 正在监听端口: {UDP_PORT} (支持255.255.255.255广播)")
add_log("💡 提示: 当有 ESP32 发送任意数据过来时，系统会自动记录并为它编号。")

window.mainloop()