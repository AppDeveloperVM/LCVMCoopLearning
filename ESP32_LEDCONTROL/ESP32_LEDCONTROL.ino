/*********
  LC & Vic Proyect on ESP32
  Complete project details at
*********/

// Load Wi-Fi library
#include <WiFi.h>

// Replace with your network credentials
const char* ssid = "LC-WIFFI";
const char* password = "xPNpzuC6N4aa*()";

// Set web server port number to 80
WiFiServer server(350);

// Variable to store the HTTP request
String header;

// Auxiliar variables to store the current output state
String output26State = "off";
String output27State = "off";

// Assign output variables to GPIO pins
const int output26 = 26;
const int output27 = 27;
const int irLed = 23;

// Current time
unsigned long currentTime = millis();
// Previous time
unsigned long previousTime = 0;
// Define timeout time in milliseconds (example: 2000ms = 2s)
const long timeoutTime = 2000;

// ################# TASK DEFINITION #################

// Task for IO in core 0
TaskHandle_t task_wiffi;

// Task for IO in core 1
TaskHandle_t task_io;

// ############## Variables for task_io ############## (This will be inside the library in the future)

// setting PWM properties
const int pwmFreq = 38000;   // PWM frecuency: 38 KHz
const int pwmLedChannel = 0; // PWM chanel: 0
const int pwmResolution = 8; // PWM resolution: 8 bits
const int pwmDuttyCycle = 127;  // PWM dutty cycle (127 = 50% because 8 bits PWM, MAX_VALUE = 255).

//#####################################################

// create a hardware timer
hw_timer_t * timer_ir = NULL;

portMUX_TYPE synch = portMUX_INITIALIZER_UNLOCKED;

volatile boolean sendIRFlag = false;
const byte MAX_COMMAND_BYTE_LENGTH = 15;

volatile byte command[15] = {0x28, 0xC6, 0x0, 0x8, 0x8, 0x3F, 0x10, 0xC, 0x86, 0x80, 0x80, 0x0, 0x0, 0x0, 0xB6};  // Comando de prueba
volatile byte commandByteLength = 15;
volatile byte bitMask = 0B10000000; // The most significant bit will be first to transmit.
volatile byte actualByte = 0;
volatile byte actualBitCounter = 7;
volatile byte actualBitValue = 0; // The actual bit value.

volatile byte onTimerTickCounterPerBit = 0; // Indicates the number of ticks of onCounter per single bit. When a bit ends this variable equals to 0.

volatile boolean sendStartBitFlag = true; // true: Indicates that start bit must be sent, false: start bit is already sended.
volatile boolean pwmOnFlag = true;

// #####################################################################

// ------------------ ISR FUNCTION FOR TIMER ------------------
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL(&synch);
  if (sendIRFlag) {
    byte b = command[actualByte];
    if (sendStartBitFlag) {
      if (pwmOnFlag) {
        if (onTimerTickCounterPerBit == 0) {
          ledcWrite(pwmLedChannel, pwmDuttyCycle); // Enables PWM output.
          pwmOnFlag = true;
        }
        else if (onTimerTickCounterPerBit == 8) { // The start bit needs 8 ticks of onTimer(), so in the 7th tick we prepare the pwmOnFlag for the next tick;
          ledcWrite(pwmLedChannel, 0); // Disables the pwm output.
          pwmOnFlag = false;
        }
        onTimerTickCounterPerBit++; // Increment onTimerTickCounterPerBit variable.
      }
      else {
        if (onTimerTickCounterPerBit == 12) { // 4 ticks more to end the start bit transmision.
          sendStartBitFlag = false; // indicates that the start bit is alredy transmited.
          ledcWrite(pwmLedChannel, pwmDuttyCycle); // Enables PWM output.
          pwmOnFlag = true; // prepare the pwmFlag for the next bit,
          onTimerTickCounterPerBit = 0; // prepare the onTimerTickCounterPerBit for next bit.
        }
        else {
          onTimerTickCounterPerBit++; // Increment onTimerTickCounterPerBit variable.
        }
      }
    }
    // Normal bits
    else {
      if (actualByte < commandByteLength) {
        if (onTimerTickCounterPerBit == 0) {
          onTimerTickCounterPerBit++;
          ledcWrite(pwmLedChannel, 0); // Disables the pwm output.
          pwmOnFlag = false;
        }
        else {
          // Checks if the bit is 1. if 1 wait 2 Ticks more.
          if (command[actualByte] & bitMask) {
            if (onTimerTickCounterPerBit == 3) {
              onTimerTickCounterPerBit = 0;
              bitMask = bitMask >> 1; // NextBit
              if (bitMask == 0) {
                bitMask = 0B10000000; // Reset Bitmask.
                actualByte++; // Next byte.
              }
              // There is another bit to send
              if (actualByte < commandByteLength) {
                ledcWrite(pwmLedChannel, pwmDuttyCycle); // Enables PWM output.
                pwmOnFlag = true; // prepare the pwmFlag for the next bit,
              }
            }
            else {
              onTimerTickCounterPerBit++; // Increment onTimerTickCounterPerBit variable.
            }
          }
          // The bit is 0. wait 1 tick more.
          else {
            if (onTimerTickCounterPerBit == 1) {
              onTimerTickCounterPerBit = 0;
              bitMask = bitMask >> 1; // NextBit
              if (bitMask == 0) {
                bitMask = 0B10000000; // Reset Bitmask.
                actualByte++; // Next byte.
              }
              // There is another bit to send
              if (actualByte < commandByteLength) {
                ledcWrite(pwmLedChannel, pwmDuttyCycle); // Enables PWM output.
                pwmOnFlag = true; // prepare the pwmFlag for the next bit,
              }
            }
            else {
              onTimerTickCounterPerBit++; // Increment onTimerTickCounterPerBit variable.
            }
          }
        }
      }
      if (actualByte >= commandByteLength) {
        sendIRFlag = false;
        sendStartBitFlag = true;
        bitMask = 0B10000000; // The most significant bit will be first to transmit.
        actualByte = 0;
        actualBitCounter = 7;
        actualBitValue = 0; // The actual bit value.
        pwmOnFlag = true; // prepare the pwmFlag for the next bit,
      }
    }
  }
  portEXIT_CRITICAL(&synch);
}

