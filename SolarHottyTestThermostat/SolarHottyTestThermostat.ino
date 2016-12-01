#include <EEPROM.h>
#include <Filters.h>


#define VER_NUM             0.1           // File version number

bool thermostat_state = false;           // Holds the state of the thermostat
const int WATER_PUMP_PIN = 12;
// DC current measurement variables
float amps = 0;
float maxAmps = 0;
float minAmps = 0;
float lastAmps = 0;
float noise = 0;
unsigned long print_DC_Period = 1000; // in milliseconds 
unsigned long previous_DC_Millis = 0;  // Track time in milliseconds since last reading

// AC current measurement variables
float testFrequency = 60;                     // test signal frequency (Hz)
float windowLength = 20.0/testFrequency;     // how long to average the signal, for statistist
float intercept = -0.1129; // to be adjusted based on calibration testing
float slope = 0.0405; // to be adjusted based on calibration testing
float current_amps; // estimated actual current in amps
unsigned long print_AC_Period = 1000; // in milliseconds 
unsigned long previous_AC_Millis = 0;  // Track time in milliseconds since last reading
RunningStatistics inputStats;                 // create statistics to look at the raw test signal

// LED status
unsigned long led_blink_period = 1000; // in milliseconds 
unsigned long last_led_check = 0;       // Track time in milliseconds since last reading
bool led_state = false;

// debug flag
bool debug_on = false;

enum thermoState
{
    OPEN,
    CLOSED
};

enum HottyState
{
    NOT_KNOWN,
    AC_DETECTED,
    DC_DETECTED
};

enum HottyState hottyState = NOT_KNOWN;

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


bool check_ac_current(int sensorValue) {
    //sensorValue = analogRead(A0);  // read the analog in value:
    inputStats.input(sensorValue);  // log to Stats function

    if ((unsigned long)(millis() - previous_AC_Millis) >= print_AC_Period)
    {
        previous_AC_Millis = millis();   // update time

        if (debug_on)
        {
            // display current values to the screen
            Serial.print( "\n" );
            // output sigma or variation values associated with the inputValue itsel
            Serial.print( "\tsigma: " ); Serial.print( inputStats.sigma() );
            // convert signal sigma value to current in amps
            current_amps = intercept + slope * inputStats.sigma();
            Serial.print( "\tamps: " ); Serial.print( current_amps );
        }
        if ((inputStats.sigma() > 50) && (hottyState != AC_DETECTED))
        {
            Serial.println("\nHotty running on AC current ");
            hottyState = AC_DETECTED;
            led_blink_period = 1000;
        }
    }
    return(inputStats.sigma() > 50);    
}

void display_help()
{
    Serial.println("\r\n");
    Serial.print("SolarHotty Test Application ");
    Serial.println(VER_NUM,1);
    Serial.println("==============================="); 
    Serial.println("Enter:");
    Serial.println("\"reset\" to reset the counter");   
    Serial.println("\"debug\" to toggle the debug flag");   
    Serial.println("\"pinon x\" to turn ON pin x");   
    Serial.println("\"pinoff x\" to turn OFF pin x");
    Serial.println("\"help x\" to display this message");
}

