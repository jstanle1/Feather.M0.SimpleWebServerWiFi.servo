
/*
  WiFi Web Server LED Blink

 A simple web server that lets you blink an LED via the web.
 This sketch will print the IP address of your WiFi Shield (once connected)
 to the Serial monitor. From there, you can open that address in a web browser
 to turn on and off the LED on pin 9.

 If the IP address of your shield is yourAddress:
 http://yourAddress/H turns the LED on
 http://yourAddress/L turns it off

 This example is written for a network using WPA encryption. For
 WEP or WPA, change the WiFi.begin() call accordingly.

 Circuit:
 * WiFi shield attached
 * LED attached to pin 9

 created 25 Nov 2012
 by Tom Igoe
 */

/*
 M0 pins
neopix  14 a0
servo1r 15 a1
servo2r 16 a2
servo3r 17 a3
handsh  18 a4 (outgoing handshake)
        19 a5 <-- maybe another LED, bright/dim as servo moves…
        24, 23, 22 -- spi/wifi
servo1  0  height
servo2  1  tilt
        13 -- led
servo3  12 grip
pwml    11
pwmr    10 incoming handshake with interrupt
pwmr    9 a7
dirr    6
dirl    5
        21, 28 -- i2c
*/

#include <SPI.h>
#include <WiFi101.h>
#include <Servo.h>
#include <Adafruit_NeoPixel.h>
#include <SAMD21turboPWM.h>

#include "arduino_secrets.h" 
///////please enter your sensitive data in the Secret tab/arduino_secrets.h
char ssid[] = "";               // your network SSID (name)
char pass[] = "";               // your network password (use for WPA, or use as key for WEP)

int status = WL_IDLE_STATUS;
WiFiServer server(80);

Adafruit_NeoPixel neo = Adafruit_NeoPixel(8, 14);

// slider ids from web page
#define SLIDER_HEIGHT   1
#define SLIDER_TILT     2
#define SLIDER_GRIP     3
#define SLIDER_SPEED    4
#define SLIDER_ROTATE   5

// microseconds min/max found during servo calibration
#define HEIGHT_MIN  700
#define HEIGHT_MAX  1600
#define TILT_MIN    1300
#define TILT_MAX    1800
#define GRIP_MIN    500
#define GRIP_MAX    2300

#define SHEIGHT   0     // 1900 level
#define STILT     1     // 1200 level
#define SGRIP     12    // 2400 closed
Servo s_height;
Servo s_tilt;
Servo s_grip;

int height;
int tilt;
int grip;

#define SERVO_MILLIS 500
int servo_millis;

#define DIRR    6
#define DIRL    5
#define PWML    11
#define PWMR    9
TurboPWM pwml;
TurboPWM pwmr;

// motor speeds
// chassis and motors currently uncalibrated
// so omega factor of D/2 is not meaningful
// will build in a fudge factor to give rotation reasonable values
#define VMAX    400   // max allowable pwm value

float vell;
float velr;

#define FWD     0
#define REV     1
bool dirl;
bool dirr;

float vchassis;
float omega;

int brightness = 100;
int neo_idx;

// GET command parsing
int idx; // string parsing index

