#include <Arduino.h>
#include "base64.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include <ESP_I2S.h>
#include <ArduinoJson.h>
#include <UrlEncode.h>
#include <esp32-hal-psram.h>

const uint8_t key = 3;    // Push key
const uint8_t I2S_LRC = 4;    //ESP32 speaker pins setup
const uint8_t I2S_BCLK = 5;
const uint8_t I2S_DOUT = 6;  
const uint8_t key_smart = 7;   //切换智能家居助手的按键
const uint8_t I2S_DIN = 41;   //ESP32 MIC pins setup
const uint8_t I2S_SCK = 42; 

// WIFI. Please change to your wifi account name and password
const char *ssid = "Your Wifi account name";
const char *password = "Your Wifi password";

// BAIDUYUN API of STT and TTS
const int DEV_PID = 1537;  // Chinese
const char *CUID = "121971725";
const char *CLIENT_ID = "nQ0MLl3qe72S9t1uH2js83eM";
const char *CLIENT_SECRET = "9SNuPMOxqmDPn61Pw4g1g5CYUFTDDomP";
const char* baidutts_url = "https://tsn.baidu.com/text2audio";
String url = baidutts_url;

// TONGYIQIANWEN API for LLM
const char* apikey = "sk-245322e11fda4b8a9719e7c535fc290e";
String llm_inputText = "Hello, TYQW!";
String apiUrl = "https://dashscope.aliyuncs.com/api/v1/services/aigc/text-generation/generation";
String apiUrl_smart = "";

String token;

I2SClass I2S;

// Audio DAC parameters
i2s_mode_t PDM_RX_mode = I2S_MODE_PDM_RX;
const int sampleRate = 16000;  // sample rate in Hz
i2s_data_bit_width_t bps = I2S_DATA_BIT_WIDTH_16BIT;
i2s_slot_mode_t slot = I2S_SLOT_MODE_MONO;

// MIC recording buffer
static const uint32_t sample_buffer_size = 320000;
signed short* sampleBuffer = NULL;

// STT JSON
char *data_json;
const int recordTimesSeconds = 10;
const int adc_data_len = 16000 * recordTimesSeconds;
const int data_json_len = adc_data_len * 2 * 1.4;

static uint8_t start_record = 0;
static uint8_t record_complete = 0;

// HTTP audio file buffer
const int AUDIO_FILE_BUFFER_SIZE  = 600000;
char* audio_file_buffer;

const int Timeout = 10000;
uint32_t audio_index = 0;
HTTPClient http_tts;

// BAIDU TTS
void get_voice_answer(String llm_answer) {
    String encodedText;
    
    encodedText = urlEncode(urlEncode(llm_answer));
    
    const char *headerKeys[] = {"Content-type", "Content-Length"};

    url = baidutts_url;
    url += "?tex=" + encodedText;
    url += "&lan=zh";
    url += "&cuid=121971725";
    url += "&ctp=1";
    url += "&aue=3";
    url += "&tok=" + token; ; 

    Serial.println("Requesting URL: " + url);

    HTTPClient http_tts;
    http_tts.setTimeout(Timeout);
    http_tts.begin(url);
    http_tts.collectHeaders(headerKeys, 2);

    int httpResponseCode_tts = http_tts.GET();
    
    
    if (httpResponseCode_tts == HTTP_CODE_OK) {
        
        String contentType = http_tts.header("Content-Type");
        Serial.print("\nContent-Type = ");
        Serial.println(contentType);
        
        if (contentType.startsWith("audio")) {
            
            Serial.println("The synthesis is successful, and the result is an audio file.");

            memset(audio_file_buffer, 0, AUDIO_FILE_BUFFER_SIZE);
            
            int32_t wait_cnt = 0;
            
            while (1) {
                size_t data_available = 0;
                char *read_buffer;
                
                data_available = http_tts.getStream().available();
                    
                if (0 != data_available) {
                    read_buffer = (char *)ps_malloc(data_available); 

                    // Check if the buffer will overfolw
                    if((audio_index + data_available) >= AUDIO_FILE_BUFFER_SIZE) {
                        Serial.println("Audio buffer overflow.");
                        free(read_buffer);
                        break;
                    } 

                    // Read the data from the stream
                    http_tts.getStream().readBytes(read_buffer, data_available);

                    // Copy the data to audio file buffer
                    memcpy(audio_file_buffer + audio_index, read_buffer, data_available);
                    audio_index += data_available;

                    free(read_buffer);
                        
                    wait_cnt = 0;
                
                } 
                else {
                    wait_cnt++;
                    
                    delay(10);
                    
                    if (wait_cnt > 200) {
                        wait_cnt = 0;
                        break;  // Break the loop if the wait time exceeds 1s
                    }
                }
            }
            Serial.printf("MP3 file length = %d\n", audio_index);
            
            I2S.playMP3((uint8_t *)audio_file_buffer, audio_index);
        
        } 
        else if (contentType.equals("application/json")) {
            Serial.println("The synthesis encountered an error, and the returned result is a JSON text.");
        } 
        else {
            Serial.println("Unknown Content-Type");
        }

        http_tts.end();

    } 
    else {
        Serial.println("Failed to receive audio file. HTTP response code: ");
        Serial.println(httpResponseCode_tts);
    }
}

