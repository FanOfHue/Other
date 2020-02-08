//#######################################################################################################
//#################################### Plugin 252: Philips Hue Bridge  ##################################
//#######################################################################################################

// This is an unofficial draft version plugin and travels without (or very limited) support ;-)
// It has only been tested on an ESP32 module using SSL connection to a Philips Hue Bridge type 2 with firmware 1809121051

// This plugin connects to a Philips Hue bridge using the provided API
// The plugin collects data from the Hue bridge and stores it into an internal data stucture
// Using a task you can get sensor values or light states into ESP Easy to be used as a dummy sensor
// The plugin will also check the entire collection of sensors and lights and will produce a rule event on any change
// (so not only the 4 values in the task are monitored, but all sensors and lights)

// The rule events created will use the ESP system name, taskname and the Hue Bridge provided sensor name like this:
//   ESP32/Temp-Outside/Hue temperature sensor 3=11.22

// The plugin can also be used to control a light device using this syntax:
//   setHue <type>/<id>/state,<json message>
// A sample to turn on light with ID 4 with a dimlevel 175:
//   setHue lights/4/state,{"on":true, "bri":175}
// When using these commands, a task needs to be setup at least for the API host, port and key

#ifdef USES_P252
#define PLUGIN_252
#define PLUGIN_ID_252         252
#define PLUGIN_NAME_252       "Hue"
#define PLUGIN_VALUENAME1_252 "Hue"

#define PLUGIN_252_SENSORLISTMAX 64 // Buffer to store status of lights and sensors, increase when needed

#if defined(ESP32)
  #include <WiFiClientSecure.h>
WiFiClientSecure P252_client;
#else
  WiFiClient P252_client;
#endif

// This struct is to store custom configuration for this task
struct P252_settingsStruct
{
  char host[26];                                   // IP address of Hue Bridge
  int port;                                        // Port (443)
  char key[80];                                    // This is the API key for the interface
  byte deviceType[4];                              // 1 for sensors, 2 for lights
  int deviceID[4];                                 // ID as used by the Hue Bridge
};

// This struct is to store the live status of lights and sensors
struct P252_deviceStruct
{
  int  internalid;                                 // calculated ID, bases on type and id
  byte group;                                      // 1 for sensors, 2 for lights
  byte id;                                         // ID as used by the Hue Bridge
  String name;                                     // Name as provided by API
  float value;                                     // Sensor value or light status (On = 1, Off = 0)
  boolean updateFlag = false;                      // True if this value has changed after last API poll
} P252_devices[PLUGIN_252_SENSORLISTMAX];

byte P252_deviceCount;                             // Number of collected lights and sensors
boolean P252_init = false;                         // state of init
boolean P252_update = false;                       // true if any change on the last API poll

int P252_connectCount = 0;