void setup() {
  Serial.begin(115200);
  // Initialize the output variables as outputs
  pinMode(irLed, OUTPUT);
  pinMode(16, INPUT);
  pinMode(output26, OUTPUT);
  pinMode(output27, OUTPUT);
  // Set outputs to LOW
  digitalWrite(irLed, LOW);
  digitalWrite(output26, LOW);
  digitalWrite(output27, LOW);

  // Task in core 0.
  xTaskCreatePinnedToCore(
    wiffiLoop,      // Function to implement the task
    "wiffiTask",    // Name of the task
    1024,           // Stack size in words
    NULL,           // Task input parameter
    0,              // Priority of the task, the higher the more priority.
    &task_wiffi,    // Task Handle
    0);             // Core where the task should run.

  // Task in core 1
  xTaskCreatePinnedToCore(
    ioLoop,         // Function to implement the task
    "ioTask",       // Name of the task
    1024,           // Stack size in words
    NULL,           // Task input parameter
    1,              // Priority of the task, the higher the more priority.
    &task_io,       // Task Handle
    1);             // Core where the task should run.

  // Connect to Wi-Fi network with SSID and password
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  // Print local IP address and start web server
  Serial.println("");
  Serial.println("WiFi connected.");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  server.begin();
}

// Task related to IO (core 1)
void ioLoop(void * parameter) {

  // configure LED PWM functionalitites
  ledcSetup(pwmLedChannel, pwmFreq, pwmResolution);

  // attach the channel to the GPIO to be controlled
  ledcAttachPin(irLed, pwmLedChannel);

  /* Use 1st timer of 4 */
  /* 1 tick take 1/(CPU_FREQ_SECONDS/CPU_FREQ_MHZ) = 1us so we set divider ESP.getCpuFreqMHz() and count up */
  timer_ir = timerBegin(0, 80, true);

  /* Attach onTimer function to our timer */
  timerAttachInterrupt(timer_ir, &onTimer, true);

  /* Set alarm to call onTimer function every 408us, 1 tick is 1us
    /* Repeat the alarm (third parameter) */
  timerAlarmWrite(timer_ir, 408, true);

  delay(500);

  /* Start an alarm */
  timerAlarmEnable(timer_ir);

  // Needs to be here, infinite loop.
  while (1) {
    if(!sendIRFlag){
      if(digitalRead(19) == HIGH){
        vTaskDelay(50 / portTICK_RATE_MS); // Wait 50ms
        if(digitalRead(19) == HIGH);
        sendIRFlag = true;
      }
    }
    else{
        
    }
    vTaskDelay(10 / portTICK_RATE_MS); // Wait 10ms.
  }
}

