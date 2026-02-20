// Arduino UNO - 智能家居执行器 + 倒计时功能
#include <ArduinoJson.h>

// ================== 新增：倒计时相关定义 ==================
#define BUZZER_PIN 9          // 蜂鸣器连接引脚（请根据实际接线修改）
#define MAX_TIMERS 5          // 最多同时运行5个倒计时

struct Timer {
    unsigned long endTime;    // 目标时刻（millis()值）
    bool active;              // 是否启用
    int duration;             // 原始秒数（用于日志）
};

Timer timers[MAX_TIMERS];     // 倒计时数组
// =========================================================

// 设备引脚映射（保持不变）
struct Device {
    String name;
    int pin;
    bool isPWM;
    int currentState;      // 当前状态：0=关，1=开（开关设备）或亮度值0-100（PWM设备）
    int lastBrightness;    // 上次的亮度值（用于toggle时恢复）
};

Device devices[] = {
    {"living_room_light", 2, true, 0, 50},
    {"bedroom_light", 3, true, 0, 50},
    {"kitchen_light", 4, false, 0, 0},     // 改为开关
    {"fan", 9, true, 0, 50},
    {"ac", 10, false, 0, 0},               // 改为"ac"
    {"tv", 11, false, 0, 0},
    {"outlet_1", 12, false, 0, 0},
    {"outlet_2", 13, false, 0, 0}
};

void setup() {
    Serial.begin(9600);  // 与笔记本电脑通信

    // ================== 新增：初始化蜂鸣器引脚 ==================
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);  // 初始不响
    // =========================================================

    // 初始化所有设备引脚
    for (int i = 0; i < sizeof(devices)/sizeof(devices[0]); i++) {
        if (devices[i].isPWM) {
            pinMode(devices[i].pin, OUTPUT);
            analogWrite(devices[i].pin, 0);  // 初始关闭
        } else {
            pinMode(devices[i].pin, OUTPUT);
            digitalWrite(devices[i].pin, LOW);
        }
    }

    Serial.println("🤖 Arduino智能家居控制器就绪（带倒计时功能）");
    Serial.println("等待ESP32指令...");
}

void loop() {
    // 监听串口指令
    if (Serial.available() > 0) {
        String cmdLine = Serial.readStringUntil('\n');
        cmdLine.trim();

        if (cmdLine.length() > 0) {
            Serial.print("收到指令: ");
            Serial.println(cmdLine);

            // ================== 新增：处理倒计时指令 ==================
            if (cmdLine.startsWith("TIMER")) {
                int seconds = cmdLine.substring(5).toInt();
                if (seconds > 0 && seconds < 86400) { // 不超过24小时
                    addTimer(seconds);
                    Serial.println("{\"status\":\"success\",\"message\":\"Timer started\"}");
                } else {
                    Serial.println("{\"status\":\"error\",\"message\":\"Invalid seconds\"}");
                }
            } else {
                // 原有的JSON指令处理
                String result = executeCommand(cmdLine);
                Serial.println(result);
            }
            // =========================================================
        }
    }

    // ================== 新增：检查倒计时 ==================
    checkTimers();
    // =====================================================

    // 可以在这里添加传感器读取（如温度、亮度）
    static unsigned long lastReport = 0;
    if (millis() - lastReport > 10000) {  // 每10秒上报一次
        lastReport = millis();
        // reportSensorData();
    }
}

// ================== 新增：倒计时管理函数 ==================
void addTimer(int seconds) {
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (!timers[i].active) {
            timers[i].endTime = millis() + (seconds * 1000UL);
            timers[i].active = true;
            timers[i].duration = seconds;
            Serial.print("倒计时已添加: ");
            Serial.print(seconds);
            Serial.println(" 秒后触发");
            return;
        }
    }
    Serial.println("错误：倒计时槽位已满，无法添加新倒计时");
}

void checkTimers() {
    unsigned long now = millis();
    for (int i = 0; i < MAX_TIMERS; i++) {
        if (timers[i].active && now >= timers[i].endTime) {
            // 触发蜂鸣器（响2秒，频率2000Hz）
            tone(BUZZER_PIN, 2000, 2000);
            Serial.print("倒计时结束: ");
            Serial.print(timers[i].duration);
            Serial.println(" 秒");
            timers[i].active = false; // 禁用该计时器
        }
    }
}