void setup() {
  int i;
  
  Serial.begin(9600);                  // initialize serial communication
  //while (!Serial)
  //  ;
    
  //pinMode(9, OUTPUT);                // handled by PWM outputs
  //pinMode(11, OUTPUT);

  // direction LOW -> move forward, for both left and right
  pinMode(DIRR, OUTPUT);
  digitalWrite(DIRR, FWD);
  pinMode(DIRL, OUTPUT);
  digitalWrite(DIRL, FWD);

  pwml.setClockDivider(2, false);      // 96MHz clock divided by 1
  pwml.timer(2, 16, 100, true);        // Use timer 2, divide clock by 16, resolution 100, single-slope PWM
  pwml.analogWrite(PWML, 0);           // PWM frequency is now 15KHz
  pwmr.setClockDivider(2, false);      // 96MHz clock divided by 1
  pwmr.timer(1, 16, 100, true);        // Use timer 1, divide clock by 16, resolution 100, single-slope PWM
  pwmr.analogWrite(PWMR, 0);           // PWM frequency is now 15KHz

  // corresponding velocity values
  vell = 0;
  dirl = FWD;
  velr = 0;
  dirr = FWD;

  vchassis = 0;
  omega = 0;

#if 0   // motor PWM/direction test
  digitalWrite(DIRL, HIGH);
  while (1)
  for (int i = 0; i < 600; i += 100) {
    Serial.println(i);
    pwmr.analogWrite(PWMR, i);
    pwml.analogWrite(PWML, i);
    delay(1000);
  }
#endif

  neo.begin();
  neo.setBrightness(10);    // much more than this is killing
  neo.setPixelColor(0, neo.Color(128, 0, 255));  // purplish
  neo.show();
  neo_idx = 0;

  // height is 700 for all the way up, 1600 for all the way down
  height = HEIGHT_MAX;
  s_height.attach(SHEIGHT);
  s_height.writeMicroseconds(height);
  // tilt range is about 1300 to 1800 -- 1300 is straight along arm, 1800 is about 90 degrees to arm
  // watch that tilt arm can be pulled back to hit pylon of lift arm, jams tilt further upward
  // also, at min tilt and max height the end of gripper hits the floor...
  // so set tilt about midway between min and max for kicks
  tilt = TILT_MIN + (TILT_MAX - TILT_MIN) / 2;
  s_tilt.attach(STILT);
  s_tilt.writeMicroseconds(tilt);
  // grip closed is 2300 ish, open is about 500
  grip = GRIP_MIN;
  s_grip.attach(SGRIP);
  s_grip.writeMicroseconds(grip);

#if 0   // tilt cal
  while(1) {
    tilt += 100;
    if (tilt > 1800)
      tilt = 1200;
    Serial.println(tilt);
    s_tilt.writeMicroseconds(tilt);
    delay(1000);
  }
#endif

#if 0   // height cal -- 600 is all the way up
  while(1) {
    height += 100;
    if (height > 1600)
      height = 700;
    Serial.println(height);
    s_height.writeMicroseconds(height);
    delay(1000);
  }
#endif
  
  //Configure pins for Adafruit ATWINC1500 Feather
  WiFi.setPins(8,7,4,2);

  // check for the presence of the shield:
  if (WiFi.status() == WL_NO_SHIELD) {
    Serial.println("WiFi shield not present");
    while (true);       // don't continue
  }

  // attempt to connect to WiFi network:
  while ( status != WL_CONNECTED) {
    Serial.print("Attempting to connect to Network named: ");
    Serial.println(ssid);                   // print the network name (SSID);

    // Connect to WPA/WPA2 network. Change this line if using open or WEP network:
    status = WiFi.begin(ssid, pass);
    // wait 2 seconds for connection:
    delay(2000);
  }
  server.begin();                           // start the web server on port 80
  printWiFiStatus();                        // you're connected now, so print out the status

  servo_millis = millis() + SERVO_MILLIS;
}