void setup_mic_pins() {

    I2S.setPinsPdmRx(I2S_SCK, I2S_DIN);

    if (!I2S.begin(PDM_RX_mode, sampleRate, bps, slot, -1)) {
        Serial.println("Failed to initialize I2S for MIC!");
        while (1); 
    }
}

void setup_speaker_pins() {

    I2S.setPins(I2S_BCLK, I2S_LRC, I2S_DOUT);

    if (!I2S.begin(I2S_MODE_STD, sampleRate, bps, slot, I2S_STD_SLOT_BOTH)) {
        Serial.println("Failed to initialize I2S for speaker!");
        while (1); 
    }
}

//分离录音和语音识别模块避免重复
String recordAndRecognizeSpeech(){
    start_record = 1;
    unsigned long timeout = recordTimesSeconds * 1000 + 3000; // 录音时间 + 3秒缓冲
    unsigned long startTime = millis();
    
    while(record_complete != 1) {
        delay(10);
        if (millis() - startTime > timeout) {
            Serial.println("录音超时!");
            start_record = 0;
            delay(50);// 等待一小段时间让录音任务有机会响应
            record_complete = 0; // 强制重置录音完成标志，防止后续混乱
            I2S.end();// 重置I2S以防硬件状态异常
            delay(100);
            setup_mic_pins();
            return ""; // 返回空字符串表示失败
        }
    }
    record_complete = 0;
    start_record = 0;

    memset(data_json, '\0', data_json_len * sizeof(char));
    strcat(data_json,"{");
    strcat(data_json,"\"format\":\"pcm\",");
    strcat(data_json,"\"rate\":16000,");
    strcat(data_json,"\"dev_pid\":1537,");
    strcat(data_json,"\"channel\":1,");
    strcat(data_json,"\"cuid\":\"121971725\",");
    strcat(data_json, "\"token\":\"");
    strcat(data_json, token.c_str());
    strcat(data_json,"\",");
    sprintf(data_json + strlen(data_json), "\"len\":%d,", adc_data_len * 2);
    strcat(data_json, "\"speech\":\"");
    strcat(data_json, base64::encode((uint8_t *)sampleBuffer, adc_data_len * sizeof(uint16_t)).c_str());
    strcat(data_json, "\"");
    strcat(data_json, "}");

    String baidu_response = send_to_stt();
    Serial.println("Recognition complete");

    DynamicJsonDocument baidu_jsondoc(1024);
    deserializeJson(baidu_jsondoc, baidu_response);
    String talk_question = baidu_jsondoc["result"][0];
    Serial.println("Input: " + talk_question);    
    return talk_question;
}

class SmartHomeExecutor {
private:
    // 保持原有的设备映射结构（仅用于解析和验证）
    struct DeviceConfig {
        String name;           // 设备名称
        String type;           // 设备类型
        bool supportsPWM;      // 是否支持PWM调光
        int lastKnownValue;    // 最后已知状态（用于指令验证）
    };
    
