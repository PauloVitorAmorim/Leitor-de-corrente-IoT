#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <mqtt_client.h>
#include <MQTT.h>
#include <EmonLib.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <DS1307.h>

#include "certificados.h"

#define DEBUG_PC false 
#define NOME_DO_DISPOSITIVO "**************"
#define ENDPOINT_MQTT "****************.iot.us-east-1.amazonaws.com"
#define MQTT_TOPICO "************"
#define LIMITE_TENTATIVA_CONEXOES 50
#define SDA_CLOCK_PIN 22
#define SCL_CLOCK_PIN 21
#define CURRENT_READ_PIN 34
#define BATERRY_READ_PIN 35

char* WIFI_NOME = "********";
char* WIFI_SENHA = "*********";


WiFiClientSecure wifi = WiFiClientSecure();
MQTTClient mqttClient = MQTTClient();

bool wifiOk = false;
bool AWSConectionOk = false;

int counter = 0;

DS1307 rtcClock;
EnergyMonitor LeitorCorrente;

void DeepSleep(int seconds){
  esp_sleep_enable_timer_wakeup(1 * seconds * 1000000L);
  esp_deep_sleep_start();
}

void ConectToWiFi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_NOME, WIFI_SENHA);

  int retries = 0;

  #if DEBUG_PC 
    Serial.print("DEBUG - INICIO DA CONEAO COM O WIFI]");
  #endif

  while(WiFi.status() != WL_CONNECTED && retries < 15){
    delay(500);

    #if DEBUG_PC
      Serial.print("TENTANDO CONECTAR AO WIFI]");
    #endif

    retries++;
  }

  if(WiFi.status() != WL_CONNECTED){

    #if DEBUG_PC
      Serial.print("ERRO - NAO FOI POSSIVEL CONECTAR COM O WIFI]");
      Serial.print("TENTANDO NOVAMENTE EM 600 SEGUNDOS...]");
    #endif

    DeepSleep(10);
    //esp_sleep_enable_timer_wakeup(1 * 60L * 1000000L);
    //esp_deep_sleep_start();
    return;
  }else{

    #if DEBUG_PC
      Serial.print("CONEXAO COM O WIFI BEM SUCEDIDA]");
    #endif

    wifiOk = true;
  }
}

void ConectToAWS(){
  wifi.setCACert(CERTIFICADO_AWS_CA);
  wifi.setCertificate(CERTIFICADO_AWS_DISPOSITIVO);
  wifi.setPrivateKey(CERTIFICADO_AWS_PRIVADO);

  mqttClient.begin(ENDPOINT_MQTT, 8883, wifi);
  int retries = 0;
  
  #if DEBUG_PC
    Serial.print("DEBUG - INICIO CONEXÃO MQTT AWS IOT ]");
  #endif

  while(!mqttClient.connect(NOME_DO_DISPOSITIVO) && retries < LIMITE_TENTATIVA_CONEXOES){
    
    #if DEBUG_PC
      Serial.print("Tentando conectar com AWS...]");
    #endif
    
    delay(100);
    retries++;
  }

  if(!mqttClient.connected()){

    #if DEBUG_PC
      Serial.println("ERRO - TIMEOUT]");
    #endif

    DeepSleep(2);
    return;
  }

  #if DEBUG_PC
    Serial.print("CONEXÃO BEM SUCEDIDA!]");
  #endif

  AWSConectionOk = true;
}

String GetDataHora(){
  uint16_t ano = 0;
  uint8_t mes = 0;
  uint8_t dia = 0;
  uint8_t hor = 0;
  uint8_t min = 0;
  uint8_t seg = 0; 
  rtcClock.get(&seg, &min, &hor, &dia, &mes, &ano);

  String ano_mes_dia = String(String(ano)+"_"+String(mes)+"_"+String(dia));
  String hor_min_seg = String(String(hor)+"_"+String(min)+"_"+String(seg));

  return String(ano_mes_dia + "U" + hor_min_seg);
}

uint16_t ReadBaterryPercentage(){
  //A tensão de 9V5 (valor lido da baterria em 100)
  //Passando pelo divisor de tensão fica 3V16
  //o valor lido é ~3921 -> 3V16 -> 100 %
  //e considerando-se valorLido / 39.21 = porcentagemBateria, 
  return analogRead(BATERRY_READ_PIN) / 39.21;
}

void SendActualPowerValueToAWS(bool atual = false){
  StaticJsonDocument<128> doc;
  String data_hora = GetDataHora();
  doc["DataHora"] = atual ? String("Atual") : data_hora;
  doc["Corrente"] = LeitorCorrente.calcIrms(1480);
  doc["BateriaPorcentagem"] = ReadBaterryPercentage();
  doc["tempo"] = data_hora;

  String output;
  serializeJson(doc, output);
  
  #if DEBUG_PC
    Serial.print('.');
    Serial.print(output);
    Serial.print(']');
  #endif

  mqttClient.publish(MQTT_TOPICO, output);
}

void initiateRTCClock(){
  #if DEBUG_PC
    Serial.print('Iniciando RTC...');
  #endif

  Wire.setPins(SDA_CLOCK_PIN, SCL_CLOCK_PIN);
  rtcClock.begin();
  
  #if DEBUG_PC
    Serial.print('RTC Iniciado');
  #endif
  
  rtcClock.start();
}

void setup() {
  Serial.begin(9600);   
  LeitorCorrente.current(CURRENT_READ_PIN, 60.606);
}

void loop() {

  #if DEBUG_PC
    for(;;){
      if(!Serial.available())
        continue;
      Serial.read();
      Serial.print("SERIAL RECEBIDA - APP NÃO INICIADO]");
      Serial.print("INICIANDO DISPOSITIVO]");
      break;
    }
  #endif

  while(!wifiOk){
    ConectToWiFi();  
  }

  while(!AWSConectionOk){
    ConectToAWS();
  }
  
  #if DEBUG_PC
    Serial.print("DISPOSITIVO OK PARA INICIAR O MONITORAMENO DO CONSUMO DA SUA RESIDENCIA!]");
  #endif
  
  initiateRTCClock();

  #if DEBUG_PC
  for(;;){
    if(!Serial.available())
      continue;
    Serial.read();
    break;
  }
  #endif
  
  for(;;){
    SendActualPowerValueToAWS();
    delay(200);
    SendActualPowerValueToAWS(true);
    delay(200);
    DeepSleep(4);
  }
}
