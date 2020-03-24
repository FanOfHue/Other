// ATTiny85 to wake the LCS Door Sensor every 10 minutes
// Digital pin 1 connected to the reed switch
// Digital pin 4 can connect to an LED for debugging state

//==============================================================================================================
// ATMEL ATTINY85 GENERIC PINOUTS (FOR REFERENCE ONLY)
//                          o-\/-+ 
//                  Reset  1|    |8  VCC 
//              D3/A3 PB3  2|    |7  PB2 D2/A1/I2C-SCL
//              D4/A2 PB4  3|    |6  PB1 D1/PWM1
//                    GND  4|    |5  PB0 D0/PWM0/I2C-SDA
//                          +----+ 
//==============================================================================================================
// PINOUTS USED IN THIS PROJECT
//                          o-\/-+ 
//                  Reset  1|    |8  VCC 
//                  D3/A3  2|    |7  SCL
//                  D4/A2  3|    |6  D1
//                    GND  4|    |5  SDA
//                          +----+ 
//==============================================================================================================

// EEPROM CONFIG:
#define EEPROM_I2C_ADDRESS 0 // I2C address (overrides default 0x26 when set to a value between 1-126)
#define EEPROM_OSC_CAL     1 // OSC calibration
#define EEPROM_VCC_CAL     2 // VCC calibration offset in mV
#define EEPROM_WAKE_TIMER  16 // Wake Timer in minutes, activates on values between 1 <> 254
// 17 for MSB in future?
#define EEPROM_WAKE_PIN    18 // Wake pin
#define EEPROM_LED_PIN     19 // LED indicator pin for sleep mode

#define CMD_DIGITAL_WRITE  1
#define CMD_DIGITAL_READ   2
#define CMD_ANALOG_WRITE   3
#define CMD_ANALOG_READ    4

#define CMD_EEPROM_WRITE   101
#define CMD_EEPROM_READ    102
#define CMD_VCC_READ       110

#define I2C_MSG_OUT_SIZE   4

#include <EEPROM.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define cbi(sfr, bit) (_SFR_BYTE(sfr) &= ~_BV(bit))
#define sbi(sfr, bit) (_SFR_BYTE(sfr) |= _BV(bit))

byte I2C_Address = 0x26;               // I2C Address default
uint8_t sendBuffer[I2C_MSG_OUT_SIZE];  // Send buffer for I2C requests
boolean state = false;                 // Toggle state for pin to doorsensor
boolean lastState = false;             // Store last state, so we can detect changes
unsigned long wakeCounter = 0;         // Times millisSeconds that we are alive when sleep enabled
boolean wake_led = false;              // State of wake mode LED indicator
int wake_led_count = 0;                // Times milliSeconds that LED is on or off
byte wake_pin = 1;                     // Wake pin, default pin 1
byte led_pin = 4;                      // LED indicator, default pin 4
boolean resetState = true;             // Indicated boot state

void setup(){
  if (EEPROM.read(EEPROM_OSC_CAL) < 255)
    OSCCAL = EEPROM.read(EEPROM_OSC_CAL);

  if (EEPROM.read(EEPROM_WAKE_PIN) != 255)
    wake_pin = EEPROM.read(EEPROM_WAKE_PIN);

  if (EEPROM.read(EEPROM_LED_PIN) != 255)
    led_pin = EEPROM.read(EEPROM_LED_PIN);

  Watchdog_setup(9); // Use WDT to sleep for 8 seconds on each call

  // Check if I2C address is configured in EEPROM, else use hardcoded default  
  byte I2C_custom = EEPROM.read(EEPROM_I2C_ADDRESS);
  if(I2C_custom > 0 && I2C_custom <= 127)
    I2C_Address = I2C_custom;

  // Start I2C slave
  TinyWireS_begin(I2C_Address);
}

void loop() {
  
  // Handle I2C requests
  if(TinyWireS_available())
    handleI2C();

  // Handle Sleep mode if enabled
  byte wake_timer = EEPROM.read(EEPROM_WAKE_TIMER);
  if(wake_timer != 0 && wake_timer != 255)
    handleSleep(wake_timer);

  // After 60 seconds, consider fresh boot state finished
  if(millis() > 60000)
    resetState = false;
    
  // This makes all counters in the loop increment each milliSec
  delay(1);
}


