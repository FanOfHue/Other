//#######################################################################################################
//#################################### Plugin 204: ESPNOW ###############################################
//#######################################################################################################

#define SERIALDEBUG true


/*
 * Plugin for ESPNOW support 
 * 
 * Created by FanOfHue for ESP Easy. Only tested on github nightly build dated 2049-10-27
 * Compiled on Arduino 1.8.5, using ESP8226 core 2.5.2.
 * 
 *  espnow
 * 
*/

#ifdef USES_P204
#define PLUGIN_204
#define PLUGIN_ID_204         204
#define PLUGIN_NAME_204       "ESPNOW"
#define PLUGIN_VALUENAME1_204 ""

#include <espnow.h>

boolean Plugin_204(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {
    case PLUGIN_DEVICE_ADD:
    {
      Device[++deviceCount].Number           = PLUGIN_ID_204;
      Device[deviceCount].Type               = DEVICE_TYPE_SINGLE;
      Device[deviceCount].VType              = SENSOR_TYPE_SWITCH;
      Device[deviceCount].Ports              = 0;
      Device[deviceCount].PullUpOption       = false;
      Device[deviceCount].InverseLogicOption = false;
      Device[deviceCount].FormulaOption      = true;
      Device[deviceCount].ValueCount         = 1;
      Device[deviceCount].SendDataOption     = true;
      Device[deviceCount].TimerOption        = true;
      Device[deviceCount].GlobalSyncOption   = true;
      break;
    }

    case PLUGIN_GET_DEVICENAME:
    {
      string = F(PLUGIN_NAME_204);
      break;
    }

    case PLUGIN_GET_DEVICEVALUENAMES:
    {
      strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_204));
      break;
    }

    case PLUGIN_WEBFORM_LOAD:
    {
      success = true;
      break;
    }

    case PLUGIN_WEBFORM_SAVE:
    {
      success = true;
      break;
    }
    
    case PLUGIN_WRITE:
      {
        String cmd = parseString(string, 1);

        if (cmd.equalsIgnoreCase(F("espnowConfig")))
        {
          success = true;
           String kok = parseString(string, 2);
           String key = parseString(string, 3);
           String macStr = parseString(string, 4);
           String mode = parseString(string, 5);
           byte mac[6];
           P204_parseBytes(macStr.c_str(), ':', mac, 6, 16);
           if (mode.equalsIgnoreCase(F("Sender"))){
             P204_espnowSender(kok.c_str(), key.c_str(), mac);
           }else{
             P204_espnowReceiver(kok.c_str(), key.c_str(), mac);
           }
         }

        if (cmd.equalsIgnoreCase(F("espnowAddPeer")))
        {
          success = true;
          String key = parseString(string, 2);
          String macStr = parseString(string, 3);
          byte role = parseString(string, 4).toInt();
          byte mac[6];
          P204_parseBytes(macStr.c_str(), ':', mac, 6, 16);
          P204_espnowAddPeer(key.c_str(), mac, role);
        }  

        if (cmd.equalsIgnoreCase(F("espnowSend")))
        {
          success = true;
          String msg = string.substring(11);
          P204_espnowSend(msg);
        }

        break;
      }
  }
  return success;
}


void P204_parseBytes(const char* str, char sep, byte* bytes, int maxBytes, int base) {
    for (int i = 0; i < maxBytes; i++) {
        bytes[i] = strtoul(str, NULL, base);  // Convert byte
        str = strchr(str, sep);               // Find next separator
        if (str == NULL || *str == '\0') {
            break;                            // No more separators, exit
        }
        str++;                                // Point to next character after separator
    }
}


