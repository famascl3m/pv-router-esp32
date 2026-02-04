#ifndef TASK_WIFI_CONNECTION
#define TASK_WIFI_CONNECTION

//***********************************
//************* LIBRAIRIES ESP
//***********************************
#include <Arduino.h>
#include "WiFi.h"

//***********************************
//************* PROGRAMME PVROUTER
//***********************************
#include "../config/enums.h"
#include "../config/config.h"
#include "../functions/WifiFunctions.h"

//***********************************
//************* Variables externes
//***********************************
extern DisplayValues gDisplayValues;
extern Configwifi configwifi; 
extern Logs logging; 
extern Memory task_mem; 

//***********************************
//************* Déclaration de fonctions
//***********************************
extern void goToDeepSleep();

//***********************************
//************* keepWiFiAlive
//***********************************
void keepWiFiAlive(void * parameter) { // NOSONAR
  /**
     * Task: monitor the WiFi connection and keep it alive!
     * 
     * When a WiFi connection is established, this task will check it every 30 seconds 
     * to make sure it's still alive.
     * 
     * If not, a reconnect is attempted. If this fails to finish within the timeout,
     * the ESP32 is send to deep sleep in an attempt to recover from this.
  */
  for(;;) {  
    if(WiFi.status() == WL_CONNECTED) {
      if (AP) { 
        search_wifi_ssid(); 
       }
       vTaskDelay(pdMS_TO_TICKS(30000));
       continue;
    }

    serial_println(F("[WIFI] Connecting"));
    gDisplayValues.currentState = DEVICE_STATE::CONNECTING_WIFI;

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(DEVICE_NAME);
    WiFi.begin(configwifi.SID, configwifi.passwd); 
      
    unsigned long startAttemptTime = millis();

    // Keep looping while we're not connected and haven't reached the timeout
    while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < WIFI_TIMEOUT) {        
    } // NOSONAR

    // Make sure that we're actually connected, otherwise go to deep sleep
    if(WiFi.status() != WL_CONNECTED) {
      serial_println(F("[WIFI] FAILED"));
             
      logging.Set_log_init(Wifi_disconnected,true);
      vTaskDelay(WIFI_RECOVER_TIME_MS / portTICK_PERIOD_MS);
    }

    serial_print(F(Wifi_reconnected));
         
    logging.Set_log_init(Wifi_reconnected,true);
    serial_println(WiFi.localIP());
    serial_print("force du signal:");
    serial_print(WiFi.RSSI());
    serial_println("dBm");
    gDisplayValues.currentState = DEVICE_STATE::UP;
    gDisplayValues.IP = WiFi.localIP();
    btStop();
  } // for
}

//***********************************
//************* keepWiFiAlive2
/// @brief  task qui permet de rechercher le wifi configuré en cas de passage en mode AP et reboot si trouvé
/// @param parameter 
//***********************************
void keepWiFiAlive2(void *pvParameters) {
    const TickType_t xDelay = pdMS_TO_TICKS(30000); // Vérifier toutes les 30s
    int failCount = 0;
    unsigned long lastConnectAttempt = 0;
    
    for(;;) {
        // Si on n'est pas en mode AP
        if (!AP) {
            // Vérifier la connexion WiFi
            if (WiFi.status() != WL_CONNECTED) {
                failCount++;
                
                unsigned long now = millis();
                
                // Logger toutes les 5 tentatives (2min30)
                if (failCount % 5 == 0) {
                    char logMsg[100];
                    int minutes = (failCount * 30) / 60;
                    snprintf(logMsg, sizeof(logMsg), "WiFi déconnecté depuis %d tentatives (%dmin)\n", failCount, minutes);
                    logging.Set_log_init(logMsg, true);
                    Serial.printf("RSSI avant perte: %d dBm\n", WiFi.RSSI());
                }
                
                // Après 20 tentatives (10 minutes), on redémarre
                if (failCount >= 20) {
                    logging.Set_log_init("WiFi perdu depuis 10min, redémarrage\n", true);
                    savelogs("-- reboot après 10min sans WiFi --");
                    delay(1000);
                    ESP.restart();
                }
                
                // Ne tenter de reconnecter que toutes les 5 secondes minimum
                if (now - lastConnectAttempt > 5000) {
                    Serial.println("keepWiFiAlive2: Tentative de reconnexion WiFi...");
                    
                    // Déconnexion propre
                    WiFi.disconnect(false, false);
                    delay(100);
                    
                    // Reconnexion
                    WiFi.begin(configwifi.SID, configwifi.passwd);
                    lastConnectAttempt = now;
                }
                
            } else {
                // WiFi connecté, reset du compteur
                if (failCount > 0) {
                    char logMsg[100];
                    snprintf(logMsg, sizeof(logMsg), "WiFi reconnecté après %d tentatives (RSSI: %d dBm)\n", 
                             failCount, WiFi.RSSI());
                    logging.Set_log_init(logMsg, true);
                    Serial.println(logMsg);
                    failCount = 0;
                }
                
                // Vérifier la qualité du signal
                int rssi = WiFi.RSSI();
                if (rssi < -85) {
                    Serial.printf("⚠️ Signal WiFi très faible: %d dBm\n", rssi);
                }
            }
        }
        
        task_mem.task_keepWiFiAlive2 = uxTaskGetStackHighWaterMark(nullptr);
        vTaskDelay(xDelay);
    }
}





#endif