    // 设备映射表（仅供解析和验证使用）
    DeviceConfig devices[7] = {
        {"living_room_light", "light", true, 0},
        {"bedroom_light", "light", true, 0},
        {"kitchen_light", "light", false, 0},
        {"outlet_1", "outlet", false, 0},
        {"outlet_2", "outlet", false, 0},
        {"curtain", "curtain", true, 0},
        {"ac_control", "ac", false, 0}
    };
    
    // 转发指令的标记
    const String FORWARD_PREFIX = "CMD:";  // Python脚本识别的指令前缀
    
    // 保持原有的房间到设备映射逻辑
    String getDeviceByLocation(const String& deviceType, const String& location) {
        for (auto& dev : devices) {
            String devName = dev.name;
            if (devName.indexOf(deviceType) >= 0 && 
                (location.isEmpty() || devName.indexOf(location) >= 0)) {
                return devName;
            }
        }
        return "";
    }
    
    // 修改：通过USB串口发送到笔记本电脑
    bool sendToLaptop(const String& command) {
        // 使用Serial（USB串口）发送数据到笔记本电脑
        if (Serial.availableForWrite() > command.length()) {
            Serial.println(command);  // 使用println自动添加换行符
            Serial.printf("[转发] 发送到笔记本: %s\n", command.c_str());
            return true;
        }
        Serial.println("[转发] 串口发送缓冲区不足");
        return false;
    }
    
    // 修改：从笔记本电脑接收响应
    String receiveFromLaptop(int timeoutMs = 3000) {
        unsigned long startTime = millis();
        String response = "";
        
        Serial.print("[转发] 等待笔记本响应...");
        
        while (millis() - startTime < timeoutMs) {
            if (Serial.available() > 0) {
                response = Serial.readStringUntil('\n');
                response.trim();
                
                // 过滤掉ESP32自己的调试输出（包含"[转发]"的）
                if (response.indexOf("[转发]") == -1) {
                    Serial.printf("收到: %s\n", response.c_str());
                    return response;
                }
                // 如果是调试信息，继续等待
            }
            delay(10);
        }
        
        Serial.println("超时");
        return "TIMEOUT";
    }
    
public:
    // 构造函数 - 不再初始化硬件串口
    SmartHomeExecutor() {
        // 注意：Serial已经在setup()中初始化
        Serial.println("SmartHomeExecutor初始化完成");
    }
    