void clientPage(WiFiClient &client)
{
    // HTTP headers always start with a response code (e.g. HTTP/1.1 200 OK)
    // and a content-type so the client knows what's coming, then a blank line:
    client.println("HTTP/1.1 200 OK");
    client.println("Content-type:text/html");
    client.println();
    client.print("<style> \
    .slider { \
      -webkit-appearance: none; \
      width: 30%; \
      height: 25px; \
      background: #d3d3d3; \
      cursor: pointer; \
    } \
    </style>");

    // the content of the HTTP response follows the header:
    client.print("<script>");
      client.print("function buttonHit() { \
          document.getElementById(\"slide\").value='0'; \
          document.getElementById(\"sliderAmount\").innerHTML='0'; \
          var xmlhttp = new XMLHttpRequest(); \
          xmlhttp.open(\"GET\", \"/L\", true); \
          xmlhttp.send(); \
      }");
      client.print("function updateSlider(ln, slideAmount, sliderd) { \
          var sliderDiv = document.getElementById(sliderd); \
          sliderDiv.innerHTML = slideAmount; \
          var xmlhttp = new XMLHttpRequest(); \
          xmlhttp.open(\"GET\", ln+slideAmount, true); \
          xmlhttp.send(); \
      }");
    client.print("</script>");
    client.print("Height: &nbsp; &nbsp;");
    client.print("<input id=\"slide\" class=\"slider\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"0\" oninput=\"updateSlider('/H?s1=', this.value, 'sliderAmount')\"> &nbsp; &nbsp;");
    client.print("<div style=\"display: inline-block;\" id=\"sliderAmount\"> 0 </div>");
    client.print("<br>");
    client.print("Tilt: &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;");
    client.print("<input id=\"slide2\" class=\"slider\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"0\" oninput=\"updateSlider('/H?s2=', this.value, 'sliderAmount2')\"> &nbsp; &nbsp;");
    client.print("<div style=\"display: inline-block;\" id=\"sliderAmount2\"> 0 </div>");
    client.print("<br>");
    client.print("Grip: &nbsp; &nbsp; &nbsp; &nbsp;");
    client.print("<input id=\"slide3\" class=\"slider\" type=\"range\" min=\"0\" max=\"100\" step=\"1\" value=\"0\" oninput=\"updateSlider('/H?s3=', this.value, 'sliderAmount3')\"> &nbsp; &nbsp;");
    client.print("<div style=\"display: inline-block;\" id=\"sliderAmount3\"> 0 </div>");
    client.print("<br>");
    client.print("<br>");
    client.print("Speed: &nbsp; &nbsp; &nbsp;");
    client.print("<input id=\"slide4\" class=\"slider\" type=\"range\" min=\"-100\" max=\"100\" step=\"1\" value=\"0\" oninput=\"updateSlider('/H?s4=', this.value, 'sliderAmount4')\"> &nbsp; &nbsp;");
    client.print("<div style=\"display: inline-block;\" id=\"sliderAmount4\"> 0 </div>");
    client.print("<br>");
    client.print("Angle: &nbsp; &nbsp; &nbsp");
    client.print("<input id=\"slide5\" class=\"slider\" type=\"range\" min=\"-100\" max=\"100\" step=\"1\" value=\"0\" oninput=\"updateSlider('/H?s5=', this.value, 'sliderAmount5')\"> &nbsp; &nbsp;");
    client.print("<div style=\"display: inline-block;\" id=\"sliderAmount5\"> 0 </div>");
    client.print("<br>");
    client.print("<br>");
    client.print("<button onclick=\"buttonHit()\">Reset</button>");

    // The HTTP response ends with another blank line:
    client.println();
}

void setSpeeds(float l, float r)
{
  if (l < 0.0) {
    dirl = REV;
    l = -l;
  } else {
    dirl = FWD;
  }

  if (r < 0.0) {
    dirr = REV;
    r = -r;
  } else {
    dirr = FWD;
  }

  digitalWrite(DIRL, dirl);
  digitalWrite(DIRR, dirr);

  pwml.analogWrite(PWML, (int)l);
  pwmr.analogWrite(PWMR, (int)r);
}

void handleSliderInput(char slider, int data)
{
  float val;
  float ftemp = (float)data / 100.0f;

  Serial.println(slider);
  Serial.println(data);
  switch (slider) {
    case SLIDER_SPEED:
      // chassis speed is +/- 100 representing percent of max allowable value
      // at 100%, want vell and velr to be max allowable values -- VMAX
      vchassis = (float)VMAX * ftemp;
      Serial.println(vchassis);
      velr = vchassis - omega;
      vell = vchassis + omega;
      setSpeeds(vell, velr);
      break;

    case SLIDER_ROTATE:
      omega = (float)VMAX * ftemp;
      velr = vchassis - omega;
      vell = vchassis + omega;
      setSpeeds(vell, velr);
      break;

    case SLIDER_HEIGHT:
      // slider height is lower as servo setting is greater -- reverse sense of input
      ftemp = 1.0f - ftemp;
      // convert to servo range
      height = (float)HEIGHT_MIN + ftemp * ((float)HEIGHT_MAX - (float)HEIGHT_MIN);
      s_height.writeMicroseconds(height);
      break;

    case SLIDER_TILT:
      tilt = (float)TILT_MIN + ftemp * ((float)TILT_MAX - (float)TILT_MIN);
      s_tilt.writeMicroseconds(tilt);
      break;

    case SLIDER_GRIP:
      grip = (float)GRIP_MIN + ftemp * ((float)GRIP_MAX - (float)GRIP_MIN);
      s_grip.writeMicroseconds(grip);
      break;

    default:
      break;
  }
}

