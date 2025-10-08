# ESP32 MQTT Bluetooth Hub

**ESP32 MQTT Bluetooth Hub** — это прошивка для ESP32, которая позволяет автоматически обнаруживать Bluetooth-устройства (например, смарт-лампы), управлять их питанием и состоянием через MQTT-брокер.  
Проект ориентирован на интеграцию с системами автоматизации.
 
## ⚙️ Конфигурация MQTT и WiFi 

До сборкой проекта необходимо настроить параметры Wi-Fi и MQTT а также имя устройства к которому хотите подключиться в файле common.h

## 🧩 Структура проекта
```bash
esp32_mqtt_btHub/
├── main/
│   ├── CMakeLists.txt
│   ├── device_manager.c     ← Логика работы с BLE-устройствами
│   ├── mqtt_manager.c       ← логика работы с MQTT и обмен сообщениями
│   ├── wifi_manager.c       ← Подключение к Wi-Fi, обработка событий сети
│   ├── esp32_mqtt_btHub.c   ← main
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
