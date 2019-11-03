//#######################################################################################################
//#################################### Plugin 201: TUYA #################################################
//#######################################################################################################

/*
 * Plugin for LCS PIR or Doorsensor running ESP Easy firmware. 
 * 
 * Created by FanOfHue for ESP Easy. Only tested on github nightly build dated 2019-10-27
 * Compiled on Arduino 1.8.5, using ESP8226 core 2.5.2.
 * Compile settings:
 *   board      : generic ESP8266
 *   flash mode : DIO
 *   flash size : 2MB (256K SPIFFS)
 * 
 * 
 * First disable serial on advanced tab and set serial logging to none!
 * Add the Tuya device on the devices tab, no special settings, just add and enable
 * 
 * Add following to the rules section: (sample for domoticz)

    on Wifi#Connected do
      tuyacheck
    endon
    on tuya#event=0 do
      sendToHTTP 192.168.0.28,8080,/json.htm?type=command&param=switchlight&idx=393&switchcmd=Off
    endon
    on tuya#event=1 do
      sendToHTTP 192.168.0.28,8080,/json.htm?type=command&param=switchlight&idx=393&switchcmd=On
    endon

 * 
 * Note that the device will only be active for about 1 second after the tuya#event, so rule processing must be quick!
*/

#ifdef USES_P201
#define PLUGIN_201
#define PLUGIN_ID_201         201
#define PLUGIN_NAME_201       "Tuya PIR/Doorsensor"
#define PLUGIN_VALUENAME1_201 "State"

boolean Plugin_201(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;

  switch (function)
  {
    case PLUGIN_DEVICE_ADD:
    {
      Device[++deviceCount].Number           = PLUGIN_ID_201;
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
      string = F(PLUGIN_NAME_201);
      break;
    }

    case PLUGIN_GET_DEVICEVALUENAMES:
    {
      strcpy_P(ExtraTaskSettings.TaskDeviceValueNames[0], PSTR(PLUGIN_VALUENAME1_201));
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
        if (cmd.equalsIgnoreCase(F("tuyaCheck")))
        {
          success = true;
          Serial.begin(9600);
          int8_t status1 = -1;
          int8_t status2 = -1;
          int8_t status3 = -1;
          int8_t status4 = -1;
          status1 = P201_tuyaSend(1,0,0);
          status2 = P201_tuyaSend(2,1,2);
          status3 = P201_tuyaSend(2,1,3);
          if(status2 < 64){ // only continue when the device was not started using the device 'connect' button
            status4 = P201_tuyaSend(2,1,4);
            Serial.print("\nTuya event:");
            Serial.println((status4 & 4) >> 2);
            if(status4 & 2){
                String eventString = F("TUYA#Event=");
                eventString += ((status4 & 4) >> 2);
                rulesProcessing(eventString);
            }
            if(status4 & 32){
                String eventString = F("TUYA#BatteryLow=1");
                rulesProcessing(eventString);
            }
            Serial.println("Wait for power off!");
            delay(5000);
          }
        }
        break;
      }
  }
  return success;
}


byte P201_tuyaSend(byte cmd, byte len, byte value)
{
  while (Serial.available())
    Serial.read();

  Serial.write(0x55);
  Serial.write(0xAA);
  Serial.write(0x00);
  Serial.write(cmd);
  Serial.write(0x00);
  Serial.write(len);
  if(len == 1)
    Serial.write(value);
  byte cs = 0xff + cmd + len + value;
  Serial.write(cs);
  Serial.flush();

  // wait max 500 mSec for reply
  for (int x = 0; x < 500; x++){
    if (Serial.available())
      break;
    delay(1);
  }
  
  byte count = 0;       // count number of bytes received within a message
  byte msgType = 0;     // stores message type (byte 4)
  byte length = 0;      // length of entire message as provided by MCU
  byte devState = 0;    // subtype ?
  byte event = 255;     // event state of device, like door open/close
  byte status = 0;      // return value
    //bit 0   0 = no reply (all other bits should be zero too), 1 = reply received
    //bit 1   0 = no event, 1 = event (like open/close, pir move)
    //bit 2   event state, 0 = door open or PIR detect, 1 = door closed
    //bit 3   reserved
    //bit 4   reserved
    //bit 5   battery LOW, not in use, need to investigate protocol
    //bit 6   device button pressed to start device
    //bit 7   not used
     
    // MCU replies
    // 55 aa 0 2 0 0 1  basic confirm
    // 55 aa 0 3 0 0 2  this is received only when the sensor was activated using 5 seconds button press, additional reply after tuyasend 2,1,2

    // Status messages from door sensor:
    // 55 aa 0 5 0 5 1 1 0 1 1 d  door open
    // 55 aa 0 5 0 5 1 1 0 1 0 c  door closed
    // 55 aa 0 5 0 5 3 4 0 1 2 13 battery OK
    // 55 aa 0 5 0 5 3 4 0 1 0 11 battery low (around 2.5 Volts, DoorSensor keeps working until 2.3 Volts)

    // 55 aa 0 5 0 5 1 4 0 1 0 f  PIR
  
  // process reply
  while (Serial.available()){
    count++;
    byte data = Serial.read();

    if(count == 4) // byte 4 contains message type
      msgType = data;
    if(count == 6) // byte 6 contains data length
      length = data;
    
    if(msgType == 5){ // msg type 5 is a status message
      if (count == 7){ // byte 7 contains status indicator
        devState = data;
      }
      if(count == 11) // byte 11 status indicator value
        event = data;
    }

    // Check if message is complete, based on msg length (header + checksum = 7 bytes)
    if(count == length + 7){
      
      // We have a complete message, start calculating return value
      
      status |= 1; //set bit 1 to confirm valid response
      
      if(msgType == 3){
        status |= 64; // set bit 6, sensor button pressed for 5 seconds
      }

      if(msgType == 5 && devState == 1){ // msg type 5 is a status message, devState 1 indicates the open/close state
        status |= 2; // set bit 2, event received
        if(event)
          status |= 4; //set event state bit 3
      }

      if(msgType == 5 && devState == 3){ // msg type 5 is a status message, devState 3 battery info
        if(event == 0)
          status |= 32; //set battery low bit 5
      }
      
      count = 0;
      length = 0;
      msgType = 0;
      devState = 0;
    }
    if(!Serial.available())
      delay(2); // wait for next serial char, timing based on 9600 baud
  }

  return status;

}
#endif

