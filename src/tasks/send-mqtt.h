#ifndef TASK_MQTT
#define TASK_MQTT

//***********************************
//************* LIBRAIRIES ESP
//***********************************
#include <Arduino.h>
#include <WiFiClientSecure.h>
#ifndef LIGHT_FIRMWARE
  #include <PubSubClient.h>
#endif

//***********************************
//************* PROGRAMME PVROUTER
//***********************************
#include "../config/config.h"
#include "../config/enums.h"
#include "mqtt-home-assistant.h"
#ifndef LIGHT_FIRMWARE
  #include "functions/Mqtt_http_Functions.h"
#endif
#include "functions/energyFunctions.h"

//***********************************
//************* Variable externes
//***********************************
extern DisplayValues gDisplayValues;
extern Config config;
extern Configmodule configmodule; 
extern Logs Logging;
extern Mqtt configmqtt;
extern Memory task_mem;
extern SemaphoreHandle_t mutex; 

#ifndef LIGHT_FIRMWARE
  extern PubSubClient client;
  extern HA device_routeur; 
  extern HA device_grid; 
  extern HA device_routed;
  extern HA device_dimmer;
  extern HA device_inject; 
  extern HA compteur_inject;
  extern HA compteur_grid;
  extern HA temperature_HA;
  extern HA power_factor;
  extern HA power_apparent;
  extern HA switch_relay1;
  extern HA switch_relay2;
  extern HA device_dimmer_boost;
  extern HA device_dimmer_alarm_temp;
  extern HA switch_dimmerlocal;
  extern HA device_dimmer_power;
  extern Programme programme_marche_forcee;
  extern Dallas dallas;
  extern gestion_puissance unified_dimmer;
#endif

//***********************************
//************* Variable locales
//***********************************
int Pow_mqtt_send = 0;
float WHtempgrid;
float WHtempinject;
long beforetime; 
#define timemilli 3.6e+6 

//***********************************
//************* send_to_mqtt
//***********************************
void send_to_mqtt(void * parameter) { // NOSONAR
  const TickType_t mqtt_timeout = pdMS_TO_TICKS(10000); // 10s timeout pour mutex
  
  for (;;) {

    if (!WiFi.isConnected()) {
      vTaskDelay(10*1000 / portTICK_PERIOD_MS);
      continue;
    }

    #ifndef LIGHT_FIRMWARE
    
    // Reconnexion MQTT si déconnecté avec timeout
    if (config.mqtt && (WiFi.status() == WL_CONNECTED)) {
      if (!client.connected()) {
        unsigned long startTime = millis();
        Serial.println("MQTT déconnecté, tentative de reconnexion...");
        reconnect();
        
        if (millis() - startTime > 10000) {
          Serial.println("⚠️ MQTT timeout reconnexion, passage au cycle suivant");
          vTaskDelay(pdMS_TO_TICKS(5000));
          continue;
        }
      }

      if (client.connected()) {
        long start = millis();
        
        #if WIFI_ACTIVE == true
          
          // Prise du mutex avec timeout
          if (xSemaphoreTake(mutex, mqtt_timeout) == pdTRUE) {
            
            // ===== APPEL SYSTÉMATIQUE DE client.loop() =====
            // Pour traiter les messages entrants (commandes HA)
            unsigned long loopStart = millis();
            client.loop();
            unsigned long loopDuration = millis() - loopStart;
            
            if (loopDuration > 5000) {
              Serial.printf("⚠️ client.loop() a pris %lu ms !\n", loopDuration);
            }
            // ===============================================
            
            Pow_mqtt_send++;
            if (Pow_mqtt_send > 5) {
              long timemesure = start - beforetime;
              float wattheure = (timemesure * abs(gDisplayValues.watt) / timemilli);  
              
              // domoticz et jeedom
              if (config.IDX != 0) {
                Mqtt_send(String(config.IDX), String(int(gDisplayValues.watt)), "", "watt");  
              }
              
              if (config.IDXdallas != 0) {
                Mqtt_send(String(config.IDXdallas), String(gDisplayValues.temperature), "", "Dallas"); 
              } 
               
              // HA
              if (configmqtt.HA) {
                device_routeur.sendInt(gDisplayValues.watt);
                device_routed.sendInt(gDisplayValues.puissance_route);
                device_dimmer_power.sendInt((unified_dimmer.get_power()) * config.charge / 100);
                power_apparent.sendFloat(PVA);                        
                power_factor.sendFloat(PowerFactor);
                temperature_HA.sendFloat(gDisplayValues.temperature);
                device_dimmer.sendInt(unified_dimmer.get_power());
                switch_relay1.sendInt(digitalRead(RELAY1));
                switch_relay2.sendInt(digitalRead(RELAY2));
                device_dimmer_boost.send(stringInt(programme_marche_forcee.run));  
                switch_dimmerlocal.sendInt(config.dimmerlocal ? 1 : 0);
                
                if (dallas.security) {
                  device_dimmer_alarm_temp.send("Ballon Chaud");
                } else {
                  device_dimmer_alarm_temp.send("RAS");
                } 
              }

              // remonté énergie domoticz et jeedom
              if (gDisplayValues.watt < 0) {
                if (config.IDX != 0 && config.mqtt) {
                  Mqtt_send(String(config.IDX), String(int(-gDisplayValues.watt)), "injection", "Reseau");
                  Mqtt_send(String(config.IDX), String("0"), "grid", "Reseau");
                }
                if (configmqtt.HA) {
                  device_inject.sendInt((-gDisplayValues.watt));
                  device_grid.send("0");
                  WHtempgrid += wattheure; 
                  compteur_inject.sendFloat(WHtempgrid);
                  client.publish(("memory/" + compteur_grid.topic + compteur_grid.Get_name()).c_str(), String(WHtempgrid).c_str(), true); 
                  compteur_grid.send("0");
                }
              } else {
                if (config.IDX != 0 && config.mqtt) {
                  Mqtt_send(String(config.IDX), String("0"), "injection", "Reseau");
                  Mqtt_send(String(config.IDX), String(int(gDisplayValues.watt)), "grid", "Reseau");
                }
                if (configmqtt.HA) {
                  device_grid.sendInt((gDisplayValues.watt));
                  device_inject.send("0");
                  compteur_inject.send("0");
                  WHtempinject += wattheure;
                  compteur_grid.sendFloat(WHtempinject);
                  client.publish(("memory/" + compteur_inject.topic + compteur_inject.Get_name()).c_str(), String(WHtempinject).c_str(), true);
                }
              }
              
              beforetime = start; 
              Pow_mqtt_send = 0;
              
            } // fin if Pow_mqtt_send > 5
            
            xSemaphoreGive(mutex);
            
          } else {
            Serial.println("⚠️ Impossible d'obtenir le mutex MQTT dans les 10s");
          }

        #endif // WIFI_ACTIVE
      }
    }
    #else
    if (config.mqtt && (WiFi.status() == WL_CONNECTED)) {
      // Mode LIGHT_FIRMWARE (si applicable)
    }
    #endif

    task_mem.task_send_mqtt = uxTaskGetStackHighWaterMark(nullptr);
    vTaskDelay(pdMS_TO_TICKS(2000 + (esp_random() % 61) - 30));

  }
}

#endif
