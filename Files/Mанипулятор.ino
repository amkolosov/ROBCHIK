#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include "Adafruit_SHT4x.h"

// Создаем объекты сервоприводов
Servo myservo1; // верхний
Servo myservo2; // середина
Servo myservo3; // нижний

// Создаем объект датчика SHT4x
Adafruit_SHT4x sht4 = Adafruit_SHT4x();

// Настройки Wi-Fi: ESP32-C3 подключается к точке доступа камеры
const char *cam_ssid = "ESP32_Camera";      
const char *cam_password = "987654321";    
const char *cam_server_url = "http://192.168.4";

WebServer server(80);

// Хранилище для сообщения от Python
char memorized_message[64] = {0}; 

bool cmd_received = false;
String last_command = "";

// Таймер для опроса датчика без использования блокирующего delay()
unsigned long last_sensor_time = 0;
const unsigned long sensor_interval = 2000; // Опрос датчика каждые 2000 мс (2 секунды)

// Функция установки углов для трех сервоприводов
void smooth_move(int target[3]) {
    myservo1.write(target[0]);
    myservo2.write(target[1]);
    myservo3.write(target[2]);
    Serial.printf("Сервоприводы: [%d, %d, %d]\n", target[0], target[1], target[2]);
}

void handle_cmd_post() {
    if (server.hasArg("plain")) {
        String incomingMessage = server.arg("plain"); 

        strncpy(memorized_message, incomingMessage.c_str(), sizeof(memorized_message) - 1);
        memorized_message[sizeof(memorized_message) - 1] = '\0'; 

        last_command = incomingMessage;
        cmd_received = true;

        String cameraResponse = "Error: Camera unreachable"; 

        // Пересылаем запрос на саму камеру
        if (WiFi.status() == WL_CONNECTED) {
            HTTPClient http;
            http.begin(cam_server_url);
            http.addHeader("Content-Type", "text/plain");
            
            int httpResponseCode = http.POST(incomingMessage); 
            if (httpResponseCode > 0) {
                cameraResponse = http.getString(); 
            }
            http.end(); 
        }

        // Возвращаем ответ в Python
        server.send(200, "text/plain", cameraResponse);
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000); 
    Serial.println("\n--- Старт ESP32-C3 (Режим Клиента) ---");

    // Инициализация шины I2C на пинах GPIO9 (SDA) и GPIO7 (SCL)
    Wire.begin(9, 7);

    // Инициализация датчика SHT4x
    if (!sht4.begin()) {
        Serial.println("SHT4x не найден. Проверьте подключение.");
        // Не блокируем процессор намертво, просто выводим ошибку
    } else {
        Serial.println("SHT4x найден!");
        sht4.setPrecision(SHT4X_HIGH_PRECISION);
        sht4.setHeater(SHT4X_NO_HEATER);
    }

    // Подключение сервоприводов
    myservo1.attach(4, 500, 2500);
    myservo2.attach(5, 500, 2500);
    myservo3.attach(6, 500, 2500);

    // Установка начального положения ОДИН РАЗ при включении
    int start_target[] = {60, 90, 103};
    smooth_move(start_target);
   
    Serial.print("Подключение к Wi-Fi сети камеры: ");
    Serial.println(cam_ssid);

    // Запуск подключения к Wi-Fi сети
    WiFi.begin(cam_ssid, cam_password);
  
    // Цикл ждет, пока плата физически не подключится к роутеру/камере
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
  
    // ТЕПЕРЬ выводим URL, так как плата-клиент получила IP от камеры
    Serial.println("\n[Wi-Fi Успешно подключен!]");
    Serial.print("-> Сгенерированный URL для вашего Python-скрипта: http://");
    Serial.print(WiFi.localIP());
    Serial.println("/cmd");
  
    server.on("/cmd", HTTP_POST, handle_cmd_post);
    server.begin();
} 

void loop() {
    // Опрос веб-сервера работает мгновенно
    server.handleClient();
  
    // Логика обработки команд от Python для сервоприводов
    if (cmd_received) {
        cmd_received = false; 
        int command_num = last_command.toInt();

        Serial.print("Обработка команды: ");
        Serial.println(command_num);

        // Базовый массив для возврата в исходное положение
        int reset_target[] = {60, 90, 103};

        if (command_num == 1) {
            int target1[] = {90, 90, 90};
            smooth_move(target1);
            delay(5000);
            smooth_move(reset_target);
        } 
        else if (command_num == 2) {
            int target2[] = {180, 180, 180};
            smooth_move(target2);
            delay(5000);
            smooth_move(reset_target);
        }
        else if (command_num == 3) {
            int target3[] = {90, 180, 75};
            smooth_move(target3);
            delay(5000);
            smooth_move(reset_target);
        }
        else if (command_num == 4) {
            int target4[] = {45, 23, 80};
            smooth_move(target4);
        }
        else if (command_num == 5) {
            int target5[] = {56, 55, 78};
            smooth_move(target5);
        }
    }

    // Чтение датчика каждые 2 секунды без блокировки основного цикла
    if (millis() - last_sensor_time >= sensor_interval) {
        last_sensor_time = millis();

        sensors_event_t humidity, temp;
        // Запрашиваем данные, только если датчик был успешно запущен
        if (sht4.getEvent(&humidity, &temp)) {
            Serial.print("Температура: ");
            Serial.print(temp.temperature);
            Serial.println(" °C");

            Serial.print("Влажность: ");
            Serial.print(humidity.relative_humidity);
            Serial.println(" %");
        }
    }

    delay(1); 
}
