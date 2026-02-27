#### 介绍
AI-Arduino结合寒假项目

该项目是利用百度云语音识别API，阿里云AI-LLM模型API，以及Seeed Studio XIAO ESP32S3 Sense、Arduino板制作的智能语音助手。该项目能够实现以下功能：

一、语音助手：通过语音识别和LLM模型，实现问答，可聊天、询问天气、新闻、时间等功能。

二、智能家居控制：通过语音识别，可以给由Arduino UNO R3开发板控制的家居发出指令。

三、倒计时设置：通过语音输入，支持复杂语音设置闹钟。


#### 编程语音使用

编程语音使用说明：采用Python和Arduino IDE修改后的C与C++语音。


#### 软件安装教程

一、IDE配置：对于国内用户，通常需要进行以下操作：

    1.从Arduino官网下载Arduino IDE 
[Arduino官网链接](https://www.arduino.cc/en/software/)

![输入图片说明](README%E5%9B%BE%E7%89%87/image.png)

    2.安装ESP32核心。由于国内用户未知网络原因采用Arduino CLI方式安装

        a.下载Arduino CLI  
[Windows,Arduino CLI官网下载](https://arduino.github.io/arduino-cli/1.4/installation/)

![输入图片说明](README%E5%9B%BE%E7%89%87/image2.png)

        b.执行以下操作：
        
![输入图片说明](README%E5%9B%BE%E7%89%87/image3.png)

注意：有一步指令需要更改:
将
```cpp
arduino-cli core install esp32:esp32@3.0.4-cn
``` 
改为：
```cpp
arduino-cli core install esp32:esp32@3.2.1-cn
```

    3.安装扩展库。
按图片内容操作即可。
![输入图片说明](README%E5%9B%BE%E7%89%87/image23.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image24.png)

二、配置Python环境。

由于不同人使用python有不同的习惯，这里给出全部教程。按链接中教程配置完成即可。
[python环境设置教程](https://www.runoob.com/python3/python3-install.html)

完成上面步骤后安装python其他库

只需要在python终端输入：
```python
pip install pyserial -i https://pypi.tuna.tsinghua.edu.cn/simple
```

三、从仓库克隆下来代码

这里提供两种方式：
一、[gitee仓库（服务器在国内）](https://gitee.com/cosed/ai_-arduino_-stt_-tts)

二、[github仓库（服务器在国外）](https://github.com/jiamankaer/HUST-AI-Arduino)

![输入图片说明](README%E5%9B%BE%E7%89%87/image4.png)

只下载红框框起来的内容就可以了。

四、注册相关账号得到API。

1.百度云
[百度云注册链接](https://cloud.baidu.com/)
![输入图片说明](README%E5%9B%BE%E7%89%87/image8.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image9.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image10.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image11.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image12.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image13.png)
得到API:这里需要注意
![输入图片说明](README%E5%9B%BE%E7%89%87/image14.png)

2.阿里云  [阿里云注册链接](https://dashscope.aliyun.com/)
![输入图片说明](README%E5%9B%BE%E7%89%87/image15.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image16.png)
![输入图片说明](README%E5%9B%BE%E7%89%87/image17.png)
创建API
![输入图片说明](README%E5%9B%BE%E7%89%87/image18.png)

这里一定要保存 API-KEY 

五：补全代码待填部分：

1. WIFI SSD账号与密码
在下面部分填写你的WIFI账号和密码
```cpp
const char *ssid = "Your Wifi account name";
const char *password = "Your Wifi password";
```
2. 百度云的AppID, API Key and Secret Key
填写百度云的AppID, API Key and Secret Key
```cpp
const char *CUID = "Your Baidu Cloud AppID";
const char *CLIENT_ID = "Your Baidu Cloud API Key";
const char *CLIENT_SECRET = "Your Baidu Cloud Secret Key";
```
3. 阿里云 API-KEY
填写你的阿里云API-KEY.
```cpp
const char* apikey = "Your Aliyun API-KEY";
```
4. 代码中一部分
填写你的百度云AppID

```cpp
url = baidutts_url;
url += "?tex=" + encodedText;
url += "&lan=zh";
url += "&cuid=Your Baidu Cloud AppID";
url += "&ctp=1";
```
填写你的百度云AppID

```cpp
strcat(data_json,"\"channel\":1,");
strcat(data_json,"\"cuid\":\"Your Baidu Cloud AppID\",");
strcat(data_json, "\"token\":\"");
```

#### 硬件安装教程：

一、需要的硬件

1.Arduino UNO R3开发板
2.Arduino UNO R3改进版套件（使用内部提供的3个按键、3个LED灯、一个无源蜂鸣器）
3.Seeed Studio XIAO ESP32S3开发板
4.扬声器模块

二、硬件接线原理图：
ESP32板子的接线：
![输入图片说明](README%E5%9B%BE%E7%89%87/pin%20connection.001.jpeg)


附录：引脚注册代码：
```cpp
const uint8_t key = 3;
const uint8_t I2S_LRC = 4;
const uint8_t I2S_BCLK = 5;
const uint8_t I2S_DOUT = 6;  
const uint8_t key_smart = 7;
const uint8_t key_timer = 8; 
const uint8_t I2S_DIN = 41;   
const uint8_t I2S_SCK = 42; 
```

Arduino UNO R3接线：
![输入图片说明](README%E5%9B%BE%E7%89%87/README%E7%94%A8%E5%9B%BE_Arduino.png)
不再具体讲述共地等基础接线知识。只讲述必须要软硬件相一致的引脚数据部分。

三、实物示意图：
![输入图片说明](README%E5%9B%BE%E7%89%87/实物连接.jpg)


#### 使用说明

一、  代码上传烧录

请将intelligent_furniture文件夹中的ino文件用Arduino IDE上传至Arduino UNO R3开发板。
![输入图片说明](README%E5%9B%BE%E7%89%87/image7.png)
按下面步骤进行： 找到工具-开发板-Arduino UNO
![输入图片说明](README%E5%9B%BE%E7%89%87/image19.png)
上传代码
![输入图片说明](README%E5%9B%BE%E7%89%87/image20.png)


将ESP32S3_AI_voiceassistant_timer.ino 用Arduino IDE上传至 Seeed Studio XIAO ESP32S3 Sense开发板
![输入图片说明](README%E5%9B%BE%E7%89%87/image6.png)
按下面步骤进行： 

1.选择串口和开发板

![输入图片说明](README%E5%9B%BE%E7%89%87/image21.png)

2.打开PSRAM。  确保PSRAM后面的参数是 工具栏内 OPI PSRAM （注意只有选了ESP32开发板之后，工具一栏才会新增这些内容。）

![输入图片说明](README%E5%9B%BE%E7%89%87/image22.png)

之后同理上传代码
![输入图片说明](README%E5%9B%BE%E7%89%87/image20.png)

二、   通讯连接

1.将两块开发板都用USB连接到同一台电脑上

2.下载并修改提供的python脚本，修改串口名称.可以通过设备管理器，或者Arduino IDE设置开发板进行查看。

![输入图片说明](README%E5%9B%BE%E7%89%87/image25.png)

![输入图片说明](README%E5%9B%BE%E7%89%87/image26.png)

3.运行python脚本。保证在使用时该脚本维持运行。

三、  具体使用

这里使用了3个按键。我们分别命名为按键、智能家居按键、倒计时按键。（对应引脚3,7,8）
3个按键具有相同的逻辑，都是长按按键进行录音，松开按键停止录音。但是录音最大时长不能超过10秒
不过倒计时按键有其他要求，第一次按下会有提示音。提示音结束后才能长按按键进行录音。

按键：负责语音助手功能，可以与智能AI助手进行各种内容的聊天、咨询。

智能家居按键：负责控制家居，只能接受控制家居的指令。

倒计时按键：这是为控制闹钟家居特别设置的按键。

PS：对于三种不同功能，语音助手功能是支持脱离Arduino UNO R3和电脑单独使用的。实际上来说，不牵扯硬件的都可以脱离Arduino UNO R3和电脑使用。

#### 开源项目引用。

原开源项目地址：
https://github.com/kiwi926/ESP32S3_AI_voice_assistant