    // 核心执行函数：保持原有JSON解析逻辑，通过笔记本中转
    String execute(const String& jsonCommand) {
        Serial.println("\n[解析] 开始处理JSON指令...");
        
        DynamicJsonDocument doc(512);
        DeserializationError error = deserializeJson(doc, jsonCommand);
        
        if (error) {
            Serial.printf("[解析] JSON解析失败: %s\n", error.c_str());
            return buildErrorResponse("JSON解析失败", error.c_str());
        }
        
        // 提取指令字段（保持原有逻辑不变）
        String command = doc["command"] | "unknown";
        String device = doc["device"] | "";
        String action = doc["action"] | "";
        JsonObject params = doc["params"];
        String location = doc["location"] | "";
        String userResponse = doc["response"] | "";
        
        Serial.printf("[解析] 解析结果: cmd=%s, dev=%s, act=%s, loc=%s\n", 
                     command.c_str(), device.c_str(), action.c_str(), location.c_str());
        
        // 验证设备是否存在
        String deviceName = getDeviceByLocation(device, location);
        if (deviceName.isEmpty()) {
            String errorMsg = "找不到设备: " + device;
            if (!location.isEmpty()) errorMsg += " 在 " + location;
            Serial.println("[解析] " + errorMsg);
            return buildErrorResponse("设备未找到", errorMsg);
        }
        
        // 查找设备配置
        DeviceConfig* devConfig = nullptr;
        for (auto& dev : devices) {
            if (dev.name == deviceName) {
                devConfig = &dev;
                break;
            }
        }
        
        if (!devConfig) {
            return buildErrorResponse("设备配置错误", "找不到设备配置: " + deviceName);
        }
        
        // 参数验证（根据命令类型）
        if (command == "control") {
            return processControlCommand(device, action, location, params, devConfig);
        } 
        else if (command == "query") {
            return processQueryCommand(device, location, devConfig);
        }
        else if (command == "config") {
            return processConfigCommand(device, action, params, location, devConfig);
        }
        else {
            return buildErrorResponse("未知命令类型", "command: " + command);
        }
    }
    
private:
    // 处理控制命令
    String processControlCommand(const String& deviceType, const String& action, 
                                const String& location, JsonObject params, 
                                DeviceConfig* devConfig) {
        // 验证action
        if (action != "on" && action != "off" && action != "toggle") {
            return buildErrorResponse("不支持的控制操作", "action: " + action);
        }
        
        // 构建转发指令（Python脚本可识别的格式）
        String forwardCommand = FORWARD_PREFIX;
        
        // 构建JSON格式的指令
        DynamicJsonDocument cmdDoc(256);
        cmdDoc["cmd"] = "control";
        cmdDoc["dev"] = devConfig->name;
        cmdDoc["act"] = action;
        
        if (!location.isEmpty()) {
            cmdDoc["loc"] = location;
        }
        
        // 添加参数
        if (!params.isNull()) {
            JsonObject cmdParams = cmdDoc.createNestedObject("params");
            for (JsonPair kv : params) {
                cmdParams[kv.key().c_str()] = kv.value();
            }
        }
        
        cmdDoc["ts"] = millis();
        
        // 序列化
        String jsonStr;
        serializeJson(cmdDoc, jsonStr);
        forwardCommand += jsonStr;
        
        // 发送到笔记本电脑
        if (!sendToLaptop(forwardCommand)) {
            return buildErrorResponse("发送失败", "串口发送失败");
        }
        
        // 等待响应
        String laptopResponse = receiveFromLaptop();
        
        // 更新最后已知状态
        if (action == "on") {
            devConfig->lastKnownValue = 100;
        } else if (action == "off") {
            devConfig->lastKnownValue = 0;
        } else if (action == "toggle") {
            devConfig->lastKnownValue = (devConfig->lastKnownValue > 0) ? 0 : 100;
        }
        
        // 解析并返回响应
        return parseLaptopResponse(laptopResponse, forwardCommand);
    }
    
    // 处理查询命令
    String processQueryCommand(const String& deviceType, const String& location, 
                              DeviceConfig* devConfig) {
        // 构建查询指令
        DynamicJsonDocument cmdDoc(200);
        cmdDoc["cmd"] = "query";
        cmdDoc["dev"] = devConfig->name;
        cmdDoc["ts"] = millis();
        
        String jsonStr;
        serializeJson(cmdDoc, jsonStr);
        
        String forwardCommand = FORWARD_PREFIX + jsonStr;
        
        // 发送到笔记本电脑
        if (!sendToLaptop(forwardCommand)) {
            return buildErrorResponse("发送失败", "查询发送失败");
        }
        
        // 等待响应
        String laptopResponse = receiveFromLaptop();
        
        return parseLaptopResponse(laptopResponse, forwardCommand);
    }
    
    // 处理配置命令
    String processConfigCommand(const String& deviceType, const String& action, 
                               JsonObject params, const String& location,
                               DeviceConfig* devConfig) {
        if (action != "set") {
            return buildErrorResponse("不支持的配置操作", "action: " + action);
        }
        
        // 验证参数
        if (deviceType == "light" && params.containsKey("brightness")) {
            int brightness = params["brightness"];
            if (brightness < 0 || brightness > 100) {
                return buildErrorResponse("参数无效", "亮度值需在0-100之间");
            }
            
            if (!devConfig->supportsPWM) {
                return buildErrorResponse("设备不支持", "该设备不支持调光");
            }
            
            // 构建配置指令
            DynamicJsonDocument cmdDoc(256);
            cmdDoc["cmd"] = "config";
            cmdDoc["dev"] = devConfig->name;
            cmdDoc["act"] = "set_brightness";
            
            JsonObject cmdParams = cmdDoc.createNestedObject("params");
            cmdParams["brightness"] = brightness;
            
            cmdDoc["ts"] = millis();
            
            String jsonStr;
            serializeJson(cmdDoc, jsonStr);
            
            String forwardCommand = FORWARD_PREFIX + jsonStr;
            
            // 发送到笔记本电脑
            if (!sendToLaptop(forwardCommand)) {
                return buildErrorResponse("发送失败", "配置发送失败");
            }
            
            // 等待响应
            String laptopResponse = receiveFromLaptop();
            
            // 更新最后已知状态
            devConfig->lastKnownValue = brightness;
            
            return parseLaptopResponse(laptopResponse, forwardCommand);
        }
        
        return buildErrorResponse("配置参数无效", "不支持的配置类型");
    }
    