String executeCommand(const String& jsonStr) {
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) {
        return "{\"status\":\"error\",\"reason\":\"JSON解析失败\"}";
    }
    
    String device = doc["device"] | "";
    String action = doc["action"] | "";
    int value = doc["value"] | 0;
    
    // 查找设备
    Device* targetDevice = NULL;
    for (int i = 0; i < sizeof(devices)/sizeof(devices[0]); i++) {
        if (devices[i].name == device) {
            targetDevice = &devices[i];
            break;
        }
    }
    
    if (!targetDevice) {
        return "{\"status\":\"error\",\"reason\":\"设备未找到\"}";
    }
    
    // 执行操作
    if (action == "on" || action == "turn_on") {
        if (targetDevice->isPWM) {
            int brightness = (value > 0) ? value : targetDevice->lastBrightness;
            if (brightness == 0) brightness = 50; // 默认50%亮度
            analogWrite(targetDevice->pin, map(brightness, 0, 100, 0, 255));
            targetDevice->currentState = brightness;
            targetDevice->lastBrightness = brightness;
            return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                   "\",\"action\":\"on\",\"brightness\":" + String(brightness) + "}";
        } else {
            digitalWrite(targetDevice->pin, HIGH);
            targetDevice->currentState = 1;
            return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                   "\",\"action\":\"on\"}";
        }
    }
    else if (action == "off" || action == "turn_off") {
        if (targetDevice->isPWM) {
            analogWrite(targetDevice->pin, 0);
            // 保存当前亮度以便下次打开时恢复
            targetDevice->lastBrightness = targetDevice->currentState;
            targetDevice->currentState = 0;
        } else {
            digitalWrite(targetDevice->pin, LOW);
            targetDevice->currentState = 0;
        }
        return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
               "\",\"action\":\"off\"}";
    }
    else if (action == "set") {
        if (targetDevice->isPWM && value >= 0 && value <= 100) {
            analogWrite(targetDevice->pin, map(value, 0, 100, 0, 255));
            targetDevice->currentState = value;
            targetDevice->lastBrightness = value;
            return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                   "\",\"brightness\":" + String(value) + "}";
        }
    }
    else if (action == "query") {
        return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
               "\",\"state\":" + String(targetDevice->currentState) + 
               ",\"isPWM\":" + String(targetDevice->isPWM ? "true" : "false") + "}";
    }
    else if (action == "toggle") {
        // 🔄 TOGGLE功能（预留）
        // 根据当前状态切换
        if (targetDevice->isPWM) {
            if (targetDevice->currentState == 0) {
                // 如果当前是关闭状态，则打开到上次亮度或默认亮度
                int brightness = targetDevice->lastBrightness > 0 ? targetDevice->lastBrightness : 50;
                analogWrite(targetDevice->pin, map(brightness, 0, 100, 0, 255));
                targetDevice->currentState = brightness;
                return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                       "\",\"action\":\"toggle\",\"new_state\":\"on\",\"brightness\":" + String(brightness) + "}";
            } else {
                // 如果当前是打开状态，则关闭
                targetDevice->lastBrightness = targetDevice->currentState;
                analogWrite(targetDevice->pin, 0);
                targetDevice->currentState = 0;
                return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                       "\",\"action\":\"toggle\",\"new_state\":\"off\"}";
            }
        } else {
            // 开关设备
            if (targetDevice->currentState == 0) {
                digitalWrite(targetDevice->pin, HIGH);
                targetDevice->currentState = 1;
                return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                       "\",\"action\":\"toggle\",\"new_state\":\"on\"}";
            } else {
                digitalWrite(targetDevice->pin, LOW);
                targetDevice->currentState = 0;
                return "{\"status\":\"success\",\"device\":\"" + targetDevice->name + 
                       "\",\"action\":\"toggle\",\"new_state\":\"off\"}";
            }
        }
    }
    
    return "{\"status\":\"error\",\"reason\":\"不支持的操作:" + action + "\"}";
}