boolean Plugin_252(byte function, struct EventStruct *event, String& string)
{
  boolean success = false;
  static byte bootCount = 0;                       // Used to create a delay at boot

  switch (function)
  {
    case PLUGIN_DEVICE_ADD:
      {
        Device[++deviceCount].Number = PLUGIN_ID_252;
        Device[deviceCount].Type = DEVICE_TYPE_DUMMY;
        Device[deviceCount].VType = SENSOR_TYPE_SINGLE;
        Device[deviceCount].Ports = 0;
        Device[deviceCount].PullUpOption = false;
        Device[deviceCount].InverseLogicOption = false;
        Device[deviceCount].FormulaOption = true;
        Device[deviceCount].DecimalsOnly = true;
        Device[deviceCount].ValueCount = 4;
        Device[deviceCount].SendDataOption = true;
        Device[deviceCount].TimerOption = true;
        Device[deviceCount].GlobalSyncOption = true;
        break;
      }

    case PLUGIN_GET_DEVICENAME:
      {
        string = F(PLUGIN_NAME_252);
        break;
      }

    case PLUGIN_WEBFORM_LOAD:
      {
        // Load the custom task settings
        P252_settingsStruct settings;
        LoadCustomTaskSettings(event->TaskIndex, (byte*)&settings, sizeof(settings));

        // add custom form controls
        addFormTextBox( F("Host"), F("huehost"), settings.host, 26);
        addFormNumericBox( F("Port"), F("hueport"), settings.port, 0, 65535);
        addFormTextBox( F("API key"), F("huekey"), settings.key, 81);

        // The active list of sensors is based on the global data struct
        int resultsOptionValues[PLUGIN_252_SENSORLISTMAX + 1];
        String resultsOptions[PLUGIN_252_SENSORLISTMAX + 1];
        byte count = 1;
        resultsOptionValues[0] = 0;
        resultsOptions[0] = "";
        for (byte x = 1; x < P252_deviceCount; x++) {
          resultsOptionValues[x] = P252_devices[x].internalid;
          switch (P252_devices[x].group)
          {
            case 1: {
                resultsOptions[x] = "Sensor - ";
                break;
              }
            case 2: {
                resultsOptions[x] = "Light - ";
                break;
              }
          }
          resultsOptions[x] += P252_devices[x].name;
          count++;
        }

        addFormSelector(F("Sensor"), F("plugin_252_devid1"), count, resultsOptions, resultsOptionValues, settings.deviceID[0]);
        addFormSelector(F("Sensor"), F("plugin_252_devid2"), count, resultsOptions, resultsOptionValues, settings.deviceID[1]);
        addFormSelector(F("Sensor"), F("plugin_252_devid3"), count, resultsOptions, resultsOptionValues, settings.deviceID[2]);
        addFormSelector(F("Sensor"), F("plugin_252_devid4"), count, resultsOptions, resultsOptionValues, settings.deviceID[3]);

        success = true;
        break;
      }

    case PLUGIN_WEBFORM_SAVE:
      {
        // prepare for custom settings
        P252_settingsStruct settings;

        // retrieve the form data
        String huehost = WebServer.arg(F("huehost"));
        strncpy(settings.host, huehost.c_str(), sizeof(settings.host));

        String hueport = WebServer.arg(F("hueport"));
        settings.port = hueport.toInt();

        String huekey = WebServer.arg(F("huekey"));
        strncpy(settings.key, huekey.c_str(), sizeof(settings.key));

        settings.deviceID[0] = getFormItemInt(F("plugin_252_devid1"));
        settings.deviceID[1] = getFormItemInt(F("plugin_252_devid2"));
        settings.deviceID[2] = getFormItemInt(F("plugin_252_devid3"));
        settings.deviceID[3] = getFormItemInt(F("plugin_252_devid4"));

        // Save the custom settings
        SaveCustomTaskSettings(event->TaskIndex, (byte*)&settings, sizeof(settings));
        success = true;
        break;
      }

    case PLUGIN_INIT:
      {
        success = true;
        break;
      }

    case PLUGIN_WRITE:
      {
        String command = parseString(string, 1);
        // Send commands to Hue Bridge to control lights
        if (command == F("sethue")) {
          P252_setHue(string);
          success = true;
        }

        if (command == F("tsthue")) {
          P252_getHue();
          P252_setHue(string);
          success = true;
        }

        // List data struct for debugging purposes
        if (command == F("listhue")) {
          P252_listHue(string);
          success = true;
        }

        // For debugging only, force manual update
        if (command == F("updatehue")) {
          P252_updateAllHue();
          success = true;
        }
        break;
      }

    case PLUGIN_TEN_PER_SECOND:
      {
        break;
      }

    case PLUGIN_ONCE_A_SECOND:
      {
        // Boot delay, update after 15 seconds, i think this needs more intelligent approach
        // got issues and crashes without it.
        if (bootCount < 30)
          bootCount++;
        if (bootCount == 15)
          P252_updateAllHue();

        // if there was a value update during the READ update poll, it will be processed here.
        if (P252_update) {
          LoadTaskSettings(event->TaskIndex);
          for (byte x = 0; x < PLUGIN_252_SENSORLISTMAX - 1; x++) {
            if (P252_devices[x].updateFlag) {

              // experimental "Message bus", somewhat like MQTT but with UDP broadcast, no broker needed. Local LAN only
              String MSGBus = Settings.Name;
              MSGBus += "/";
              MSGBus += ExtraTaskSettings.TaskDeviceName;
              MSGBus += "/";
              MSGBus += P252_devices[x].name;
              MSGBus += "=";
              MSGBus += P252_devices[x].value;
              IPAddress remoteNodeIP = {255, 255, 255, 255};
              portUDP.beginPacket(remoteNodeIP, 65501);
              portUDP.write((uint8_t*)MSGBus.c_str(), MSGBus.length());
              portUDP.endPacket();
              delay(10);

              // Create rule events on any change on the Hue Bridge for lights and sensors
              String eventString = ExtraTaskSettings.TaskDeviceName;
              eventString += F("#");
              eventString += P252_devices[x].name;
              eventString += F("=");
              eventString += P252_devices[x].value;
              rulesProcessing(eventString);
              P252_devices[x].updateFlag = false;
            }
          }
          P252_update = false;
        }

        break;
      }

    case PLUGIN_READ:
      {
        // No wifi during init fase, so using a small boot delay before we run this
        if (bootCount >= 30) {
          P252_updateAllHue();

          P252_settingsStruct settings;
          LoadCustomTaskSettings(event->TaskIndex, (byte*)&settings, sizeof(settings));
          for (byte varNr = 0; varNr < 4; varNr++)
          {
            if (settings.deviceID[varNr] != 0) {
              byte id = 0;
              byte group = 0;
              float value = 0;
              for (byte x = 1; x < P252_deviceCount; x++) {
                if (settings.deviceID[varNr] == P252_devices[x].internalid) {
                  id = P252_devices[x].id;
                  group = P252_devices[x].group;
                  value = P252_devices[x].value;
                  break;
                }
              }
              UserVar[(VARS_PER_TASK * event->TaskIndex) + varNr] = value;
              success = true;
            }
          }
        }
        break;
      }
  }
  return success;
}