    // 解析笔记本电脑的响应
    String parseLaptopResponse(const String& response, const String& sentCommand) {
        if (response == "TIMEOUT") {
            return buildErrorResponse("响应超时", "未收到笔记本响应");
        }
        
        // 尝试解析响应JSON
        DynamicJsonDocument respDoc(256);
        DeserializationError error = deserializeJson(respDoc, response);
        
        if (error) {
            // 如果不是JSON，直接返回原始响应
            return buildSuccessResponse("指令已转发", response, sentCommand);
        }
        
        // 如果是JSON，提取状态信息
        String status = respDoc["status"] | "unknown";
        String message = respDoc["message"] | "无消息";
        
        if (status == "success" || status == "ok") {
            return buildSuccessResponse(message, response, sentCommand);
        } else {
            return buildErrorResponse("执行失败", "笔记本返回: " + message);
        }
    }
    
    // 构建成功响应
    String buildSuccessResponse(const String& message, const String& rawResponse, 
                               const String& forwardedCommand) {
        DynamicJsonDocument doc(512);
        doc["status"] = "success";
        doc["message"] = message;
        doc["raw_response"] = rawResponse;
        doc["forwarded_command"] = forwardedCommand;
        doc["timestamp"] = millis();
        
        String response;
        serializeJson(doc, response);
        return response;
    }
    
    // 构建错误响应
    String buildErrorResponse(const String& error, const String& details) {
        DynamicJsonDocument doc(256);
        doc["status"] = "error";
        doc["error"] = error;
        doc["details"] = details;
        doc["timestamp"] = millis();
        
        String response;
        serializeJson(doc, response);
        return response;
    }
    
public:
    // 检查笔记本电脑中转服务是否就绪
    bool checkConnection() {
        Serial.println("检查笔记本连接...");
        
        // 发送测试指令
        String testCommand = "PING:" + String(millis());
        Serial.println(testCommand);
        
        // 等待响应
        unsigned long startTime = millis();
        while (millis() - startTime < 2000) {
            if (Serial.available() > 0) {
                String response = Serial.readStringUntil('\n');
                response.trim();
                
                // 忽略自己的调试输出
                if (response.indexOf("[") == -1) {
                    Serial.printf("[测试] 收到响应: %s\n", response.c_str());
                    
                    if (response.indexOf("PONG") >= 0 || response.indexOf("READY") >= 0) {
                        Serial.println("笔记本中转服务就绪");
                        return true;
                    }
                }
            }
            delay(10);
        }
        
        Serial.println("笔记本中转服务无响应");
        Serial.println("请确保: ");
        Serial.println("1. Python脚本正在运行");
        Serial.println("2. 串口监视器已关闭");
        Serial.println("3. 串口端口设置正确");
        return false;
    }   
    
    // 获取设备状态
    String getDeviceStatus(const String& deviceName) {
        for (auto& dev : devices) {
            if (dev.name == deviceName) {
                DynamicJsonDocument doc(200);
                doc["device"] = dev.name;
                doc["last_known_value"] = dev.lastKnownValue;
                doc["supports_pwm"] = dev.supportsPWM;
                doc["status"] = "cached";
                
                String response;
                serializeJson(doc, response);
                return response;
            }
        }
        return "{\"error\":\"设备未找到\"}";
    }
};
SmartHomeExecutor homeExecutor;



