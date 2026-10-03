// камера FREENOVE-ESP32-WROVER
#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <esp_http_server.h>
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "board_config.h"

// --- НАСТРОЙКИ WI-FI ---
const char *ap_ssid = "ESP32_Camera";      
const char *ap_password = "987654321";    

// --- НАСТРОЙКИ ЛИДАРА ---
#define NUM_SECTORS 4
#define LIDAR_RX_PIN 35  // Провод TX лидара и резистор 1 кОм к 3.3V подключены сюда!
#define LIDAR_TX_PIN -1  
#define LIDAR_BAUDRATE 115200 

// Массив расстояний для секторов: 0-Вперед, 1-Вправо, 2-Назад (игнорируется), 3-Влево
volatile uint16_t sectorDistances[NUM_SECTORS] = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };

#define CRITICAL_DIST_MM 100  // Порог срабатывания: 10см
#define ALARM_INTERVAL_MS 100 // Интервал проверки препятствий (100 мс)

// --- НАСТРОЙКА БЛОКА СПАМА НА ВРЕМЯ ---
#define SPAM_TIMEOUT_MS 1500  // Время паузы между отправками "9" (1000 мс = 1 секунда)

void startCameraServer();
void setupLedFlash();
extern httpd_handle_t camera_httpd; 
TaskHandle_t LidarTaskHandle = NULL;

// --- Обработчик POST-запроса от Python по Wi-Fi ---
// Переменная для хранения сообщения от второй ESP
String messageFromSecondESP = ""; 

// 1. Обработчик для ВТОРОЙ ESP (куда она будет слать данные)
static esp_err_t esp_post_handler(httpd_req_t *req) {
    char content[32]; // Буфер побольше
    size_t recv_size = sizeof(content);

    int ret = httpd_req_recv(req, content, recv_size - 1);
    if (ret <= 0) return ESP_FAIL;

    content[ret] = '\0';
    messageFromSecondESP = String(content); // Сохраняем сообщение

    httpd_resp_sendstr(req, "RECEIVED");
    return ESP_OK;
}

// 2. Измененный обработчик для PYTHON
static esp_err_t cmd_post_handler(httpd_req_t *req) {
    char content[10];
    size_t recv_size = sizeof(content);

    int ret = httpd_req_recv(req, content, recv_size - 1);
    if (ret <= 0) return ESP_FAIL;

    content[ret] = '\0'; 
    Serial.println(content); // Пересылаем в Ардуину

    // Если есть сообщение от второй ESP — отдаем его Пайтону, иначе пишем "OK"
    if (messageFromSecondESP != "") {
        httpd_resp_sendstr(req, messageFromSecondESP.c_str());
        messageFromSecondESP = ""; // Очищаем после отправки
    } else {
        httpd_resp_sendstr(req, "OK");
    }
    return ESP_OK;
}

// Не забудьте зарегистрировать новый URI в setup():
httpd_uri_t esp_uri = { .uri = "/from_esp", .method = HTTP_POST, .handler = esp_post_handler, .user_ctx = NULL };

// --- ФУНКЦИЯ ПАРСИНГА ПАКЕТА ЛИДАРА LIDAR X2 (МАРКЕР 0xA5) ---
bool parseAndProcessPacket() {
  if (Serial2.available() > 64) {
    while(Serial2.available()) Serial2.read();
    return false;
  }

  if (Serial2.available() >= 22) {
    if (Serial2.peek() != 0xA5) {
      Serial2.read(); 
      return false;
    }

    Serial2.read(); // Удаляем маркер 0xA5
    Serial2.read(); // Пропускаем байт типа 0x00
    uint8_t index = Serial2.read(); // Читаем индекс пакета для вычисления угла
    Serial2.read(); // Пропускаем байт режима

    if (index == 0xA0 || index == 0x00) {
      for (int s = 0; s < NUM_SECTORS; s++) {
        sectorDistances[s] = 0xFFFF; 
      }
    }

    for (int i = 0; i < 4; i++) {
      uint8_t byte1 = Serial2.read();
      uint8_t byte2 = Serial2.read();
      Serial2.read(); // Пропускаем байт качества 1
      Serial2.read(); // Пропускаем байт качества 2

      uint16_t distance = byte1 | ((byte2 & 0x3F) << 8);
      
      uint8_t baseIndex = (index >= 0xA0) ? 0xA0 : 0x00;
      uint16_t angle = (index - baseIndex) * 4 + i; 

      if (distance > 15 && distance < 60000) { 
        int targetSector = -1;
        
        if (angle >= 315 || angle < 45)       targetSector = 0; // 0 - Вперед
        else if (angle >= 45 && angle < 135)  targetSector = 1; // 1 - Вправо
        else if (angle >= 135 && angle < 225) targetSector = 2; // 2 - Назад (Сектор с мотором)
        else if (angle >= 225 && angle < 315) targetSector = 3; // 3 - Влево

        if (targetSector != -1 && distance < sectorDistances[targetSector]) {
          sectorDistances[targetSector] = distance;
        }
      }
    }       
    
    Serial2.read(); // Пропускаем Checksum
    Serial2.read();
    return true;
  }
  return false; 
}