//*********************************************************************************************
// This function gets a single value, but it is not used ATM
//*********************************************************************************************
void P252_getHue() {

  // find the task to get the Hue Bridge config. The task has to be enabled for this function
  byte index = 255;
  for (byte y = 0; y < TASKS_MAX; y++)
    if (Settings.TaskDeviceEnabled[y] && Settings.TaskDeviceNumber[y] == PLUGIN_ID_252) {
      index = y;
      break;
    }
  if (index == 255)
    return;

  // Get the custom task configuration
  P252_settingsStruct settings;
  LoadCustomTaskSettings(index, (byte*)&settings, sizeof(settings));

  String path = F("/api/");
  path += settings.key;
  path += "/lights/1";

  Serial.println("GH start");
  // Try to connect to the Hue Bridge
  if (!P252_client.connected()) {
    Serial.println("GH Connecting!");
    P252_connectCount++;
    P252_client.connect(settings.host, settings.port);
  }

  if (P252_client.connected())
  {
    P252_client.print(F("GET "));
    P252_client.print(path);
    P252_client.println(F(" HTTP/1.1"));
    P252_client.println(F("Connection: keep-alive"));
    P252_client.print(F("Host: "));
    P252_client.println(settings.host);
    P252_client.println();

    unsigned long timer = millis() + 200;
    while (!P252_client.available() && millis() < timer)
      delay(1);

    // Wait for reply header
    boolean header = true;
    while (P252_client.available() && header) {
      String line = P252_client.readStringUntil('\n');
      if (line.substring(0, 15) == F("HTTP/1.1 200 OK"))
        addLog(LOG_LEVEL_INFO, line);
      if (line.length() <= 1)
        header = false;
      delay(1);
    }

    String reply = "";
    while (P252_client.available()) {
      char chr = P252_client.read();
      reply += chr;
      yield();
    }
    Serial.println("GH finished");
  }
}


