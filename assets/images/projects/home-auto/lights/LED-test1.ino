const int LED_PIN = 9;
const int transition = 3000; //5 seconds
const int stepInterval = 40;

const int numberOfSteps = transition/stepInterval;

int level = 0;
int input = 0;

int changeStep = 0;

unsigned long lastChange;

void setup() {
 Serial.begin(9600); //This pipes to the serial monitor
 Serial1.begin(9600); //This is the UART, to EPS8622
 
  // put your setup code here, to run once:
  pinMode(LED_PIN, OUTPUT);

  lastChange = millis();

}

void loop() {
// if there's any serial available, read it:
  while (Serial.available() > 0) {

    // look for the next valid integer in the incoming serial stream:
    input = Serial.parseInt();

    // look for the newline. That's the end of your
    // sentence:
    if (Serial.read() == '\n') {
      // constrain the values to 0 - 255 and invert
      // if you're using a common-cathode LED, just use "constrain(color, 0, 255);"
      input = constrain(input, 0, 255);

      if (input > level){
        changeStep = ceil(((float) (input - level))/((float) numberOfSteps));
      }else if (input < level){
        changeStep = -ceil(((float) (level - input))/((float) numberOfSteps));
      }else{
        changeStep = 0;
      }

      Serial.println(level);
      Serial.println(changeStep);

      // print the three numbers in one string as hexadecimal:
      Serial.println(input);
      Serial.println("OK");
    }
  }

  if ((millis() - lastChange) > stepInterval && changeStep != 0){ //40ms steps
      
      if ((level <= input && changeStep < 0) || (level >= input && changeStep > 0)) {
        changeStep = 0;
        level = input;
      }else{

        level += changeStep;
  
        level = constrain(level, 0, 255);
      }

        // fade the red, green, and blue legs of the LED:
      analogWrite(LED_PIN, level);

      lastChange = millis();
  }

}