void P204_espnowSender(const char* kok, const char* key, uint8_t* mac){
#if SERIALDEBUG
  Serial.println("ESP Sender");
  Serial.print("KOK: ");
  Serial.println(kok);
  Serial.print("Key: ");
  Serial.println(key);
  Serial.print("MAC: ");
  for(byte x=0; x <6;x++){
    Serial.print(mac[x],HEX);
    if(x != 5)
      Serial.print("-");
  }
  Serial.println();
#endif
  WiFi.forceSleepWake();
  delay(1);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  byte wifiChannel = 1; 
  wifi_set_macaddr(STATION_IF, mac);
  #if SERIALDEBUG
    Serial.print("This node STA mac: "); Serial.println(WiFi.macAddress());
  #endif
  if (esp_now_init() == 0) {
    #if SERIALDEBUG
      Serial.println("*** ESP_Now Sender init");
    #endif
    esp_now_set_self_role(ESP_NOW_ROLE_CONTROLLER);
    esp_now_set_kok((uint8_t*)kok,16);
    esp_now_register_send_cb([](uint8_t* mac, uint8_t sendStatus) {
      #if SERIALDEBUG
      Serial.printf("send_cb, send done, status = %i\n", sendStatus);
      #endif
    });
  }
}


void P204_espnowSend(String msg){
  struct __attribute__((packed)) SENSOR_DATA {
    char msg[64];
  }sensorData;
  strcpy(sensorData.msg, msg.c_str());  
  uint8_t bs[sizeof(sensorData)];
  memcpy(bs, &sensorData, sizeof(sensorData));
  esp_now_send(NULL, bs, sizeof(sensorData)); // NULL means send to all peers
}


void P204_espnowAddPeer(const char* key, uint8_t* mac, byte role){
#if SERIALDEBUG
  Serial.println("Add Peer");
  Serial.print("Key: ");
  Serial.println(key);
  Serial.print("MAC: ");
  for(byte x=0; x <6;x++){
    Serial.print(mac[x],HEX);
    if(x != 5)
      Serial.print("-");
  }
  Serial.println();
#endif
  byte wifiChannel = 1;
  if(role == 0){ 
    #if SERIALDEBUG
      Serial.println("PEER SLAVE");
    #endif
    esp_now_add_peer(mac, ESP_NOW_ROLE_SLAVE, wifiChannel, (uint8_t*)key, 16);
  }
  else
  {
    #if SERIALDEBUG
      Serial.println("PEER CONTROLLER");
    #endif
    esp_now_add_peer(mac, ESP_NOW_ROLE_CONTROLLER, wifiChannel, (uint8_t*)key, 16);
  }
}


void P204_espnowReceiver(const char* kok, const char* key, uint8_t* mac){
#if SERIALDEBUG
  Serial.println("ESP Receiver");
  Serial.print("KOK: ");
  Serial.println(kok);
  Serial.print("Key: ");
  Serial.println(key);
  Serial.print("MAC: ");
  for(byte x=0; x <6;x++){
    Serial.print(mac[x],HEX);
    if(x != 5)
      Serial.print("-");
  }
  Serial.println();
#endif  

  WiFi.forceSleepWake();
  delay(1);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  byte wifiChannel = 1; 

  wifi_set_macaddr(STATION_IF, mac);
  #if SERIALDEBUG
    Serial.print("This node STA mac: "); Serial.println(WiFi.macAddress());
  #endif
    if (esp_now_init()==0) {
      #if SERIALDEBUG
        Serial.println("*** ESP_Now receiver init");
      #endif
      esp_now_set_self_role(ESP_NOW_ROLE_SLAVE);
      esp_now_set_kok((uint8_t*)kok,16);
      esp_now_register_recv_cb([](uint8_t *mac, uint8_t *data, uint8_t len) {
      struct __attribute__((packed)) SENSOR_DATA {
        char msg[64];
      }sensorData;
        if(esp_now_is_peer_exist(mac)){
          String eventString = F("Event ESPNOW#");
          eventString += mac[5];
          eventString += "=";
          //Serial.write(mac, 6); // mac address of remote ESP-Now device
          //Serial.write(len);
          memcpy(&sensorData, data, sizeof(sensorData));
          //Serial.write(data, len);
          eventString += sensorData.msg;
          Serial.println(eventString);
        }
        else
        {
          #if SERIALDEBUG
            Serial.print("Unknown MAC: ");
            for(byte x=0; x <6;x++){
              Serial.print(mac[x],HEX);
              if(x != 5)
                Serial.print("-");
            }
            Serial.println();
          #endif
        }
        
      });
    }
}
#endif