//*********************************************************************************************
// This function sends a put command to the Hue Bridge to control lights with json msg
//*********************************************************************************************
void P252_setHue(String strLine) {

  boolean success = false;
  byte attempts = 0;

  // find the task to get the Hue Bridge config. The task does not need to be enabled for this function
  byte index = 255;
  for (byte y = 0; y < TASKS_MAX; y++)
    if (Settings.TaskDeviceNumber[y] == PLUGIN_ID_252) {
      index = y;
      break;
    }
  if (index == 255)
    return;

  // Get the custom task configuration
  P252_settingsStruct settings;
  LoadCustomTaskSettings(index, (byte*)&settings, sizeof(settings));

  // Construct the API request
  int pos = strLine.indexOf(",");
  String subpath = strLine.substring(7, pos);
  String msgs = strLine.substring(pos + 1);
  String path = "/api/";
  path += settings.key;
  path += "/";
  path += subpath;

  String log = F("SetHue Connecting to ");
  log += settings.host;
  log += " path: ";
  log += path;
  addLog(LOG_LEVEL_DEBUG, log);

  while (success == false && attempts < 3)
  {
    attempts++;
    // Try to connect to the Hue Bridge
    if (!P252_client.connected()) {
      P252_connectCount++;
      P252_client.connect(settings.host, settings.port);
    }

    if (P252_client.connected())
    {
      P252_client.print(F("PUT "));
      P252_client.print(path);
      P252_client.println(F(" HTTP/1.1"));
      P252_client.println(F("Connection: keep-alive"));
      P252_client.print(F("Host: "));
      P252_client.println(settings.host);
      P252_client.println(F("Content-Type: text/plain;charset=UTF-8"));
      P252_client.print(F("Content-Length: "));
      P252_client.println(msgs.length());
      P252_client.println();
      P252_client.println(msgs);

      unsigned long timer = millis() + 200;
      while (!P252_client.available() && millis() < timer)
        delay(1);

      // Wait for reply header
      boolean header = true;
      while (P252_client.available() && header) {
        String line = P252_client.readStringUntil('\n');
        if (line.substring(0, 15) == F("HTTP/1.1 200 OK"))
          addLog(LOG_LEVEL_INFO, line);
        if (line.length() <= 1)
          header = false;
        yield();
      }

      String body = "";
      body.reserve(80);
      while (P252_client.available()) {
        char chr = P252_client.read();
        body += chr;
        yield();
      }
      int pos = body.indexOf('success');
      if (pos != -1) {
        String log = F("SetHue Successful, attempts: ");
        log += attempts;
        addLog(LOG_LEVEL_DEBUG, log);
        success = true;
      }
    }
  }
}


//*********************************************************************************************
// This function updates all groups
//*********************************************************************************************
void P252_updateAllHue() {
  P252_updateHue(1, "sensors");
  P252_updateHue(2, "lights");
}


//*********************************************************************************************
// This function collects all sensors or light devices from the Hue Bridge and updates the data struct
//*********************************************************************************************
void P252_updateHue(byte groupid, String group) {

  // find the task to get the Hue Bridge config. The task has to be enabled for this function
  byte index = 255;
  for (byte y = 0; y < TASKS_MAX; y++)
    if (Settings.TaskDeviceEnabled[y] && Settings.TaskDeviceNumber[y] == PLUGIN_ID_252) {
      index = y;
      break;
    }
  if (index == 255)
    return;

  // Get the custom task configuration
  P252_settingsStruct settings;
  LoadCustomTaskSettings(index, (byte*)&settings, sizeof(settings));

  // Construct the API request
  String path = F("/api/");
  path += settings.key;
  path += "/";
  path += group;

  String log = F("UpdateHue Connecting to ");
  log += settings.host;
  log += " path: ";
  log += path;
  addLog(LOG_LEVEL_INFO, log);

  if (!P252_client.connected()) {
    P252_connectCount++;
    P252_client.connect(settings.host, settings.port);
  }

  if (P252_client.connected())
  {
    P252_init = true;
    P252_client.print(F("GET "));
    P252_client.print(path);
    P252_client.println(F(" HTTP/1.1"));
    P252_client.println(F("Connection: keep-alive"));
    P252_client.print(F("Host: "));
    P252_client.println(settings.host);
    P252_client.println();

    unsigned long timer = millis() + 200;
    while (!P252_client.available() && millis() < timer)
      delay(1);

    // Wait for reply header
    boolean header = true;
    while (P252_client.available() && header) {
      String line = P252_client.readStringUntil('\n');
      if (line.substring(0, 15) == F("HTTP/1.1 200 OK"))
        addLog(LOG_LEVEL_DEBUG, line);
      if (line.length() <= 1)
        header = false;
      delay(1);
    }


    // Get the body and process the json data
    String root;  // this will be used for the root elements
    String child; // this will be used for the child element of a root, just the entire json block for this root element
    byte level = 0;
    while (P252_client.available()) {
      char chr = P252_client.read();
      if (chr == '{') {
        level++;
      }
      if (chr == '}') {
        level--;
        if (level == 1) {
          int pos = root.indexOf('"');
          if (pos != -1) {
            root = root.substring(pos + 1 );
            pos = root.indexOf('"');
            root = root.substring(0, pos);
          }
          byte id = root.toInt();

          // Get the name and type of this device
          String name = findProperty(child, "name");
          String type = findProperty(child, "type");

          // Get the property name, based on device type
          String param = P252_getParameter(type);
          if (param.length() != 0) {

            // Get the sensor value
            String svalue = findProperty(child, param);
            float value = svalue.toFloat();

            // Some adjustments for some types
            if (param == "temperature") // Temperature, 21.00 degrees comes in as 2100
              value = value / 100;
            if (param == "on" || param == "presence") {
              // light devices and motion sensors send true or false, convert to float value representing on/off
              if (svalue == "true") value = 1;
              if (svalue == "false") value = 0;
            }

            // try to locate this device in the data array
            boolean exists = false;
            for (byte x = 0; x < PLUGIN_252_SENSORLISTMAX - 1; x++) {
              if (P252_devices[x].group == groupid && P252_devices[x].id == id) {
                exists = true;
                // update if the value is different
                if (P252_devices[x].value != value) {
                  P252_devices[x].value = value;
                  String log = "Changed! : ";
                  log += P252_devices[x].name;
                  log += " = ";
                  log += P252_devices[x].value;
                  addLog(LOG_LEVEL_INFO, log);
                  P252_devices[x].updateFlag = true;
                  P252_update = true;
                }
              }
            }
            if (!exists) {
              // add this device, find a free position in the array
              for (byte x = 0; x < PLUGIN_252_SENSORLISTMAX - 1; x++) {
                if (P252_devices[x].group == 0) {
                  P252_devices[x].internalid = 100 * groupid + id;
                  P252_devices[x].group = groupid;
                  P252_devices[x].id = id;
                  P252_devices[x].name = name;
                  P252_devices[x].value = value;
                  P252_deviceCount++;
                  break;
                }
              }
            }
          }
          root = "";
          child = "";
        }
      }
      if (level == 1) {
        root += chr;
      }
      if (level > 1) {
        child += chr;
      }
      yield();
    }
  }
}


