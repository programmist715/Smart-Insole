#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"

MAX30105 sensor;

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

BLECharacteristic *pCharacteristic;

#define I2C_SDA 8
#define I2C_SCL 9
#define GSR_SNAP1 1    
#define GSR_SNAP2 2    
#define MERIDIAN 0    

const byte RATE_SIZE = 10;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
float beatsPerMinute = 0;
int beatAvg = 0;
int lastSentPulse = 0;

int gsrValue1 = 0;
int gsrValue2 = 0;
int gsrAverage = 0;

int meridianValue = 0;

bool seizureWarning = false;

#define PULSE_THRESHOLD 100      
#define GSR_THRESHOLD 3800       
#define MERIDIAN_THRESHOLD 300     

void measurePulse() {
  long irValue = sensor.getIR();
  
  if (checkForBeat(irValue) == true) {
    long delta = millis() - lastBeat;
    lastBeat = millis();
    
    if (delta > 0) {
      beatsPerMinute = 60.0 / (delta / 1000.0);
      
      if (beatsPerMinute < 200 && beatsPerMinute > 30) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;
        
        beatAvg = 0;
        for (byte x = 0; x < RATE_SIZE; x++) {
          beatAvg += rates[x];
        }
        beatAvg /= RATE_SIZE;
      }
    }
  }
  static unsigned long lastPulsePrint = 0;
  if (millis() - lastPulsePrint > 2000) {
    lastPulsePrint = millis();
    if (irValue < 30000) {
      Serial.println("Пульс: нет пальца");
    } else if (beatAvg > 0) {
      Serial.print("Пульс: ");
      Serial.print(beatAvg);
      Serial.println(" BPM");
    }
  }
}

void measureGSR() {
  pinMode(GSR_SNAP1, OUTPUT);
  pinMode(GSR_SNAP2, INPUT);
  digitalWrite(GSR_SNAP1, HIGH);
  delay(10);
  gsrValue1 = analogRead(GSR_SNAP2);
  
  pinMode(GSR_SNAP2, OUTPUT);
  pinMode(GSR_SNAP1, INPUT);
  digitalWrite(GSR_SNAP2, HIGH);
  delay(10);
  gsrValue2 = analogRead(GSR_SNAP1);
  
  pinMode(GSR_SNAP1, INPUT);
  pinMode(GSR_SNAP2, INPUT);
  
  gsrAverage = (gsrValue1 + gsrValue2) / 2;
  
  Serial.print(" GSR: ");
  Serial.println(gsrAverage);
}

void measureMeridian() {
  pinMode(GSR_SNAP1, OUTPUT);
  pinMode(GSR_SNAP2, OUTPUT);
  digitalWrite(GSR_SNAP1, LOW);
  digitalWrite(GSR_SNAP2, LOW);
  
  delay(10);
  meridianValue = analogRead(MERIDIAN);
  
  pinMode(GSR_SNAP1, INPUT);
  pinMode(GSR_SNAP2, INPUT);
  pinMode(MERIDIAN, INPUT);
  
  Serial.print(" Меридиан: ");
  Serial.println(meridianValue);
}

void checkSeizure() {
  bool pulseHigh = (beatAvg > PULSE_THRESHOLD);
  bool gsrHigh = (gsrAverage > GSR_THRESHOLD);
  bool meridianLow = (meridianValue < MERIDIAN_THRESHOLD);

  if (pulseHigh && gsrHigh && meridianLow) {
    seizureWarning = true;
    Serial.println(" ВНИМАНИЕ: Возможный приступ!");
  } else {
    seizureWarning = false;
  }
}

void sendData() {
  String data = String(beatAvg) + "," + 
                String(gsrAverage) + "," + 
                String(meridianValue) + "," +
                String(seizureWarning ? "1" : "0");
  
  pCharacteristic->setValue(data.c_str());
  pCharacteristic->notify();
  
  Serial.print(" Отправлено: ");
  Serial.println(data);
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  pinMode(GSR_SNAP1, INPUT);
  pinMode(GSR_SNAP2, INPUT);
  pinMode(MERIDIAN, INPUT);

  BLEDevice::init("ESP32_Insole");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  
  pCharacteristic = pService->createCharacteristic(
                      CHARACTERISTIC_UUID,
                      BLECharacteristic::PROPERTY_READ |
                      BLECharacteristic::PROPERTY_NOTIFY
                    );
  
  pCharacteristic->setValue("0");
  pService->start();
  
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
  Serial.println(" BLE запущен: ESP32_Insole");

  Wire.begin(I2C_SDA, I2C_SCL);
  
  if (!sensor.begin(Wire)) {
    Serial.println(" Датчик пульса не найден! Проверь подключение:");
    while(1);  
  }
  
  sensor.setup();
  sensor.setLEDMode(2);
  sensor.setPulseAmplitudeRed(0x1F);
  sensor.setPulseAmplitudeIR(0x1F);
  sensor.setPulseAmplitudeGreen(0);
  Serial.println("Датчик пульса готов! Приложи палец.");
  
  Serial.println("Все системы готовы!");
}

void loop() {
  measurePulse();
  measureGSR();
  measureMeridian();
  checkSeizure();
  sendData();
  
  delay(1000);
}
