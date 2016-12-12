#include <EEPROM.h>
#include <Filters.h>
//#include <SD.h>

#define TEST_FREQUENCY                      50            // test signal frequency (Hz)
#define WATER_PUMP_PIN                      12
//#define AC_GEYSER_ASSIST_ELEMENT_PIN        12

bool thermostat_state = false;           // Holds the state of the thermostat

// DC current measurement variables
float maxAmps = 0;
float minAmps = 0;
float lastAmps = 0;
unsigned long print_DC_Period = 1000;           // in milliseconds 
unsigned long previous_DC_Millis = 0;           // Track time in milliseconds since last reading
#define MV_PER_AMP                  66                  // use 185 or use 100 for 20A Module and 66 for 30A Module
#define ACS_OFFSET                  2500

// AC current measurement variables
float windowLength = 20.0/TEST_FREQUENCY;        // how long to average the signal, for statistist
float intercept = -0.1129;                      // to be adjusted based on calibration testing
float slope = 0.0405;                           // to be adjusted based on calibration testing
unsigned long print_AC_Period = 1000; // in milliseconds 
unsigned long previous_AC_Millis = 0;  // Track time in milliseconds since last reading
RunningStatistics inputStats;                 // create statistics to look at the raw test signal


// debug flag
bool debug_on = false;

// SD Card

bool sd_card_present = true;

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
    inputStats.input(sensorValue);  // log to Stats function
    float amps = 0;

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
            amps = intercept + slope * inputStats.sigma();
            Serial.print( "\tamps: " ); Serial.print( amps );
        }
    }
    return(inputStats.sigma() > 50);    
}

void display_help()
{
    String VER_NUM = "0.2";           // File version number
    String data = "\r\n";
    data += "\nSolarHotty Test Application " + VER_NUM;
    data += ("\n==============================="); 
    data += ("\nEnter:");
    data += ("\n\t\"setcounter x\" set the counter to value x");   
    data += ("\n\t\"debug\" to toggle the debug flag");   
    data += ("\n\t\"pinon x\" to turn ON pin x");   
    data += ("\n\t\"pinoff x\" to turn OFF pin x");
    data += ("\n\t\"help\" to display this message");

    log_data(data);

    print_configuration();
}

void setup()
{   
    // On the Ethernet Shield, CS is pin 4. Note that even if it's not
    // used as the CS pin, the hardware CS pin (10 on most Arduino boards,
    // 53 on the Mega) must be left as an output or the SD library
    // functions will not work.
    const int chipSelect = 4;
    Serial.begin(115200);
    // Open serial communications and wait for port to open:
    while (!Serial) {
        ; // wait for serial port to connect. Needed for Leonardo only
    }
    log_data("Initializing SD card...");
    // make sure that the default chip select pin is set to
    // output, even if you don't use it:
    pinMode(10, OUTPUT);
    
    // see if the card is present and can be initialized:
    //if (!SD.begin(chipSelect)) 
    {
        // don't do anything more:
        sd_card_present = false;
        log_data("Card failed, or not present");
    }
    //log_data("card initialized.");

    pinMode(WATER_PUMP_PIN, OUTPUT);
    //pinMode(AC_GEYSER_ASSIST_ELEMENT_PIN, OUTPUT);
    pinMode(LED_BUILTIN, OUTPUT);
    // Read the configuration i.e. the total number of contact breaks of the thermostat
    EEPROM_readAnything(0, configuration);
    display_help();            
}                              

enum thermoState get_thermostat_state(int raw_value)
{
    return abs(measure_dc_current(raw_value)) > 0.3 ? CLOSED : OPEN;
}

void print_configuration()
{
    Serial.print("\n# of thermostat contact breaks: ");
    Serial.println(configuration.counter);     
}

void print_thermostat_state()
{
    log_data(thermostat_state == CLOSED ? "Thermostat state: close" : "Thermostat state: open" ); 
}

float measure_dc_current(int RawValue) {       

    double Voltage = 0;
    float noise = 0;
    float amps = 0;


    Voltage = (RawValue / 1024.0) * 5000; // Gets you mV
    amps = ((Voltage - ACS_OFFSET) / MV_PER_AMP); 
    amps = (amps + lastAmps) / 2;
    lastAmps = amps;
    maxAmps = max(maxAmps, amps);
    minAmps = min(minAmps, amps);
    noise = maxAmps - minAmps;
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
            Serial.println("\nHotty running on DC current ");
        }
    }
    return amps;
}

String command;


