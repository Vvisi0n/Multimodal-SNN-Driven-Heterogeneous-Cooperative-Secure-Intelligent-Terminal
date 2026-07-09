"""
udp_fec_receiver.py
UDP + FEC (1:4 异或冗余) 视频流接收库
封装了接收、重组、恢复 JPEG 帧的功能。
"""

import socket
import struct
import time
import threading
import queue
from typing import Optional, Tuple

# ============ 参数默认值 ============
DEFAULT_UDP_PORT = 5555
HEADER_SIZE = 8              # frame_id(2) + chunk_index(2) + total_chunks(2) + jpeg_len(2)
CHUNK_DATA_SIZE = 1024
DATA_PER_GROUP = 4
REDUN_FLAG = 0xFFFF
FRAME_TIMEOUT = 2.0
QUEUE_SIZE = 8

class UDPFECReceiver:
    """UDP + FEC 视频流接收器"""
    def __init__(self, port: int = DEFAULT_UDP_PORT,
                 frame_timeout: float = FRAME_TIMEOUT,
                 queue_size: int = QUEUE_SIZE):
        """
        初始化接收器
        :param port: 监听的 UDP 端口
        :param frame_timeout: 单帧超时丢弃时间（秒）
        :param queue_size: 内部已解码帧队列大小
        """
        self.port = port
        self.frame_timeout = frame_timeout
        self.queue_size = queue_size
        
        self.sock: Optional[socket.socket] = None
        self.running = False
        self.recv_thread: Optional[threading.Thread] = None
        
        # 内部状态
        self.assembly = {}          # frame_id -> 帧组装信息
        self.assembly_lock = threading.Lock()
        self.frame_queue = queue.Queue(maxsize=queue_size)
    
    def _xor_blocks(self, *blocks) -> bytes:
        """逐字节异或"""
        length = len(blocks[0])
        result = bytearray(length)
        for b in blocks:
            for i in range(length):
                result[i] ^= b[i]
        return bytes(result)
    
    def _recover_missing_chunk(self, present_chunks: dict, redundant_block: bytes) -> dict:
        """缺失一个块时用冗余恢复"""
        expected_indices = set(range(DATA_PER_GROUP))
        missing_idx = (expected_indices - set(present_chunks.keys())).pop()
        recovered = self._xor_blocks(redundant_block, *present_chunks.values())
        return {missing_idx: recovered}
    
    def _recv_loop(self):
        """接收线程的主循环"""
        while self.running:
            try:
                data, _ = self.sock.recvfrom(65535)
            except socket.timeout:
                continue
            except OSError:
                break
            
            if len(data) < HEADER_SIZE:
                continue
            
            # 解包头
            frame_id, chunk_index, total_chunks, jpeg_len = struct.unpack('!HHHH', data[:HEADER_SIZE])
            payload = data[HEADER_SIZE:]
            now = time.time()
            
            with self.assembly_lock:
                # 超时清理
                stale = [fid for fid, info in self.assembly.items()
                         if now - info['last_time'] > self.frame_timeout]
                for fid in stale:
                    del self.assembly[fid]
                
                # 初始化或更新帧信息
                if frame_id not in self.assembly:
                    self.assembly[frame_id] = {
                        'total_chunks': total_chunks,
                        'jpeg_len': jpeg_len,
                        'chunks': {},
                        'redundant': {},
                        'last_time': now,
                        'complete_groups': set(),
                        'total_groups': (total_chunks + DATA_PER_GROUP - 1) // DATA_PER_GROUP
                    }
                else:
                    self.assembly[frame_id]['last_time'] = now
                
                fid_data = self.assembly[frame_id]
                
                # 处理冗余包
                if chunk_index == REDUN_FLAG:
                    if len(payload) < 2 + CHUNK_DATA_SIZE:
                        continue
                    group_id = struct.unpack('!H', payload[:2])[0]
                    redundant_block = payload[2:2 + CHUNK_DATA_SIZE]
                    fid_data['redundant'][group_id] = redundant_block
                else:
                    # 数据包
                    if len(payload) != CHUNK_DATA_SIZE:
                        continue
                    fid_data['chunks'][chunk_index] = payload
                
                # 尝试恢复每组
                total_groups = fid_data['total_groups']
                for gid in range(total_groups):
                    if gid in fid_data['complete_groups']:
                        continue
                    
                    start_idx = gid * DATA_PER_GROUP
                    end_idx = min(start_idx + DATA_PER_GROUP, total_chunks)
                    
                    present = {}
                    missing = 0
                    for idx in range(start_idx, end_idx):
                        if idx in fid_data['chunks']:
                            present[idx - start_idx] = fid_data['chunks'][idx]
                        else:
                            missing += 1
                    
                    if missing == 0:
                        fid_data['complete_groups'].add(gid)
                    elif missing == 1 and gid in fid_data['redundant']:
                        redundant = fid_data['redundant'][gid]
                        recovered = self._recover_missing_chunk(present, redundant)
                        for inner_idx, recovered_data in recovered.items():
                            global_idx = start_idx + inner_idx
                            fid_data['chunks'][global_idx] = recovered_data
                        fid_data['complete_groups'].add(gid)
                
                # 检查整帧是否完整
                if len(fid_data['complete_groups']) == fid_data['total_groups']:
                    sorted_indices = sorted(fid_data['chunks'].keys())
                    jpeg_padded = b''.join(fid_data['chunks'][i] for i in sorted_indices)
                    jpeg_data = jpeg_padded[:fid_data['jpeg_len']]
                    
                    try:
                        self.frame_queue.put_nowait(jpeg_data)
                    except queue.Full:
                        pass  # 丢弃旧帧，避免阻塞
                    del self.assembly[frame_id]
    
    def start(self) -> bool:
        """启动接收器，返回是否成功"""
        if self.running:
            return False
        
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        try:
            self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 256 * 1024)
        except:
            pass
        self.sock.bind(('0.0.0.0', self.port))
        self.sock.settimeout(0.5)
        
        self.running = True
        self.recv_thread = threading.Thread(target=self._recv_loop, daemon=True)
        self.recv_thread.start()
        return True
    
    def get_frame(self, timeout: float = 0.5) -> Optional[bytes]:
        """
        获取一帧 JPEG 字节数据
        :param timeout: 等待超时（秒）
        :return: JPEG 字节串，若无数据返回 None
        """
        try:
            return self.frame_queue.get(timeout=timeout)
        except queue.Empty:
            return None
    
    def stop(self):
        """停止接收器，释放资源"""
        self.running = False
        if self.sock:
            self.sock.close()
        if self.recv_thread and self.recv_thread.is_alive():
            self.recv_thread.join(timeout=1.0)
        # 清空队列
        while not self.frame_queue.empty():
            try:
                self.frame_queue.get_nowait()
            except queue.Empty:
                break