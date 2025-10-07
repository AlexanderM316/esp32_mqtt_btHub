# ESP32 MQTT Bluetooth Hub

**ESP32 MQTT Bluetooth Hub** — это прошивка для ESP32, которая позволяет автоматически обнаруживать Bluetooth-устройства (например, смарт-лампы), управлять их питанием и состоянием через MQTT-брокер.  
Проект ориентирован на интеграцию с системами автоматизации.
 
## ⚙️ Конфигурация MQTT и WiFi 

До сборкой проекта необходимо настроить параметры Wi-Fi и MQTT в файле common.h

## 🧩 Структура проекта
```bash
esp32_mqtt_btHub/
├── main/
│   ├── device_manager.c
│   ├── mqtt_manager.c
│   ├── wifi_manager.c       
│   ├── esp32_mqtt_btHub.c  ← main
│   └── include/
│       ├── device_manager.h
│       ├── mqtt_manager.h
│       ├── wifi_manager.h   
│       └── common.h         ← общие константы и конфигурация (Wi-Fi, MQTT и др.)
├── CMakeLists.txt
├── sdkconfig
├── README.md
└── LICENSE
```
