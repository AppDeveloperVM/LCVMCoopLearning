#include <ESP8266WiFi.h>
 
const char* ssid = "MiFibra-9B80";
const char* password = "Thi5P4rt7I5Get7ingCr4zy";

int rPin = 2;
int gPin = 5;

WiFiServer server(80);
void setup() {
  Serial.begin(115200);
  delay(10);

  //setting all pins
  pinMode(rPin, OUTPUT);
  digitalWrite(rPin, LOW);

  pinMode(gPin, OUTPUT);
  digitalWrite(gPin, LOW);
 
  // Connect to WiFi network
  Serial.println();
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
 
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected");
 
  // Start the server
  server.begin();
  Serial.println("Server started");
 
  // Print the IP address
  Serial.print("Use this URL to connect: ");
  Serial.print("http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
 
}
 
void loop() {
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }
 
  // Wait until the client sends some data
  Serial.println("new client");
  while(!client.available()){
    delay(1);
  }
 
  // Read the first line of the request
  String request = client.readStringUntil('\r');
  Serial.println(request);
  client.flush();
 
  // Match the request
  int pin_status = LOW;

  if (request.indexOf("/LED=RED") != -1) {
 
    if (request.indexOf("/STAT=ON") != -1) {
      pin_status = HIGH;
      digitalWrite(rPin, pin_status);
    }
    if (request.indexOf("/STAT=OFF") != -1){
      pin_status = LOW;
      digitalWrite(rPin, pin_status);
    }

  }

  if (request.indexOf("/LED=GREEN") != -1) {
 
    if (request.indexOf("/STAT=ON") != -1) {
      pin_status = HIGH;
      digitalWrite(gPin, pin_status);
    }
    if (request.indexOf("/STAT=OFF") != -1){
      pin_status = LOW;
      digitalWrite(gPin, pin_status);
    }

  }


  // Set ledPin according to the request
  
 
  // Return the response
  client.println("HTTP/1.1 200 OK");
  client.println("Content-Type: text/html");
  client.println(""); //  do not forget this one
  client.println("<!DOCTYPE HTML>");
  client.println("<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">");
  client.println("<html>");
 
  client.print("Led pin is now: ");
 
  if(pin_status == HIGH) {
    client.print("On");
  } else {
    client.print("Off");
  }
  client.println("<br><br>");
  
  client.println("Red PIN");
  client.println("<br><br>");
  client.println("<a href=\"/LED=RED/STAT=ON\"\"><button>Turn On </button></a>");
  client.println("<a href=\"/LED=RED/STAT=OFF\"\"><button>Turn Off </button></a><br />");
  client.println("<br><br>");
  
  client.println("Green PIN");
  client.println("<br><br>");
  client.println("<a href=\"/LED=GREEN/STAT=ON\"\"><button>Turn On </button></a>");
  client.println("<a href=\"/LED=GREEN/STAT=OFF\"\"><button>Turn Off </button></a><br />");  
  client.println("</html>");
 
  delay(1);
  Serial.println("Client disconnected");
  Serial.println("");
 
}