// Task related with wiffi conection (In core 0).
void wiffiLoop(void * parameter) {
  // Needs to be here, infinite loop.
  while (1) {
    WiFiClient client = server.available();   // Listen for incoming clients

    if (client) {                             // If a new client connects,
      currentTime = millis();
      previousTime = currentTime;
      Serial.println("New Client.");          // print a message out in the serial port
      String currentLine = "";                // make a String to hold incoming data from the client
      while (client.connected() && currentTime - previousTime <= timeoutTime) {  // loop while the client's connected
        currentTime = millis();
        if (client.available()) {             // if there's bytes to read from the client,
          char c = client.read();             // read a byte, then
          Serial.write(c);                    // print it out the serial monitor
          header += c;
          if (c == '\n') {                    // if the byte is a newline character
            // if the current line is blank, you got two newline characters in a row.
            // that's the end of the client HTTP request, so send a response:
            if (currentLine.length() == 0) {
              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line:
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Connection: close");
              client.println();

              // turns the GPIOs on and off
              if (header.indexOf("GET /26/on") >= 0) {
                Serial.println("GPIO 26 on");
                output26State = "on";
                digitalWrite(output26, HIGH);
              } else if (header.indexOf("GET /26/off") >= 0) {
                Serial.println("GPIO 26 off");
                output26State = "off";
                digitalWrite(output26, LOW);
              } else if (header.indexOf("GET /27/on") >= 0) {
                Serial.println("GPIO 27 on");
                output27State = "on";
                digitalWrite(output27, HIGH);
              } else if (header.indexOf("GET /27/off") >= 0) {
                Serial.println("GPIO 27 off");
                output27State = "off";
                digitalWrite(output27, LOW);
              }

              // Display the HTML web page
              client.println("<!DOCTYPE html><html>");
              client.println("<head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
              client.println("<link rel=\"icon\" href=\"data:,\">");
              // CSS to style the on/off buttons
              // Feel free to change the background-color and font-size attributes to fit your preferences
              client.println("<style>html { font-family: Helvetica; display: inline-block; margin: 0px auto; text-align: center;}");
              client.println(".button { background-color: #4CAF50; border: none; color: white; padding: 16px 40px;");
              client.println("text-decoration: none; font-size: 30px; margin: 2px; cursor: pointer;}");
              client.println(".button2 {background-color: #555555;}</style></head>");

              // Web Page Heading
              client.println("<body><h1>ESP32 Web Server</h1>");

              // Display current state, and ON/OFF buttons for GPIO 26
              client.println("<p>GPIO 26 - State " + output26State + "</p>");
              // If the output26State is off, it displays the ON button
              if (output26State == "off") {
                client.println("<p><a href=\"/26/on\"><button class=\"button\">ON</button></a></p>");
              } else {
                client.println("<p><a href=\"/26/off\"><button class=\"button button2\">OFF</button></a></p>");
              }

              // Display current state, and ON/OFF buttons for GPIO 27
              client.println("<p>GPIO 27 - State " + output27State + "</p>");
              // If the output27State is off, it displays the ON button
              if (output27State == "off") {
                client.println("<p><a href=\"/27/on\"><button class=\"button\">ON</button></a></p>");
              } else {
                client.println("<p><a href=\"/27/off\"><button class=\"button button2\">OFF</button></a></p>");
              }
              client.println("</body></html>");

              // The HTTP response ends with another blank line
              client.println();
              // Break out of the while loop
              break;
            } else { // if you got a newline, then clear currentLine
              currentLine = "";
            }
          } else if (c != '\r') {  // if you got anything else but a carriage return character,
            currentLine += c;      // add it to the end of the currentLine
          }
        }
      }
      // Clear the header variable
      header = "";
      // Close the connection
      client.stop();
      Serial.println("Client disconnected.");
      Serial.println("");
    }
  }
}

// Arduino needs this function to work properly.
void loop() {
  //delay(100); // Needs some code to work, the best thing to do is a delay since RTOS change the task instead of doing the delay.
  vTaskDelete(NULL); // Delete this task.
}

// ####################### IO FUNCTIONS (In future this will be a library for suporting different protocols) #######################

// ------------- "HIGH LEVEL FUNCTIONS" -------------
void sendStartBit() {
  transmit(3285, 1600);
}

void sendBit(unsigned int bit) {
  switch (bit) {
    // Sends the "0" bit.
    case 0:
      transmit(400, 400);
      break;

    // Sends the "1" bit.
    case 1:
      transmit(400, 1210);
      break;
  }
}

// ------------- "MEDIUM LEVEL FUNCTIONS" -------------
void transmit(unsigned long onTime, unsigned long offTime) {
  ledcWrite(pwmLedChannel, pwmDuttyCycle);
  ledcWrite(pwmLedChannel, 0);
}
