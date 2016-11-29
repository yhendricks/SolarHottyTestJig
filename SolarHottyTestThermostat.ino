#include <EEPROM.h>

bool thermostat_state = false;           // Holds the state of the thermostat
const int WATER_PUMP_PIN = 12;
// current measurement variables
float amps = 0;
float maxAmps = 0;
float minAmps = 0;
float lastAmps = 0;
float noise = 0;

enum thermoState {
    OPEN,
    CLOSED
};

template <class T> int EEPROM_writeAnything(int ee, const T& value)
{
    const byte* p = (const byte*)(const void*)&value;
    int i;
    for (i = 0; i < sizeof(value); i++)
    EEPROM.write(ee++, *p++);
    return i;
}


template <class T> int EEPROM_readAnything(int ee, T& value)
{
    byte* p = (byte*)(void*)&value;
    int i;
    for (i = 0; i < sizeof(value); i++)
    *p++ = EEPROM.read(ee++);
    return i;
}


struct config_t
{
    long counter;
} configuration;


void setup()
{    
    Serial.begin(9600);
    pinMode(WATER_PUMP_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    // Read the configuration i.e. the total number of contact breaks of the thermostat
    EEPROM_readAnything(81, configuration);
    Serial.println("\r\n");
    Serial.println("SolarHotty Test Application");
    Serial.println("==========================="); 
    Serial.println("Press x to reset the counter");   
}
  
enum thermoState get_thermostat_state()
{
//    return digitalRead(MEASURE_THERMO_CURRENT_PIN) ?  OPEN :  CLOSED;
    return measure_current() > 0.5 ? CLOSED : OPEN;
}

void print_configuration()
{
    Serial.print("# of thermostat contact breaks: ");
    Serial.println(configuration.counter);     
}

void print_thermostat_state()
{
    Serial.println(thermostat_state == CLOSED ? "Thermostat is close" : "Thermostat is open" ); 
}

int mVperAmp = 66; // use 185 or use 100 for 20A Module and 66 for 30A Module
int RawValue= 0;
int ACSoffset = 2500; 
double Voltage = 0;
double Amps = 0;

float measure_current() {
    RawValue = analogRead(A0);
//    Serial.print(RawValue);
//    Serial.print(" ");
    Voltage = (RawValue / 1024.0) * 5000; // Gets you mV
    amps = ((Voltage - ACSoffset) / mVperAmp); 
    
    //amps = (516 - RawValue) * 27.03 / 1023;
    amps = (amps + lastAmps) / 2;
    lastAmps = amps;
    maxAmps = max(maxAmps, amps);
    minAmps = min(minAmps, amps);
    noise = maxAmps - minAmps;
//    Serial.print(amps);
//    Serial.print(" ");
//    Serial.println(noise);
    if (Serial.read() != -1) 
        {maxAmps = amps; minAmps = amps;}
    return amps;
}

void loop() {
    byte byteRead;
    while (1) {
        //measure_current();
        if (get_thermostat_state() == CLOSED) {
            // There is DC current flowing
            if (thermostat_state == OPEN) {
                configuration.counter++;
                thermostat_state = CLOSED;
                digitalWrite(WATER_PUMP_PIN, HIGH);              // switch OFF water pump
                print_thermostat_state();
                print_configuration();
            }
        }
        else {
            // There is NO DC current flowing
            if (thermostat_state == CLOSED)
            {
                thermostat_state = OPEN;   
                print_thermostat_state();    
                digitalWrite(WATER_PUMP_PIN, LOW);             // switch ON water pump
            }        
        }
        /*  check if data has been sent from the computer: */
        if (Serial.available()) {
            /* read the most recent byte */
            byteRead = Serial.read();
            if (byteRead == 'x') {
                // Reset the counter
                configuration.counter = 0;
                print_configuration();
            }
        }

        EEPROM_writeAnything(81, configuration);
        digitalWrite(LED_BUILTIN, HIGH);   // turn the LED on (HIGH is the voltage level)
        delay(500);                       
        digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
        delay(500);
    }
}