//*********************************************************************************************
// This function lists the internal data struct for debugging purposes
//*********************************************************************************************
void P252_listHue(String strLine) {
  for (byte x = 0; x < P252_deviceCount; x++) {
    Serial.print(x);
    Serial.print("=");
    Serial.print(P252_devices[x].internalid);
    Serial.print(" - ");
    Serial.print(P252_devices[x].group);
    Serial.print(" - ");
    Serial.print(P252_devices[x].id);
    Serial.print(" - ");
    Serial.print(P252_devices[x].name);
    Serial.print(" - ");
    Serial.print(P252_devices[x].value);
    Serial.println();
  }
}


//*********************************************************************************************
// This function returns a given property value from the json msg
//*********************************************************************************************
String findProperty(String line, String match) {

  // parse the incoming json struct for a given property to find a match
  // property value is returned as a string
  // return empty if there's no match

  String tmpLine = "";
  int pos = line.indexOf(match); // find the start
  if (pos != -1) {
    tmpLine = line.substring(pos);
    pos = tmpLine.indexOf(","); // find the end
    if (pos != -1) {
      tmpLine = tmpLine.substring(0, pos);
      pos = tmpLine.indexOf(":"); // find the value
      if (pos != -1) {
        tmpLine = tmpLine.substring(pos + 1);
        pos = tmpLine.indexOf('"'); // find and remove the leading "
        if (pos != -1) {
          tmpLine = tmpLine.substring(pos + 1);
          pos = tmpLine.indexOf('"'); // find and remove the trailing "
          tmpLine = tmpLine.substring(0, pos);
        }
      }
    }
  }
  return tmpLine;
}


//*********************************************************************************************
// This function returns the used parameter name, based on the sensor type
//*********************************************************************************************
String P252_getParameter(String type) {
  if (type == F("ZLLTemperature")) return F("temperature");
  if (type == F("ZLLLightLevel")) return F("lightlevel");
  if (type == F("ZLLPresence")) return F("presence");
  if (type == F("CLIPGenericStatus")) return F("status");
  if (type == F("ZLLSwitch")) return F("buttonevent");
  if (type == F("Daylight")) return F("daylight");
  if (type == F("Dimmable light")) return F("on");
  if (type == F("Color temperature light")) return F("on");
  if (type == F("On/Off plug-in unit")) return F("on");
  return "";
}

#endif