void handleI2C(){
  delay(1); // wait for all bytes to receive
  byte cmd = TinyWireS_receive();
  byte port = TinyWireS_receive();
  int value = TinyWireS_receive();
  value += TinyWireS_receive()*256;
  switch(cmd)
  {
  case CMD_DIGITAL_WRITE:
    {
      pinMode(port,OUTPUT);
      digitalWrite(port,value);
      break;
    }
  case CMD_DIGITAL_READ:
    {
      pinMode(port,INPUT_PULLUP);
      clearSendBuffer();
      sendBuffer[0] = digitalRead(port);
      I2CReply();
      break;
    }
  case CMD_ANALOG_WRITE:
    {
      analogWrite(port,value);
      break;
    }
  case CMD_ANALOG_READ:
    {
      clearSendBuffer();
      int valueRead = analogRead(port);
      sendBuffer[0] = valueRead & 0xff;
      sendBuffer[1] = valueRead >> 8;
      I2CReply();
      break;
    }
  case CMD_EEPROM_WRITE:
    {
      byte oldValue = EEPROM.read(port);
      if(value != oldValue)
        EEPROM.write(port,value);
      break;
    } 
  case CMD_EEPROM_READ:
    {
      clearSendBuffer();
      sendBuffer[0] = EEPROM.read(port);
      I2CReply();
      break;
    }
  case CMD_VCC_READ:
    {
      clearSendBuffer();
      int valueRead = VCC();
      sendBuffer[0] = valueRead & 0xff;
      sendBuffer[1] = valueRead >> 8;
      I2CReply();
      break;
    }
  }
}

void handleSleep(byte wake_timer){

  // logic to toggle control output pin
  if(state != lastState){
    lastState = state;
    if(state){
      pinMode(wake_pin, OUTPUT);
      digitalWrite(wake_pin, LOW);
    }
    else{
      pinMode(wake_pin, INPUT);
    }
  }

  // non blocking led blink when not in sleep mode
  if(!wake_led && wake_led_count > 500){
    wake_led = true;
    wake_led_count=0;
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin,HIGH);
  }
  if(wake_led && wake_led_count > 25){
    wake_led = false;
    wake_led_count=0;
    pinMode(led_pin, OUTPUT);
    digitalWrite(led_pin,LOW);
  }
  wake_led_count++;

  // System will be awake for 10 seconds, then goes to sleep with I2C disabled
  // Ony WDT will wake the MCU.
  wakeCounter++;
  
  unsigned long maxAwake = 10000;
  if(resetState)
    maxAwake = 60000;
    
  if(wakeCounter > maxAwake){
    digitalWrite(led_pin,LOW); // Make sure LED is off

    // This may be needed, need to check without
    digitalWrite(0,LOW);
    digitalWrite(2,LOW);
    pinMode(0,INPUT);  // SDA pin float as input
    pinMode(2,INPUT);  // SCL pin float as input
    
    USICR = 0; // Disable IRQ from I2C pins
    Watchdog_sleep(wake_timer * 7.5); // 75 = sleep for 10 minutes
    wakeCounter = 0 ;
    // Restart I2C slave
    TinyWireS_begin(I2C_Address);
    state = !state;
  }
}

void Watchdog_setup(int ii)
{  
  // The prescale value is held in bits 5,2,1,0
  // This block moves ii into these bits
  byte bb;
  if (ii > 9 ) ii=9;
  bb=ii & 7;
  if (ii > 7) bb|= (1<<5);
  bb|= (1<<WDCE);

  // Reset the watchdog reset flag
  MCUSR &= ~(1<<WDRF);
  // Start timed sequence
  WDTCR |= (1<<WDCE) | (1<<WDE);
  // Set new watchdog timeout value
  WDTCR = bb;
  // Enable interrupts instead of reset
  WDTCR |= _BV(WDIE);
}

void Watchdog_sleep(int waitTime)
{
  // Calculate the delay time
  int waitCounter = 0;
  while (waitCounter != waitTime) 
  {
    cbi(ADCSRA,ADEN); // Switch Analog to Digital converter OFF 
    set_sleep_mode(SLEEP_MODE_PWR_DOWN); // Set sleep mode
    sleep_mode(); // System sleeps here
    sbi(ADCSRA,ADEN);  // Switch Analog to Digital converter ON
    waitCounter++;
  }
}

ISR(WDT_vect) 
{
  // Don't do anything here but we must include this
  // block of code otherwise the interrupt calls an
  // uninitialized interrupt handler.
}

void clearSendBuffer()
{
  for(byte x=0; x < sizeof(sendBuffer); x++)
    sendBuffer[x]=0;
}

void I2CReply(){
  for(byte x=0; x < sizeof(sendBuffer);x++){
    TinyWireS_send(sendBuffer[x]);
  }
  // reinit after session makes it more reliable
  delay(50);
  TinyWireS_begin(I2C_Address);
}

long VCC() {  
  ADMUX = _BV(MUX3) | _BV(MUX2);
  delay(2); // Wait for Vref to settle
  ADCSRA |= _BV(ADSC); // Start conversion
  while (bit_is_set(ADCSRA,ADSC)); // measuring
  long vref = 1100; // Default 1100 mV reference

  // Calibrate with stored EEPROM value
  // Used as signed value, so ranges from -127 to +127 mV
  // Default EEPROM value is 255, so this would offset to -1 mV, nothing to worry about
  int8_t vcal = EEPROM.read(EEPROM_VCC_CAL);
  vref += vcal;
  long vcc = ADC;
  vcc = (vref*1023) / vcc; // Calculate Vcc (in mV);    
  return vcc;
}