void setup()
{    
    Serial.begin(57600);
    pinMode(WATER_PUMP_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    // Read the configuration i.e. the total number of contact breaks of the thermostat
    EEPROM_readAnything(81, configuration);
    display_help();            
}                              

enum thermoState get_thermostat_state(int raw_value)
{
//    return digitalRead(MEASURE_THERMO_CURRENT_PIN) ?  OPEN :  CLOSED;
    return measure_dc_current(raw_value) > 0.5 ? CLOSED : OPEN;
}

void print_configuration()
{
    Serial.print("# of thermostat contact breaks: ");
    Serial.println(configuration.counter);     
}

void print_thermostat_state()
{
    Serial.println(thermostat_state == CLOSED ? "Thermostat state: close" : "Thermostat state: open" ); 
}

int mVperAmp = 66; // use 185 or use 100 for 20A Module and 66 for 30A Module
int RawValue= 0;
int ACSoffset = 2500; 
double Voltage = 0;
double Amps = 0;

float measure_dc_current(int RawValue) {       

    Voltage = (RawValue / 1024.0) * 5000; // Gets you mV
    amps = ((Voltage - ACSoffset) / mVperAmp); 

    //amps = (516 - RawValue) * 27.03 / 1023;
    amps = (amps + lastAmps) / 2;
    lastAmps = amps;
    maxAmps = max(maxAmps, amps);
    minAmps = min(minAmps, amps);
    noise = maxAmps - minAmps;
    if (Serial.read() != -1)
    {
        maxAmps = amps; minAmps = amps;
    }
    if ((unsigned long)(millis() - previous_DC_Millis) >= print_DC_Period)
    {
        previous_DC_Millis = millis();   // update time
        if (debug_on)
        {
            Serial.print( "\n" );
            Serial.print("raw value: "); 
            Serial.print(RawValue);
            Serial.print(" ");
            Serial.print(", amps: ");
            Serial.print(amps);
            Serial.print(" ");
            Serial.print(", noise: "); 
            Serial.print(noise);
        }
        if (hottyState != DC_DETECTED)
        {
            hottyState = DC_DETECTED;
            led_blink_period = 400;
            Serial.println("\nHotty running on DC current ");
        }
    }
    return amps;
}

String command;

void loop() {
    byte byteRead;

    inputStats.setWindowSecs( windowLength );
    while (1)
    {
        RawValue = analogRead(A0);
        if (check_ac_current(RawValue) == false)
        {
            if (get_thermostat_state(RawValue) == CLOSED)
            {
                // There is DC current flowing
                if (thermostat_state == OPEN)
                {
                    configuration.counter++;
                    thermostat_state = CLOSED;
                    digitalWrite(WATER_PUMP_PIN, HIGH);              // switch OFF water pump
                    print_thermostat_state();
                    print_configuration();
                }
            }
            else
            {
                // There is NO DC current flowing
                if (thermostat_state == CLOSED)
                {
                    thermostat_state = OPEN;   
                    print_thermostat_state();    
                    digitalWrite(WATER_PUMP_PIN, LOW);             // switch ON water pump
                }
            }
        }
        else
        {
            digitalWrite(WATER_PUMP_PIN, LOW);             // switch ON water pump          
        }
        //  check if data has been sent from the computer: 
        if (Serial.available())
        {
            char c = Serial.read();
            if (c == '\n')
            {
                parseCommand(command);
                command = ""; 
            }
            else
            {
                command += c;
            }
        }

        EEPROM_writeAnything(81, configuration);
        if ((unsigned long)(millis() - last_led_check) >= (led_blink_period /2))
        {
            last_led_check = millis();   // update time
            if (led_state)
            {
                led_state = false;
                digitalWrite(LED_BUILTIN, LOW);    // turn the LED off by making the voltage LOW
            }
            else
            {
                led_state = true;
                digitalWrite(LED_BUILTIN, HIGH);  // turn the LED on (HIGH is the voltage level)
            }
        }
    }
}

void parseCommand(String cmd)
{
    String part1;
    String part2;
    // e.g. PINON 3  or PINOFF 3
    part1 = cmd.substring(0, cmd.indexOf(" "));
    part1 = cmd.substring(cmd.indexOf(" ") + 1);
    if (part1.equalsIgnoreCase("pinon"))
    {
        int pin = part2.toInt();
        digitalWrite(pin, HIGH);
    }
    else if (part1.equalsIgnoreCase("pinoff"))
    {   
        int pin = part2.toInt();
        digitalWrite(pin, LOW);
    }
    else if (part1.equalsIgnoreCase("debug"))
    {         
        debug_on = !debug_on; 
    } 
    else if (part1.equalsIgnoreCase("reset"))
    {             
        configuration.counter = 0;
        print_configuration();   
    }
    else if (part1.equalsIgnoreCase("help"))
    {         
        display_help(); 
    }
    else 
    {
        Serial.println("Invalid Command - " + cmd);
    }

}

