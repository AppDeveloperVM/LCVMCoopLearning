/*********
  LC & Vic Proyect on ESP32
  Complete project details at
*********/

// Load Wi-Fi library
#include <WiFi.h>

// Replace with your network credentials
const char* ssid = "**************";
const char* password = "**************";

const int irLed = 23; // IR LED 950 nm

// Set web server port number to 350
WiFiServer server(350);

// Variable to store the HTTP request
String header;

// AC variables. (This variables are AC specific)
byte temperatura = 22;
String masterControl = "AUTO";
String fanControl = "AUTO";
boolean powerOn = false;
boolean swingState = false; // Indicates the state of the swing in the AC. True if swing is on, false if swing is off.
boolean swingChangedFlag = false; // Indicates that swingState is changed.
byte air_direction = 0;

SemaphoreHandle_t varUpdate_Semaphore; // Sempaphore for sync tasks.

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
const int pwmFreq = 38000;      // PWM frecuency: 38 KHz
const int pwmLedChannel = 0;    // PWM chanel: 0
const int pwmResolution = 8;    // PWM resolution: 8 bits
const int pwmDuttyCycle = 127;  // PWM dutty cycle (127 = 50% because 8 bits PWM, MAX_VALUE = 255).

//#####################################################

// create a hardware timer
hw_timer_t * timer_ir = NULL;

portMUX_TYPE synch = portMUX_INITIALIZER_UNLOCKED;

volatile boolean sendIRFlag = false;
const byte MAX_COMMAND_BYTE_LENGTH = 15;

volatile byte command[15] = {0x28, 0xC6, 0x0, 0x8, 0x8, 0x3F, 0x10, 0xC, 0x86, 0x80, 0x80, 0x0, 0x0, 0x0, 0xB6};  // Comando de prueba
volatile byte commandByteLength = 15; // Length of the command to be sent in bytes.
volatile byte bitMask = 0B10000000; // The most significant bit of each byte will be sent first.
volatile byte actualByte = 0; // Keeps the count of the actualByte in the transmision.
volatile byte actualBit = 0;  // Keps the count of the actualBit in the transmision.
volatile byte commandBitLength = 120; // Keeps the bitCount

volatile byte onTimerTickCounterPerBit = 0; // Indicates the number of ticks of onCounter per single bit. When a bit ends this variable equals to 0.

volatile boolean sendStartBitFlag = true; // true: Indicates that start bit must be sent, false: start bit is already sended.
volatile boolean pwmOnFlag = true;

// #####################################################################

// ------------------ ISR FUNCTION FOR TIMER ------------------
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL(&synch);
  if (sendIRFlag) {
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
        // Sends the stop bit.
        if (onTimerTickCounterPerBit == 0) {
          ledcWrite(pwmLedChannel, pwmDuttyCycle); // Enables PWM output.
          pwmOnFlag = true;
          onTimerTickCounterPerBit++;
        }
        else if (onTimerTickCounterPerBit == 1) {
          ledcWrite(pwmLedChannel, 0); // Disables the pwm output.
          pwmOnFlag = false;
          onTimerTickCounterPerBit++;
        }
        else if (onTimerTickCounterPerBit > 1 && onTimerTickCounterPerBit < 10) {
          onTimerTickCounterPerBit++;
        }
        // When stop bit is sended
        else if (onTimerTickCounterPerBit == 10) {
          onTimerTickCounterPerBit = 0;
          sendIRFlag = false;
          sendStartBitFlag = true;
          bitMask = 0B10000000; // The most significant bit will be first to transmit.
          actualByte = 0;
          pwmOnFlag = true; // prepare the pwmFlag for the next bit,
        }
      }
    }
  }
  portEXIT_CRITICAL(&synch);
}

