#include <Arduino.h>
#include "base64.h"
#include "WiFi.h"
#include "HTTPClient.h"
#include <ESP_I2S.h>
#include <ArduinoJson.h>
#include <UrlEncode.h>
#include <esp32-hal-psram.h>

const uint8_t key = 3;        // Push key
const uint8_t I2S_LRC = 4;    //ESP32 speaker pins setup
const uint8_t I2S_BCLK = 5;
const uint8_t I2S_DOUT = 6;  
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

void setup(){
    Serial.begin(115200);
    delay(1000);

    pinMode(key, INPUT_PULLUP);
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

            start_record = 1;
            while(record_complete != 1) {
                delay(10);
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

            LLM_answer = get_GPT_answer(talk_question);
            Serial.println("\nAnswer: " + LLM_answer);

            //Change I2S pins setup from MIC to speaker
            I2S.end(); 
            setup_speaker_pins();
            get_voice_answer(LLM_answer);

            I2S.end();
            setup_mic_pins();
            Serial.println("\nPress the button to talk to me.");

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

