/****
 * Simplistic project to give confidence that the board has been soldered correctly
 * and that a number of the pins work - e.g. by pulsing the voltage on GPIO pins it is easy
 * to check they are operating using a voltmeter.
 * 
 * Validates that the serial port (either  the 4 pins or via the FT232 + USB-C)
 * is working - i.e. that a user can see debug messages coming from the board.
 * 
 * Simple tests for the 3 capacitative touch buttons on the PCB - i.e. touch them and see the debug 
 * values change via the serial port.
 * 
 * THIS PROJECT ASSUMES THAT THE TFT display AND THE RFM69HW are NOT SOLDERED IN
 * i.e. it tests that the connectors are ok first.
*/
#include <Arduino.h>
#include <sstream>

/*
• Touch0 >> GPIO4
• Touch1 >>  Not available on Devkit 30 pin version but available on Devkit 36 pin version 
• Touch2 >> GPIO2
• Touch3 >> GPIO15
• Touch4 >> GPIO13
• Touch5 >> GPIO12
• Touch6 >> GPIO14
• Touch7 >> GPIO27
• Touch8 >> GPIO33
• Touch9 >> GPIO32

i.e. for Touch9 you could use T9 - see below.
*/

/// we can add pin numbers to this array and it will pulse high -> low and allow us to test
const int GPIO_PIN_TOTEST[] = {21,22,25,26,34,35};
const int TOUCH_PINS[] = {T9,T8,T7};

bool flag = true; 

void setup() {
  Serial.begin(115200);
  Serial.println("Starting");


  /// Setup the GPIO for the 3 capacitative buttons 
  for(int touchpin : TOUCH_PINS)
  {
    pinMode(touchpin, INPUT);
  }

  // Set all the GPIO pins as OUTPUT ready to be pulsed high->low
  for (int i : GPIO_PIN_TOTEST) {
      pinMode(i,OUTPUT);
      digitalWrite(i,flag);
  }

  Serial.println("Finished setup");
}


void loop() {

  /// Pulse each GPIO between HIGH -> LOW every 2 seconds
  delay(2000); 
  flag = !flag;

  for (int i : GPIO_PIN_TOTEST) {
    digitalWrite(i,flag);  
  }
  
  std::stringstream stream;

  stream << "Touch button: ";

  for(int touchpin : TOUCH_PINS)
  {
    stream  << touchpin;
    stream << " [";
    stream << touchRead(touchpin);
    stream << "],  ";
  }

  Serial.println(stream.str().c_str());
}