void setup(){
    Serial.begin(115200);
    delay(1000);
     if (homeExecutor.checkConnection()) {
    Serial.println("智能家居系统就绪");
}

    pinMode(key, INPUT_PULLUP);
    pinMode(key_smart, INPUT_PULLUP);
    sampleBuffer = (signed short*)ps_malloc(320000 * sizeof(signed short));
    if (!sampleBuffer || !esp_ptr_external_ram(sampleBuffer)) {
        Serial.println("分配PSRAM失败!");
    }

    setup_mic_pins();

    // WiFi connect
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid,password);
    Serial.print("Connecting to Wifi ..");

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print('.');
        delay(1000);
    }

    Serial.println(WiFi.localIP());
    Serial.printf("\r\n-- wifi connect success! --\r\n");

     // ========== PSRAM初始化和验证 ==========
    Serial.println("\n========== 内存配置检查 ==========");
    
    // 检查PSRAM是否可用
    if (psramInit()) {
        size_t total_psram = ESP.getPsramSize();
        size_t free_psram = ESP.getFreePsram();
        
        Serial.printf(" PSRAM初始化成功!\n");
        Serial.printf("   PSRAM总容量: %.2f MB\n", total_psram / (1024.0 * 1024.0));
        Serial.printf("   当前可用PSRAM: %.2f MB\n", free_psram / (1024.0 * 1024.0));
    } else {
        Serial.println("  警告: PSRAM初始化失败，将使用内部RAM");
        Serial.println("   请检查开发板菜单中PSRAM设置是否为'OPI PSRAM'");
        while(1) {
            delay(1000); // 保持停止状态，可通过串口看到错误信息
        }
    }
    
    // 显示内存状态
    Serial.printf("内部RAM空闲: %.2f KB\n", ESP.getFreeHeap() / 1024.0);
    Serial.printf("芯片型号: %s\n", ESP.getChipModel());
    Serial.println("==================================\n");

    token = get_token();
    Serial.println("按下按键进行录音");
    Serial.println("注意：录音时长最多为10秒");

    // Allocate memory for the MIC data JSON buffer
    data_json = (char *)ps_malloc(data_json_len * sizeof(char));  
    if (!data_json) {
        Serial.println("Failed to allocate memory for data_json");
        while (1) {
            delay(2000);    //  关键：让CPU休眠至少1秒
            Serial.print("Error"); // 维持串口连接
        }
    }

    // Allocate memory for the audio file buffer
    audio_file_buffer = (char *)ps_malloc(AUDIO_FILE_BUFFER_SIZE * sizeof(char));
    if (!audio_file_buffer || !esp_ptr_external_ram(audio_file_buffer)) {
    Serial.println(" audio_file_buffer内存分配失败!");
        while (1) {
            delay(2000);    //  关键：让CPU休眠至少1秒
            Serial.print("Error"); // 维持串口连接
        }
}
    Serial.println("内存分配成功");

    // Create a new task for MIC data reading
    xTaskCreate(capture_samples, "CaptureSamples", 1024 * 32, (void*)sample_buffer_size, 10, NULL);

}

void loop() {
    String LLM_answer;

    if (digitalRead(key) == 0) {
        delay(30);
        if (digitalRead(key) == 0) {
            Serial.printf("Start recording\r\n");
            String talk_question_AI = recordAndRecognizeSpeech();
            LLM_answer = get_GPT_answer(talk_question_AI);
            Serial.println("\nAnswer: " + LLM_answer);

            //Change I2S pins setup from MIC to speaker
            I2S.end(); 
            setup_speaker_pins();
            get_voice_answer(LLM_answer);

            I2S.end();
            setup_mic_pins();
            Serial.println("\nPress the button to talk to me.");
            return;

        }
    }
    if (digitalRead(key_smart) == 0) {
        delay(30);
        if (digitalRead(key_smart) == 0) {
            Serial.printf("Start recording\r\n");
            String talk_question_smart_home = recordAndRecognizeSpeech();
            processVoiceCommand(talk_question_smart_home);
            return;

        }
    }    
    delay(1);
}


static void capture_samples(void* arg) {

    size_t bytes_read = 0;

    while (1) {
        if (start_record == 1) {
            I2S.readBytes((char*)sampleBuffer, sample_buffer_size);
            record_complete = 1;
            start_record = 0;
        }
        else {
            delay(100);
        }
    }
    vTaskDelete(NULL);
}