#define PUMP_ON_OFF_TIME 15000
void loop() {
    byte byteRead;
    bool led_state = false;
    int counter = 0;
    int RawValue= 0;
    // LED status
    unsigned long led_blink_period = 1000; // in milliseconds 
    unsigned long last_led_check = 0;       // Track time in milliseconds since last reading
    unsigned long last_pump_check = 0;       // Track time in milliseconds since last reading

    byte pump_counter = 0;
    bool starting_up = true;


    inputStats.setWindowSecs( windowLength );
    while (1)
    {
        RawValue = analogRead(A0);
        if (check_ac_current(RawValue) == false)
        {
            if (get_thermostat_state(RawValue) == CLOSED)
            {
                // There is DC current flowing, but check multiple times to make sure
                if (thermostat_state == OPEN)
                {
                    counter = 0;
                    for (int i = 0; i < 10; i++)
                    {
                        delayMicroseconds(1000);
                        RawValue = analogRead(A0);
                        if (get_thermostat_state(RawValue) == CLOSED)
                            counter++;
                    }
                    if (counter >= 7)
                    {
                        if (!starting_up)
                          configuration.counter++;
                        else
                          starting_up = false;
                        EEPROM_writeAnything(0, configuration);
                        thermostat_state = CLOSED;
                        digitalWrite(WATER_PUMP_PIN, HIGH);                             // switch OFF water pump
                        //digitalWrite(AC_GEYSER_ASSIST_ELEMENT_PIN, HIGH);               // switch ON the AC Geyser                        
                        print_thermostat_state();
                        print_configuration();
                        led_blink_period = 400;
                        pump_counter = 0;
                    }
                }
            }
            else
            {
                // There is NO DC current flowing
                if (thermostat_state == CLOSED)
                {
                    counter = 0;
                    for (int i = 0; i < 10; i++)
                    {
                        delayMicroseconds(1000);
                        RawValue = analogRead(A0);
                        if (get_thermostat_state(RawValue) == OPEN)
                            counter++;
                    }
                    if (counter >= 7)
                    {
                        thermostat_state = OPEN;   
                        print_thermostat_state(); 
                        log_data("\nHotty running on DC with NO current detected");   
                        digitalWrite(WATER_PUMP_PIN, LOW);             // switch ON water pump
                        //digitalWrite(AC_GEYSER_ASSIST_ELEMENT_PIN, LOW);               // switch OFF the AC Geyser 
                        print_thermostat_state();
                        print_configuration();
                        led_blink_period = 1000;
                        pump_counter = 31;
                    }
                }

                if (pump_counter > 0)
                {
                    if ((unsigned long)(millis() - last_pump_check) >= PUMP_ON_OFF_TIME)
                    {
                        last_pump_check = millis();   // update time
                        if (pump_counter-- % 2)
                        {
                            digitalWrite(WATER_PUMP_PIN, LOW);             // switch ON water pump
                        }
                        else
                        {
                            digitalWrite(WATER_PUMP_PIN, HIGH);             // switch OFF water pump
                        } 
                    }     
                }         
            }
        }
        else
        {
            if (hottyState != AC_DETECTED)
            {
                // wait a while and then do a re-check
                delay(5000);
                // Sometime the hotty does a series of test. So to avoid switching while it is doing the tests
                // we wait a while then perform the check again.
                if (check_ac_current(RawValue) == true)
                {                            
                    log_data("\nHotty running on AC current ");
                    hottyState = AC_DETECTED;
                    digitalWrite(WATER_PUMP_PIN, LOW);                              // switch ON water pump 
                    //digitalWrite(AC_GEYSER_ASSIST_ELEMENT_PIN, LOW);               // switch ON the AC Geyser      
                    led_blink_period = 2000;
                    pump_counter = 0;
                }
            }
        }
        //  check if data has been sent from the computer: 
        while (Serial.available())
        {
            char c = Serial.read();
            if (c == '\n')
            {
                parseCommand(command);
                command = ""; 
            }
            else
            {
                if (isAlphaNumeric(c) || isWhitespace(c))
                {
                    command += c;
                }
            }
        }

        
        // Set the blinking period
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

void log_data(String dataString)
{ 
    String data_file = "data.txt";  
    // print to the serial port too:
    Serial.println(dataString);

    if (sd_card_present)
    {
//        // open the file. note that only one file can be open at a time,
//        // so you have to close this one before opening another.
//        File dataFile = SD.open(data_file, FILE_WRITE);
//
//        // if the file is available, write to it:
//        if (dataFile) {
//            dataFile.println(dataString);
//            dataFile.close();        
//        }  
//        // if the file isn't open, pop up an error:
//        else {
//            Serial.println("error opening - " + data_file);
//        } 
    }
}

void parseCommand(String cmd)
{
    String part1;
    String part2;

    if (cmd.indexOf(" ") == -1)
    {
        if (cmd.indexOf("debug") != -1)
        {
            debug_on = !debug_on; 
        }
        else if (cmd.indexOf("help") != -1)
        {
            display_help(); 
        }
        else
        {
            Serial.println("Invalid Command - " + cmd);
        }
    }
    else
    {
        // e.g. PINON 3  or PINOFF 3
        part1 = cmd.substring(0, cmd.indexOf(" "));
        part2 = cmd.substring(cmd.indexOf(" ") + 1);
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
        else if (part1.equalsIgnoreCase("setcounter"))
        {
            int value = part2.toInt();
            configuration.counter = value;
            EEPROM_writeAnything(0, configuration);
            print_configuration();
        }
        else
        {
            Serial.println("Invalid Command - " + cmd);
        }
    }
}