void setup() {
  Serial.begin(115200);
  // Initialize the output variables as outputs
  pinMode(irLed, OUTPUT);
  // Set outputs to LOW
  digitalWrite(irLed, LOW);

  // Task in core 0.
  xTaskCreatePinnedToCore(
    wiffiLoop,      // Function to implement the task
    "wiffiTask",    // Name of the task
    2048,           // Stack size in words
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

  startTimer();

  delay(500);

  // Needs to be here, infinite loop.
  while (1) {

    //xSemaphoreTake(varUpdate_Semaphore, portMAX_DELAY); // Waits until the semaphore is free.

    if (!sendIRFlag) {

    }
    else {

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
            // that's the end of the head in the HTTP request.
            if (currentLine.length() == 0) {

              // We must check if there is more info in the client request. (body part of the request)
              if (client.available()) {
                String postInfo = "";
                while (client.available()) {
                  c = client.read();
                  postInfo += c;
                  Serial.write(c);
                }
                if (header.indexOf("POST / HTTP/1.1") >= 0) {

                  // Update the values of the variables
                  commandByteLength = findVarFromPost("byteLenght", postInfo).toInt();
                  commandBitLength = findVarFromPost("bitNumber", postInfo).toInt();

                  for(int i=0; i<commandByteLength; i++){
                    String varName = "b"+String(i);
                    command[i] = findVarFromPost(varName, postInfo).toInt();
                  }

                  sendIRFlag = true;  // borrar

                  //xSemaphoreGive(varUpdate_Semaphore); // Releases the semaphore
                }
              }

              // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
              // and a content-type so the client knows what's coming, then a blank line:
              client.println("HTTP/1.1 200 OK");
              client.println("Content-type:text/html");
              client.println("Access-Control-Allow-Origin: *");
              client.println("Access-Control-Allow-Methods: GET,POST,OPTIONS");
              client.println("Access-Control-Allow-Headers: Content-Type");
              client.println("Connection: close");
              client.println();

              // Display the HTML web page
              client.println("<!DOCTYPE html><html>");
              client.println("<title>ESP32 AC web server</title>");
              //------------------- HEAD -------------------
              client.println("<head>");
              //------------------- STYLE -------------------
              client.println("<style>");
              // ----- Style inputs -----
              client.println("input[type=text],input[type=number],select{width: 50%; padding: 12px 20px;margin: 8px 0; display:inline-block;border: 1px solid #ccc;border-radius: 4px;box-sizing: border-box;}");
              // ----- Style the submit button -----
              client.println("input[type=submit] {width: 50%; background-color: #4CAF50; color: white; padding: 14px 20px; margin: 8px 0; border: none; border-radius: 4px; cursor: pointer;}");
              // ----- Add a background color to the submit button on mouse-over -----
              client.println("input[type=submit]:hover {background-color: #45a049;");
              client.println(".ck-button {margin:4px; background-color:#EFEFEF; border-radius:4px; border:1px solid #D0D0D0; overflow:auto; float:left;}");
              client.println(".ck-button:hover {background:rgb(9, 110, 40);}");
              client.println(".ck-button label {float:left;width:4.0em;}");
              client.println(".ck-button label span {text-align:center; padding:3px 0px; display:block;}");
              client.println(".ck-button label input { position:absolute; top:-20px;}");
              client.println(".ck-button input:checked + span { background-color:rgb(28, 153, 17); color:#fff;}");
              client.println("</style>");
              // ------------------- END STYLE -------------------

              // ------------------- SCRIPT -------------------
              client.println("<script> function setInputs() {");
              // Time vars
              client.println("var today = new Date(); var time = today.getHours() + \": \" + today.getMinutes() ; // + \": \" + today.getSeconds()");
              // get variable defaults.
              client.println("document.getElementById(\"time\").value = time;");
              client.print("document.getElementById(\"temp\").defaultValue = "); client.print(temperatura); client.println(";");
              client.println("}</script>");
              // ----------------- END SCRIPT -----------------

              client.println("</head>");
              // ----------------- END HEAD -----------------
              // ------------------- BODY -------------------
              client.println("<body onload=\"setInputs()\">");
              client.println("<form  method=\"POST\"><br><strong>ESP32 AC WEB SERVER</strong></br><label for=\"time\">TIME:</label><input type=\"text\" id=\"time\" name=\"time\" placeholder=\"time\"> <br></br> <label for=\"temp\">TEMPERATURA:</label> <input type=\"number\" id=\"temp\" name=\"temp\" placeholder=\"temp\"> <br></br>");

              client.println("<label for=\"masterCtrl\">MASTER CONTROL:</label>");
              client.println("<select name=\"masterCtrl\" id=\"masterCtrl\">");
              client.print("<option value=\"AUTO\""); if (masterControl.equals("AUTO")) client.print(" selected "); client.println(">AUTO</option>");
              client.print("<option value=\"COOL\""); if (masterControl.equals("COOL")) client.print(" selected "); client.println(">COOL</option>");
              client.print("<option value=\"DRY\"");  if (masterControl.equals("DRY"))  client.print(" selected "); client.println(">DRY</option>");
              client.print("<option value=\"FAN\"");  if (masterControl.equals("FAN"))  client.print(" selected "); client.println(">FAN</option>");
              client.print("<option value=\"HEAT\""); if (masterControl.equals("HEAT")) client.print(" selected "); client.println(">HEAT</option>");
              client.println("</select>");

              client.println("<br></br>");

              client.println("<label for=\"fanCtrl\">FUN CONTROL:</label>");
              client.println("<select name=\"fanCtrl\" id=\"fanCtrl\">");
              client.print("<option value=\"AUTO\"");  if (fanControl.equals("AUTO"))  client.print(" selected "); client.println(">AUTO</option>");
              client.print("<option value=\"HIGH\"");  if (fanControl.equals("HIGH"))  client.print(" selected "); client.println(">HIGH</option>");
              client.print("<option value=\"MED\"");   if (fanControl.equals("MED"))   client.print(" selected "); client.println(">MED</option>");
              client.print("<option value=\"LOW\"");   if (fanControl.equals("LOW"))   client.print(" selected "); client.println(">LOW</option>");
              client.print("<option value=\"QUIET\""); if (fanControl.equals("QUIET")) client.print(" selected "); client.println(">QUIET</option>");
              client.println("</select>");

              client.println("<br></br>");
              client.println("<div class=\"ck-button\"><label><input type=\"checkbox\" id=\"powerOn\" name=\"powerOn\" class=\"ck-button\" value=\"ON\""); if (powerOn) client.print(" checked"); client.println("><span>POWER ON</span></label></div>");
              client.println("<div class=\"ck-button\"><label><input type=\"checkbox\" id=\"swing\" name=\"swing\" class=\"ck-button\" value=\"ON\""); if (swingState) client.print(" checked"); client.println("><span>Swing</span></label></div>");
              client.println("<div class=\"ck-button\"><label><input type=\"checkbox\" id=\"air_direction\" name=\"air_direction\" class=\"ck-button\" value=\"1\"><span>air direction</span> </label></div><br>");

              client.println("<input type=\"submit\" name=\"submit\">");
              client.println("</form>");
              // ---------------------------- END OF BODY ----------------------------
              client.println("</body>");
              // ---------------------------- END OF HTML ----------------------------
              client.println("</html>");

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

// ####################### FUNCTIONS #######################

String findVarFromPost(String postVarName, String postInfo) {
  postVarName = postVarName + "=";
  byte startIndex = postInfo.indexOf(postVarName);
  byte endIndex = postInfo.indexOf("&", startIndex + 1);
  String result = "";

  if (startIndex >= 0) {
    if (endIndex >= 0) {
      result = postInfo.substring(startIndex + postVarName.length(), endIndex);
    }
    else {
      result = postInfo.substring(startIndex + postVarName.length());
    }
  }

  Serial.print("\nVarName: "); Serial.println(postVarName.substring(0, postVarName.length() - 1));
  Serial.print("startIndex = "); Serial.print(startIndex); Serial.print("\t endIndex = "); Serial.println(endIndex);
  Serial.print("return: "); Serial.println(result);

  return result;
}

void startTimer() {
  /* Use 1st timer of 4 */
  /* 1 tick take 1/(CPU_FREQ_SECONDS/CPU_FREQ_MHZ) = 1us so we set divider ESP.getCpuFreqMHz() and count up */
  timer_ir = timerBegin(0, 80, true);

  /* Attach onTimer function to our timer */
  timerAttachInterrupt(timer_ir, &onTimer, true);

  /* Set alarm to call on function every 408us, 1 tick is 1us
    /* Repeat the alarm (third parameter) */
  timerAlarmWrite(timer_ir, 408, true);

  /* Start an alarm */
  timerAlarmEnable(timer_ir);
}

void stopTimer() {
  timerAlarmDisable(timer_ir); // stop alarm
  timerDetachInterrupt(timer_ir); // detach interrupt
  timerEnd(timer_ir); // end timer
}

// ####################### IO FUNCTIONS (In future this will be a library for suporting different protocols) #######################