void loop() {
  WiFiClient client = server.available();   // listen for incoming clients

  if (client) {                             // if you get a client,
    Serial.println("new client");           // print a message out the serial port
    String currentLine = "";                // make a String to hold incoming data from the client
    while (client.connected()) {            // loop while the client's connected
      if (client.available()) {             // if there's bytes to read from the client,
        char c = client.read();             // read a byte, then
        Serial.write(c);                    // print it out the serial monitor
        if (c == '\n') {                    // if the byte is a newline character

          // if the current line is blank, you got two newline characters in a row.
          // that's the end of the client HTTP request, so send a response:
          if (currentLine.length() == 0) {
            clientPage(client);
            break;
          } else {                          // if you got a newline, then clear currentLine:
            currentLine = "";
          }
        } else if (c != '\r') {             // if you got anything else but a carriage return character,
          currentLine += c;                 // add it to the end of the currentLine
        } else {
          // got a \r -- it's end of the line, check line for a GET command 
          // Check to see if the client request was "GET /H" or "GET /L":
          if (currentLine.startsWith("GET /H")) {
            Serial.println(currentLine);
            // on GET /H, parse GET parameter for slider number and slider value
            idx = currentLine.lastIndexOf("=");
            if (idx != -1) {
              String num = currentLine.substring(idx+1, currentLine.length());
              char slider_num = currentLine[idx-1] - '0';
              //Serial.println(slider_num);
              //Serial.println(num.toInt());
              // deal with slider data -- converted to ints
              handleSliderInput(slider_num, num.toInt());
            }
          }
          if (currentLine.startsWith("GET /L")) {
            // re-init motion variables
            vell = 0;
            dirl = FWD;
            velr = 0;
            dirr = FWD;
            vchassis = 0;
            omega = 0;
            // actually turn off motors...
            setSpeeds(vell, velr);

            // set servos to initial values
            height = HEIGHT_MAX;
            s_height.writeMicroseconds(height);
            tilt = TILT_MIN + (TILT_MAX - TILT_MIN) / 2;
            s_tilt.writeMicroseconds(tilt);
            grip = GRIP_MIN;
            s_grip.writeMicroseconds(grip);
          }
        }
      }
    }
    // close the connection:
    client.stop();
    Serial.println("client disonnected");
  }

  if (servo_millis < millis()) {
    servo_millis = millis() + SERVO_MILLIS;

#if 0
    height += 100;
    if (height > HEIGHT_MAX)
      height = HEIGHT_MIN;
    s_height.writeMicroseconds(height);
#endif

#if 0
    tilt += 100;
    if (tilt > TILT_MAX)
      tilt = TILT_MIN;
    s_tilt.writeMicroseconds(tilt);
#endif

#if 0
    // fully closed -> 2400us
    // range is something like 500 -> 2200 (or 2300)
    grip += 100;
    if (grip > GRIP_MAX)
      grip = GRIP_MIN;
    s_grip.writeMicroseconds(grip);
#endif

    neo_idx++;
    if (neo_idx > 7)
      neo_idx = 0;
    neo.clear();
    neo.setPixelColor(neo_idx, neo.Color(grip%255, height%255, tilt%255));
    neo.show();
  }
}

void printWiFiStatus() {
  // print the SSID of the network you're attached to:
  Serial.print("SSID: ");
  Serial.println(WiFi.SSID());

  // print your WiFi shield's IP address:
  IPAddress ip = WiFi.localIP();
  Serial.print("IP Address: ");
  Serial.println(ip);

  // print the received signal strength:
  long rssi = WiFi.RSSI();
  Serial.print("signal strength (RSSI):");
  Serial.print(rssi);
  Serial.println(" dBm");
  // print where to go in a browser:
  Serial.print("To see this page in action, open a browser to http://");
  Serial.println(ip);
}