// --- ПРОВЕРКА ПРЕПЯТСТВИЙ ТОЛЬКО В ПЕРЕДНЕМ СЕКТОРЕ ---
void checkObstaclesAndAlarm() {
  static uint32_t lastCheckTime = 0;
  static uint32_t last9SentTime = 0; // Запоминает время последней отправки девятки
  uint32_t now = millis();

  if (now - lastCheckTime < ALARM_INTERVAL_MS) return;
  lastCheckTime = now;

  // Оставляем проверку СТРОГО переднего сектора (индекс 0)
  // Все остальные сектора (1, 2, 3) игнорируются
  bool dangerDetected = (sectorDistances[0] < CRITICAL_DIST_MM);

  // Если обнаружена опасность прямо перед роботом
  if (dangerDetected) {
    if (now - last9SentTime >= SPAM_TIMEOUT_MS) {
      Serial.println("9"); // Отправляем "9\r\n" в Ардуину на скорости 9600
      last9SentTime = now; // Перезапускаем таймер блокировки спама
    }
  }
}

// --- ОТДЕЛЬНЫЙ ПОТОК ДЛЯ ЛИДАРА (ЯДРО 0) ---
void LidarTask(void *pvParameters) {
  while(Serial2.available()) Serial2.read(); // Очистка буфера при старте
  
  while (true) {
    while (Serial2.available() >= 22) {
      parseAndProcessPacket();
    }
    checkObstaclesAndAlarm();
    vTaskDelay(1 / portTICK_PERIOD_MS); 
  }
}

// --- SETUP ---
void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  Serial.begin(9600); // Скорость 9600 для Ардуины
  Serial.setDebugOutput(false);

  pinMode(LIDAR_RX_PIN, INPUT); 
  Serial2.begin(LIDAR_BAUDRATE, SERIAL_8N1, LIDAR_RX_PIN, LIDAR_TX_PIN);

  // Конфигурация камеры
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0; config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM; config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM; config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM; config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM; config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000; config.frame_size = FRAMESIZE_UXGA; config.pixel_format = PIXFORMAT_JPEG;  
  config.grab_mode = CAMERA_GRAB_WHEN_EMPTY; config.fb_location = CAMERA_FB_IN_PSRAM; config.jpeg_quality = 12; config.fb_count = 1;

  if (config.pixel_format == PIXFORMAT_JPEG) {
    if (psramFound()) {
      config.jpeg_quality = 10;
      config.fb_count = 2;
      config.grab_mode = CAMERA_GRAB_LATEST;
    } else {
      config.frame_size = FRAMESIZE_SVGA;
      config.fb_location = CAMERA_FB_IN_DRAM;
    }
  } else {
    config.frame_size = FRAMESIZE_240X240;
  }

  esp_camera_init(&config);

  sensor_t *s = esp_camera_sensor_get();
  if (s && s->id.PID == OV3660_PID) {
    s->set_vflip(s, 1);        
    s->set_brightness(s, 1);   
    s->set_saturation(s, -2);  
  }
  if (s && config.pixel_format == PIXFORMAT_JPEG) {
    s->set_framesize(s, FRAMESIZE_QVGA);
  }

#if defined(LED_GPIO_NUM)
  setupLedFlash();
#endif

  WiFi.softAP(ap_ssid, ap_password);
  startCameraServer();

  if (camera_httpd != NULL) {
      httpd_register_uri_handler(camera_httpd, &cmd_uri);
      httpd_register_uri_handler(camera_httpd, &esp_uri);
  }

  for (int i = 0; i < NUM_SECTORS; i++) {
    sectorDistances[i] = 0xFFFF;
  }

  xTaskCreatePinnedToCore(LidarTask, "LidarTask", 4096, NULL, 1, &LidarTaskHandle, 0);
} 

void loop() {
  delay(100); 
}