//Get BAIDU token
String get_token() {
    HTTPClient http_baidu;

    String url = String("https://aip.baidubce.com/oauth/2.0/token?grant_type=client_credentials&client_id=") + CLIENT_ID + "&client_secret=" + CLIENT_SECRET;
    Serial.println(url);

    http_baidu.begin(url);

    int httpCode = http_baidu.GET();

    if (httpCode > 0) {
        String payload = http_baidu.getString();
        DynamicJsonDocument doc(1024);
        deserializeJson(doc, payload);
        token = doc["access_token"].as<String>();
        Serial.println(token);
    } 
    else {
        Serial.println("Error on HTTP request for token");
    }
    http_baidu.end();

    return token;
}

String send_to_stt() {
    HTTPClient http_stt;

    http_stt.begin("http://vop.baidu.com/server_api");
    http_stt.addHeader("Content-Type", "application/json");

    int httpResponseCode = http_stt.POST(data_json);

    if (httpResponseCode > 0) {
        if (httpResponseCode == HTTP_CODE_OK) {
        String payload = http_stt.getString();
        Serial.println(payload);
        return payload;
        }
    } 
    else {
        Serial.printf("[HTTP] POST failed, error: %s\n", http_stt.errorToString(httpResponseCode).c_str());
    }
    http_stt.end();
}

//TONGYIQIANWEN LLM
String get_GPT_answer(String llm_inputText) {
    HTTPClient http_llm;

    http_llm.setTimeout(Timeout);
    http_llm.begin(apiUrl);

    http_llm.addHeader("Content-Type", "application/json");
    http_llm.addHeader("Authorization", String(apikey));

    String payload_LLM = "{\"model\":\"qwen-turbo\",\"input\":{\"messages\":[{\"role\": \"system\",\"content\": \"要求下面的回答严格控制在256字符以内\"},{\"role\": \"user\",\"content\": \"" + llm_inputText + "\"}]}}";
    
    int httpResponseCode = http_llm.POST(payload_LLM);

    if (httpResponseCode == 200) {
        String response = http_llm.getString();
        http_llm.end();
        Serial.println(response);

        // Parse JSON response
        DynamicJsonDocument jsonDoc(1024);
        deserializeJson(jsonDoc, response);
        String outputText = jsonDoc["output"]["text"];

        return outputText;
    
    } 
    else {
        http_llm.end();
        Serial.printf("Error %i \n", httpResponseCode);
        return "<error>";
    }
}

// 辅助函数：转义JSON字符串中的特殊字符
String escapeJsonString(const String& input) {
    String output;
    output.reserve(input.length() * 2);
    
    for (size_t i = 0; i < input.length(); i++) {
        char sign = input[i];
        switch (sign) {
            case '\\': output += "\\\\"; break;
            case '\"': output += "\\\""; break;
            case '\b': output += "\\b"; break;
            case '\f': output += "\\f"; break;
            case '\n': output += "\\n"; break;
            case '\r': output += "\\r"; break;
            case '\t': output += "\\t"; break;
            default: output += sign; break;
        }
    }
    return output;
}

