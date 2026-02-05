import serial
import time
#import sys
import json

class LaptopRelay:
    def __init__(self):
        # ESP32的串口（连接到笔记本电脑）
        self.esp32_port = 'COM3'  # Windows
        # 或 '/dev/ttyUSB0'  # Linux/Mac
        self.esp32_baud = 115200
        
        # Arduino的串口
        self.arduino_port = 'COM6'  # Windows
        # 或 '/dev/ttyUSB1'  # Linux/Mac
        self.arduino_baud = 9600
        
        self.esp32 = None
        self.arduino = None
        
    def connect(self):
        try:
            print(f"连接ESP32: {self.esp32_port}  {self.esp32_baud}")
            self.esp32 = serial.Serial(self.esp32_port, self.esp32_baud, timeout=0.1)
            time.sleep(2)
            
            print(f"连接Arduino: {self.arduino_port}  {self.arduino_baud}")
            self.arduino = serial.Serial(self.arduino_port, self.arduino_baud, timeout=0.1)
            time.sleep(2)
            
            # 发送就绪信号给ESP32
            self.esp32.write(b"READY\n")
            print("✅ 中转服务就绪")
            print("=" * 50)
            return True
            
        except Exception as e:
            print(f"连接失败: {e}")
            return False
    
    def process_esp32_command(self, line):
        """处理ESP32发送的指令"""
        line = line.strip()
        
        # 忽略空行或调试信息
        if not line or line.startswith("["):
            return None
        
        print(f"[ESP32] {line}")
        
        # 处理PING指令
        if line.startswith("PING:"):
            pong_response = f"PING:{line[5:]}\n"
            self.esp32.write(pong_response.encode())
            print("[笔记本] 发送PING响应")
            return None
        
        # 处理RAW指令（直接转发）
        if line.startswith("RAW:"):
            raw_cmd = line[4:] + "\n"
            self.arduino.write(raw_cmd.encode())
            print("[笔记本] 转发RAW指令到Arduino")
            return None
        
        # 处理智能家居指令（以CMD:开头）
        if line.startswith("CMD:"):
            json_str = line[4:]  # 移除CMD:前缀
            try:
                # 解析JSON指令
                cmd_data = json.loads(json_str)
                device = cmd_data.get("dev", "unknown")
                action = cmd_data.get("act", "unknown")
                
                print(f"📨 智能家居指令: {device} -> {action}")
                
                # 转换为Arduino指令格式（可根据需要调整）
                arduino_cmd = self.convert_to_arduino_format(cmd_data)
                
                # 发送到Arduino
                self.arduino.write((arduino_cmd + "\n").encode())
                print(f"[笔记本] 转发到Arduino: {arduino_cmd}")
                
                # 等待Arduino响应
                time.sleep(0.1)
                if self.arduino.in_waiting > 0:
                    arduino_response = self.arduino.readline().decode().strip()
                    print(f"[Arduino] {arduino_response}")
                    
                    # 将Arduino响应转发回ESP32
                    self.esp32.write((arduino_response + "\n").encode())
                    print("[笔记本] 转发响应回ESP32")
                
                return arduino_cmd
                
            except json.JSONDecodeError as e:
                print(f"JSON解析错误: {e}")
                error_msg = '{{"status":"error","message":"JSON解析失败"}}\n'
                self.esp32.write(error_msg.encode())
                return None
        
        return None
    
    def convert_to_arduino_format(self, cmd_data):
        """将ESP32的JSON指令转换为Arduino期望的JSON格式"""
        cmd_type = cmd_data.get("cmd", "control")
        device = cmd_data.get("dev", "")
        action = cmd_data.get("act", "")
        params = cmd_data.get("params", {})
        
        arduino_json = {
            "device": device
        }
        
        # 根据命令类型处理
        if cmd_type == "control":
            if action in ["on", "turn_on"]:
                arduino_json["action"] = "on"
                if "brightness" in params:
                    arduino_json["value"] = params["brightness"]
                else:
                    arduino_json["value"] = 100
            elif action in ["off", "turn_off"]:
                arduino_json["action"] = "off"
                arduino_json["value"] = 0
            elif action == "toggle":
                # toggle需要Arduino支持或转换为on/off
                arduino_json["action"] = "toggle"
        
        elif cmd_type == "query":
            arduino_json["action"] = "query"
        
        elif cmd_type == "config" and action == "set":
            if "brightness" in params:
                arduino_json["action"] = "set"
                arduino_json["value"] = params["brightness"]
        
        return json.dumps(arduino_json)
    
    
    def run(self):
        """主循环"""
        print("串口中转服务运行中...")
        print("按Ctrl+C退出")
        
        try:
            while True:
                # 读取ESP32数据
                if self.esp32 and self.esp32.in_waiting > 0:
                    try:
                        line = self.esp32.readline().decode('utf-8', errors='ignore')
                        self.process_esp32_command(line)
                    except Exception as e:
                        print(f"处理ESP32数据错误: {e}")
                
                # 读取Arduino数据（主动上报）
                if self.arduino and self.arduino.in_waiting > 0:
                    arduino_data = self.arduino.readline().decode().strip()
                    if arduino_data:
                        print(f"[Arduino主动上报] {arduino_data}")
                        # 可选：转发给ESP32
                        # self.esp32.write((arduino_data + "\n").encode())
                
                time.sleep(0.01)
                
        except KeyboardInterrupt:
            print("\n服务停止")
        finally:
            if self.esp32:
                self.esp32.close()
            if self.arduino:
                self.arduino.close()

if __name__ == "__main__":
    relay = LaptopRelay()
    if relay.connect():
        relay.run()