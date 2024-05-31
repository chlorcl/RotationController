#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <ESPAsyncWebServer.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "config.h"

struct GyroData {
  float x = 0;
  float y = 0;
  float z = 0;
};

enum LogLevel {
  INFO,
  WARNING,
  ERROR
};

LogLevel logLevel = ERROR;

const char *ssid = WIFI_SSID;
const char *password = WIFI_PASSWORD;

AsyncWebServer server(80);
AsyncEventSource events("/events");
Adafruit_MPU6050 mpu;
JsonDocument readings;
sensors_event_t a, g, temp;

unsigned long previousEventCall = 0;
unsigned long eventDelay = 100;

GyroData gyroData;

void getGyroReadings() {
  mpu.getEvent(&a, &g, &temp);

  if (abs(g.gyro.x) > X_AXIS_ERROR) {
    gyroData.x += g.gyro.x * (eventDelay / 1000.0);
  }

  if (abs( g.gyro.y) > Y_AXIS_ERROR) {
    gyroData.y +=  g.gyro.y * (eventDelay / 1000.0);
  }

  if (abs(g.gyro.z) > Z_AXIS_ERROR) {
    gyroData.z += g.gyro.z* (eventDelay / 1000.0);
  }

  readings["x"] = String(gyroData.x);
  readings["y"] = String(gyroData.y);
  readings["z"] = String(gyroData.z);

  if (logLevel == INFO) {
    Serial.print("x ");
    Serial.print(g.gyro.x);
    Serial.print(" y ");
    Serial.print(g.gyro.y);
    Serial.print(" z ");
    Serial.println(g.gyro.z);
  }
}

String serializeGyroData() {
  String gyroDataAsString;
  serializeJson(readings, gyroDataAsString);
  return gyroDataAsString;
}

void handleUpload(AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
  String logmessage = "Client:" + request->client()->remoteIP().toString() + " " + request->url();
  if (logLevel == INFO)
    Serial.println(logmessage);

  if (!index) {
    logmessage = "Upload Start: " + String(filename);
    request->_tempFile = SPIFFS.open("/model.glb", "w");
    if (!request->_tempFile) {
      SPIFFS.remove("/model.glb");
      request->_tempFile = SPIFFS.open("/model.glb", "w");
    }
    if (logLevel == INFO)
      Serial.println(logmessage);
  }

  if (len) {
    request->_tempFile.write(data, len);
    logmessage = "Writing file: " + String(filename) + " index=" + String(index) + " len=" + String(len);
    if (logLevel == INFO)
      Serial.println(logmessage);
  }

  if (final) {
    logmessage = "Upload Complete: " + String(filename) + ",size: " + String(index + len);
    request->_tempFile.close();
    if (logLevel == INFO)
      Serial.println(logmessage);
    request->send(200, "text/plain", "Upload OK");
  }
}

void setup() {
  if (OVERRIDE_CORS == 1) {
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, PUT");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");
  }

  Wire.setPins(SDA_PIN, SCL_PIN);
  Wire.begin();

  Serial.begin(9600);

  if (!SPIFFS.begin(true)) {
    if (logLevel <= ERROR) {
      Serial.println("An Error has occurred while mounting SPIFFS");
    }
    return;
  } else {
    if (logLevel == INFO) {
      Serial.println("SPIFFS mounted successfully");
    }
  }

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  if (logLevel == INFO) {
    Serial.println("");
    Serial.print("Connecting to WiFi...");
  }

  while (WiFi.status() != WL_CONNECTED) {
    if (logLevel == INFO) {
      Serial.print(".");
      delay(1000);
    }
  }

  if (logLevel == INFO) {
    Serial.println("Connected to WiFi!");
    Serial.println(WiFi.localIP());
  }

  if (!mpu.begin()) {
    if (logLevel <= ERROR) {
      Serial.println("Failed to find MPU6050 chip");
    }
    while (true) {
      delay(10);
    }
  }

  if (logLevel == INFO)
    Serial.println("MPU6050 Found!");

  server.on("/reset", HTTP_GET, [](AsyncWebServerRequest *request) {
    gyroData.x = 0;
    gyroData.y = 0;
    gyroData.z = 0;
    request->send(200, "text/plain", "OK");
  });

  server.on("/uploadModel", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "text/plain", "OK");
  }, handleUpload);

  server.serveStatic("/", SPIFFS, "/");

  events.onConnect([](AsyncEventSourceClient *client) {
    if (client->lastId()) {
      if (logLevel == INFO)
        Serial.printf("Client reconnected! Last message ID that it got is: %u\n", client->lastId());
    }
    client->send("hello!", NULL, millis(), 10000);
  });

  server.addHandler(&events);
  server.begin();
}

void loop() {
  if ((millis() - previousEventCall) > eventDelay) {
    getGyroReadings();
    events.send(serializeGyroData().c_str(), "data", millis());
    previousEventCall = millis();
  }
}