//智能家居回收LLM模型的函数
String get_GPT_answer_smart_home(String llm_inputText) {
    HTTPClient http_llm;

    http_llm.setTimeout(Timeout);
    http_llm.begin(apiUrl);

    http_llm.addHeader("Content-Type", "application/json");
    http_llm.addHeader("Authorization", String(apikey));

    // 构建请求，这次我们要求返回JSON格式的指令
    String payload_LLM = "{";
    payload_LLM += "\"model\":\"qwen-turbo\",";
    payload_LLM += "\"input\":{\"messages\":[";
    
    // 系统指令 - 强制JSON输出格式
    payload_LLM += "{\"role\": \"system\", \"content\": \"";
    payload_LLM += "你是一个智能家居控制系统。用户指令将被转换为JSON格式的控制命令。";
    payload_LLM += "必须且只能返回一个有效的JSON对象，格式如下：\\n";
    payload_LLM += "{";
    payload_LLM += "  \\\"command\\\": [必须填写，值域: 'control', 'query', 'config'],";
    payload_LLM += "  \\\"device\\\": [必须填写，值域: 'light', 'outlet', 'fan', 'ac', 'curtain', 'tv'],";
    payload_LLM += "  \\\"action\\\": [根据command选择: 'on'/'off'/'toggle'（control时）, 'status'（query时）, 'set'（config时）],";
    payload_LLM += "  \\\"params\\\": {可选，参数对象，如 {\\\"brightness\\\": 50} },";
    payload_LLM += "  \\\"location\\\": [可选，值域: 'living_room', 'bedroom', 'kitchen', 'bathroom'],";
    payload_LLM += "  \\\"response\\\": [必须，给用户的简短语音回复，20字内]";
    payload_LLM += "}\\n";
    payload_LLM += "示例1 - 开灯: {\\\"command\\\":\\\"control\\\",\\\"device\\\":\\\"light\\\",\\\"action\\\":\\\"on\\\",\\\"location\\\":\\\"living_room\\\",\\\"response\\\":\\\"已打开客厅灯\\\"}\\n";
    payload_LLM += "示例2 - 查询温度: {\\\"command\\\":\\\"query\\\",\\\"device\\\":\\\"ac\\\",\\\"action\\\":\\\"status\\\",\\\"response\\\":\\\"当前温度26度\\\"}\\n";
    payload_LLM += "示例3 - 调光: {\\\"command\\\":\\\"config\\\",\\\"device\\\":\\\"light\\\",\\\"action\\\":\\\"set\\\",\\\"params\\\":{\\\"brightness\\\":70},\\\"response\\\":\\\"亮度调到70%\\\"}\\n";
    payload_LLM += "重要：仅返回JSON，无其他任何文本。如果无法理解指令，返回: {\\\"error\\\":\\\"无法理解指令\\\",\\\"response\\\":\\\"请再说一遍\\\"}";
    payload_LLM += "\"},";
    
    // 用户指令
    payload_LLM += "{\"role\": \"user\", \"content\": \"" + escapeJsonString(llm_inputText) + "\"}";
    payload_LLM += "]}}";
    
    int httpResponseCode = http_llm.POST(payload_LLM);
    
    if (httpResponseCode == 200) {
        String response = http_llm.getString();
        http_llm.end();
        
        // 强化JSON解析和验证
        DynamicJsonDocument jsonDoc(1024);
        DeserializationError error = deserializeJson(jsonDoc, response);
        
        if (error) {
            return "{\"error\":\"JSON解析失败\",\"raw_response\":\"" + escapeJsonString(response.substring(0, 100)) + "\"}";
        }
        
        // 验证是否包含必需的字段
        String outputText = jsonDoc["output"]["text"];
        DynamicJsonDocument resultDoc(512);
        error = deserializeJson(resultDoc, outputText);
        
        if (error || !resultDoc.containsKey("command") || !resultDoc.containsKey("response")) {
            // 如果不是有效JSON，尝试提取可能的JSON部分
            int jsonStart = outputText.indexOf('{');
            int jsonEnd = outputText.lastIndexOf('}');
            if (jsonStart >= 0 && jsonEnd > jsonStart) {
                String jsonPart = outputText.substring(jsonStart, jsonEnd + 1);
                return jsonPart;
            }
            return "{\"error\":\"格式错误\",\"response\":\"指令格式不正确\"}";
        }
        
        return outputText;
        
    } else {
        http_llm.end();
        return "{\"error\":\"API请求失败\",\"code\":" + String(httpResponseCode) + "}";
    }
}

//ESP32处理智能家居LLM模型返回的内容
void processVoiceCommand(String userSpeech) {
    // 获取大模型的JSON响应
    String aiJson = get_GPT_answer_smart_home(userSpeech);
    
    // 使用SmartHomeExecutor处理
    String result = homeExecutor.execute(aiJson);
    
    // 解析并处理结果
    DynamicJsonDocument doc(256);
    deserializeJson(doc, result);
    
    if (doc["status"] == "success") {
        // 指令已成功转发到Arduino
        Serial.println("指令转发成功");
        
        // 可以获取Arduino的响应
        String arduinoResponse = doc["arduino_response"];
    } else {
        // 错误处理
        Serial.println("错误: " + doc["error"].as<String>());
    }
}