#include <TFT_eSPI.h>
#include <WiFiManager.h>
#include <NTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Timezone.h>
#include <ESPAsyncWebSrv.h>
#include <TAMC_GT911.h>

#define TOUCH_INT 21
#define TOUCH_RST 25
#define TOUCH_SCL  32
#define TOUCH_SDA  33

#define TOUCH_WIDTH  480 
#define TOUCH_HEIGHT 320

int timeout = 120; // seconds to run for
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite tftSprite = TFT_eSprite(&tft);

TAMC_GT911 tp = TAMC_GT911(TOUCH_SDA, TOUCH_SCL, TOUCH_INT, TOUCH_RST, TOUCH_WIDTH, TOUCH_HEIGHT);

unsigned long pauseFor = 0;
bool touched = false;
unsigned long lastTouch = 0;
bool msgSent = true;

int last_hour = 0;
bool isBright = false; // Flag to track brightness state
unsigned long pressStartTime = 0;
String oldTime = "";
String oldDate = "";

/**
 * Input time in epoch format and return tm time format
 * by Renzo Mischianti <www.mischianti.org> 
 */
static tm getDateTimeByParams(long time){
    struct tm *newtime;
    const time_t tim = time;
    newtime = localtime(&tim);
    return *newtime;
}
/**
 * Input tm time format and return String with format pattern
 * by Renzo Mischianti <www.mischianti.org>
 */
static String getDateTimeStringByParams(tm *newtime, char* pattern = (char *)"%d/%m/%Y %H:%M:%S"){
    char buffer[30];
    strftime(buffer, 30, pattern, newtime);
    return buffer;
}
 
/**
 * Input time in epoch format format and return String with format pattern
 * by Renzo Mischianti <www.mischianti.org> 
 */
static String getEpochStringByParams(long time, char* pattern = (char *)"%H:%M:%S"){
//    struct tm *newtime;
    tm newtime;
    newtime = getDateTimeByParams(time);
    return getDateTimeStringByParams(&newtime, pattern);
}


WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "au.pool.ntp.org", 0, 60 * 60 * 1000);

TimeChangeRule aEDT = {"AEDT", First, Sun, Oct, 2, 660}; // UTC + 11 hours
TimeChangeRule aEST = {"AEST", First, Sun, Apr, 3, 600}; // UTC + 10 hours
Timezone ausET(aEDT, aEST);

AsyncWebServer server(88);

void connect_wifi(bool forceConfig = false) {
  
  WiFiManager wm;
  wm.setAPCallback(configModeCallback);
  wm.setConfigPortalTimeout(timeout);
  
  if (forceConfig) {
    if (!wm.startConfigPortal("esp32Clock")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      ESP.restart();
      delay(5000);
    }
  } else {
    if (!wm.autoConnect("esp32Clock")) {
      Serial.println("failed to connect and hit timeout");
      delay(3000);
      ESP.restart();
      delay(5000);
    }
  }

}

void configModeCallback(WiFiManager *myWiFiManager) {
  tft.fillScreen(TFT_BLACK);
  unsigned int textColor = TFT_WHITE;
  unsigned int bgColor = TFT_BLACK;
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  String msg = "WIFI Config Mode";
  tft.drawString(msg, 30, 97, 4);
  tft.setTextSize(1);
  msg = "Connect to: " + String(myWiFiManager->getConfigPortalSSID());
  tft.drawString(msg, 30, 157, 4);
  msg = "Navigate to: " + WiFi.softAPIP().toString(); 
  tft.drawString(msg, 30, 197, 4);
  msgSent = true;

}

void displayIP() {
  tft.setTextColor(TFT_WHITE);
  tft.fillScreen(TFT_BLACK);
  tft.setTextSize(2);
  String msg = "WIFI Connection";
  tft.drawString(msg, 20, 97, 4);
  tft.setTextSize(1);
  msg = "IP Address: " + WiFi.localIP().toString();
  tft.drawString(msg, 20, 157, 4);
  msg = "WEB UI:  http://" + WiFi.localIP().toString() + ":88";
  tft.drawString(msg, 20, 197, 4);
  pauseFor = millis() + 15000;
  msgSent = true;
}

// Function to convert HTML color string to TFT_eSPI color integer
uint16_t htmlColorToTftColor(const char *htmlColor) {
  // Skip '#' character
  htmlColor++;

  // Convert the hex values of R, G, and B components
  long rgb = strtol(htmlColor, nullptr, 16);

  // Extract individual color components
  int r = (rgb >> 16) & 0xFF;
  int g = (rgb >> 8) & 0xFF;
  int b = rgb & 0xFF;

  // Pack the RGB components into a 16-bit color value
  uint16_t tftColor = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);

  return tftColor;
}

void drawMultiLineText(const String &text, int x, int y, int lineHeight) {
  // Split the text into lines
  String remainingText = text;
  while (remainingText.length() > 0) {
    int lineEnd = remainingText.indexOf('\n');  // Find the end of the line
    if (lineEnd == -1) {
      lineEnd = remainingText.length();  // If no line break, use the entire text
    }

    String line = remainingText.substring(0, lineEnd);  // Extract the line
    remainingText = remainingText.substring(lineEnd + 1);  // Remove the line from the original text

    tft.drawString(line, x, y, 4);  // Draw the line at the specified coordinates
    y += lineHeight;  // Move to the next line
  }
}

String htmlPage() {
  String html = "<html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css'>";
  html += "<script src='https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/js/bootstrap.bundle.min.js'></script>";
  html += "<style>";
  html += "body {";
  html += "  font-family: Arial, sans-serif;";
  html += "}";
  html += "</style>";
  html += "<title>NTP Clock Message Centre</title>";
  html += "<script>";
  html += "function submitForm(form) {";
  html += "var xhr = new XMLHttpRequest();";
  html += "var formData = new FormData(document.getElementById(form));";
  html += "xhr.onreadystatechange = function() {";
  html += "if (xhr.readyState == XMLHttpRequest.DONE) {";
  html += "document.getElementById('responseDiv').innerHTML = xhr.responseText;";
  html += "}";
  html += "};";
  html += "xhr.open('POST', '/update', true);";
  html += "xhr.send(formData);";
  html += "return false;";
  html += "}";

    // Function to handle display on/off
  html += "function controlDisplay(action) {";
  html += "var xhr = new XMLHttpRequest();";
  html += "xhr.onreadystatechange = function() {";
  html += "if (xhr.readyState == XMLHttpRequest.DONE) {";
  html += "document.getElementById('responseDiv').innerHTML = xhr.responseText;";
  html += "}";
  html += "};";
  html += "xhr.open('GET', '/screen' + action, true);";
  html += "xhr.send();";
  html += "}";
  
  html += "</script></head><body class='container'>";
  html += "<div class='row justify-content-center border'>";
  html += "<div class='col-md-12 mt-5'>";
  
  // Buttons to control display
  html += "<div class='row'>";
  html += "<div class='col-6 text-center'>";
  html += "<button class='btn btn-success mt-3' onclick='controlDisplay(\"on\")'>Turn On Display</button>";
  html += "</div>";
  html += "<div class='col-6 text-center'>";
  html += "<button class='btn btn-danger mt-3' onclick='controlDisplay(\"off\")'>Turn Off Display</button>";
  html += "</div>";
  html += "</div>";
  
  html += "<form id='updateForm' onsubmit='return submitForm(this.id)'>";
  html += "<label for='text' class='form-label'>Enter Text:</label>";
  html += "<textarea name='text' class='form-control' placeholder='Enter Text to Send here'></textarea>";
  html += "<label for='x' class='form-label'>X Pos:</label>";
  html += "<input type='text' name='x' value='5' class='form-control'>";
  html += "<label for='y' class='form-label'>Y Pos:</label>";
  html += "<input type='text' name='y' value='5' class='form-control'>";
  html += "<label for='text-color' class='form-label'>Text Color:</label>";
  html += "<input type='color' value='#FFFFFF' name='text-color' class='form-control'>";
  html += "<label for='bg-color' class='form-label'>Background Color:</label>";
  html += "<input type='color' value='#000000' name='bg-color' class='form-control'>";
  html += "<label for='text-size' class='form-label'>Text Size:</label>";
  html += "<input type='text' name='text-size' value='2' class='form-control'>";
  html += "<label for='duration' class='form-label'>Duration (S):</label>";
  html += "<input type='text' name='duration' value='15' class='form-control'>";
  html += "<button type='submit' class='btn btn-primary mt-3'>Submit</button>";
  html += "</form></div></div>";
  
  html += "<div class='row border'>";
  html += "<form class='form-inline col-md-3' id='QuickMessage1' onsubmit='return submitForm(this.id)'>";
  html += "<input type='hidden' name='text-color'value='#FFFFFF'>";
  html += "<input type='hidden' name='bg-color' value='#FF0000'>";
  html += "<input type='hidden' name='text' value='Please Lower\nMicrophone'>";
  html += "<input type='hidden' name='text-size' value='2' class='form-control'>";
  html += "<input type='hidden' name='duration' value='15' class='form-control'>";
  html += "<input type='hidden' name='y' value='105'>";
  html += "<input type='hidden' name='x' value='90'>";  
  html += "<button type='submit' class='btn btn-primary mt-3 form-control'>Lower Mic (15s)</button>";
  html += "</form>";

  html += "<form class='form-inline col-md-3' id='QuickMessage2' onsubmit='return submitForm(this.id)'>";
  html += "<input type='hidden' name='text-color'value='#FFFFFF'>";
  html += "<input type='hidden' name='bg-color' value='#FF0000'>";
  html += "<input type='hidden' name='text' value='Please Raise\nMicrophone'>";
  html += "<input type='hidden' name='text-size' value='2' class='form-control'>";
  html += "<input type='hidden' name='duration' value='15' class='form-control'>";
  html += "<input type='hidden' name='y' value='105'>";
  html += "<input type='hidden' name='x' value='90'>";  
  html += "<button type='submit' class='btn btn-primary mt-3 form-control'>Raise Mic (15s)</button>";
  html += "</form>";

  html += "<form class='form-inline col-md-3' id='QuickMessage3' onsubmit='return submitForm(this.id)'>";
  html += "<input type='hidden' name='text-color'value='#000000'>";
  html += "<input type='hidden' name='bg-color' value='#47c819'>";
  html += "<input type='hidden' name='text' value='Singing Item\n        Ready'>";
  html += "<input type='hidden' name='text-size' value='2' class='form-control'>";
  html += "<input type='hidden' name='duration' value='30' class='form-control'>";
  html += "<input type='hidden' name='y' value='100'>";
  html += "<input type='hidden' name='x' value='100'>";  
  html += "<button type='submit' class='btn btn-primary mt-3 form-control'>Item Ready (30s)</button>";
  html += "</form>";

  html += "<form class='form-inline col-md-3' id='QuickMessage4' onsubmit='return submitForm(this.id)'>";
  html += "<input type='hidden' name='text-color'value='#000000'>";
  html += "<input type='hidden' name='bg-color' value='#f0b32d'>";
  html += "<input type='hidden' name='text' value='Skit Ready'>";
  html += "<input type='hidden' name='text-size' value='2' class='form-control'>";
  html += "<input type='hidden' name='duration' value='30' class='form-control'>";
  html += "<input type='hidden' name='y' value='120'>";
  html += "<input type='hidden' name='x' value='130'>";  
  html += "<button type='submit' class='btn btn-primary mt-3 form-control'>Skit Ready(30s)</button>";
  html += "</form>";
  html += "</div>";
  
  html += "<div class='row border'>";
  html += "<div id='responseDiv' class='mt-3'></div>";
  html += "</div></body></html>";

  return html;
}


void handleRoot(AsyncWebServerRequest *request) {

  request->send(200, "text/html", htmlPage());
}

void handleUpdate(AsyncWebServerRequest *request) {
  unsigned long pause = 5000;
  unsigned int textColor = TFT_WHITE;
  unsigned int bgColor = TFT_BLACK;
  int x = 10;
  int y = 10;
  String txt = "";
  int textSize = 2;
  if (request->hasParam("duration"), true) {
      String duration = (request->getParam("duration", true)->value());
      pause = strtol(duration.c_str(), NULL, 10) * 1000;
  }
  if (request->hasParam("text-color"), true) {
    String txtcolor = (request->getParam("text-color", true)->value());
    textColor = htmlColorToTftColor(txtcolor.c_str());

      
  }
  if (request->hasParam("bg-color"), true) {
    String bgcolor = (request->getParam("bg-color", true)->value());
    bgColor = htmlColorToTftColor(bgcolor.c_str());
  }

  if (request->hasParam("text-size"), true) {
    String size = (request->getParam("text-size", true)->value());
    textSize = size.toInt();
  }

  if (request->hasParam("x"), true) {
    String xPos = (request->getParam("x", true)->value());
    x = xPos.toInt();
  }

  if (request->hasParam("y"), true) {
    String yPos = (request->getParam("y", true)->value());
    y = yPos.toInt();
  }
  
  if (request->hasParam("text"), true) {
    txt = request->getParam("text", true)->value();
    // Display the entered text on the LCD
    tftSprite.deleteSprite();
    tft.setTextColor(textColor);
    tft.fillScreen(bgColor);
    tft.setTextSize(textSize);
    drawMultiLineText(txt, x, y, textSize * 20 + 10);
  }
  request->send(200, "text/plain", "Update received: "+ txt);
  pauseFor = millis() + pause;
  msgSent = true;
}

void notFound(AsyncWebServerRequest *request) {
    request->send(404, "text/plain", "Not found");
}

void setup() {

  pinMode(27, OUTPUT);//打钟
  digitalWrite(27,HIGH);
  pinMode(17, OUTPUT);//打钟
  digitalWrite(17,HIGH);
  pinMode(16, OUTPUT);//打钟
  digitalWrite(16,HIGH);
  pinMode(4, OUTPUT);//打钟
  digitalWrite(4,HIGH);
  tp.begin();
  tp.setRotation(1);
  
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  tftSprite.setColorDepth(1);
  tftSprite.createSprite(435, 120);
  
  tftSprite.setTextColor(TFT_WHITE);
  
  //tftSprite.setTextFont(2);
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  connect_wifi();
  delay(1000);

  timeClient.begin();
  delay(250);
  if (timeClient.update()) {
    Serial.print("Adjust local clock");
    unsigned long epoch = timeClient.getEpochTime();
    setTime(epoch);
  } else {
    Serial.print("NTP Update not WORK!!");
  }

  // Setup the web server routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    handleUpdate(request);
  });
  server.on("/screenon", HTTP_GET, [](AsyncWebServerRequest *request) {
        digitalWrite(27,HIGH);
        request->send(200, "text/html", "Sreen On");
  });
  server.on("/screenoff", HTTP_GET, [](AsyncWebServerRequest *request) {
        digitalWrite(27,LOW);
        request->send(200, "text/html", "Sreen Off");
  });
  // Start the web server
  server.onNotFound(notFound);
  
  server.begin();
}



void displayDate(String currentDate) {
    tft.setTextSize(1);
    tft.setTextColor(TFT_BLACK);
    tft.drawString(oldDate, 110, 227, 6);
    tft.setTextColor(htmlColorToTftColor("#777777"));
    tft.drawString(currentDate, 110, 227, 6);
    oldDate = currentDate;
}


void loop() {
  long timeEpoch = ausET.toLocal(now());

  if ((minute(timeEpoch) == 0) && last_hour != hour(timeEpoch)) {
    unsigned long epoch = timeClient.getEpochTime();
    setTime(epoch);
    last_hour = hour(timeEpoch);
    Serial.println("Time Updated");
  }

  String currentTime = getEpochStringByParams(timeEpoch, (char *)"%H:%M:%S");
  String currentDate = getEpochStringByParams(timeEpoch, (char *)"%d-%m-%Y");

  tp.read();
  if (tp.isTouched) {
    if (!touched) {
      pressStartTime = millis();
    }
    lastTouch = millis();
    touched = true;
  }
  if (touched && (millis() - lastTouch) >= 200) {
    unsigned long pressDuration = millis() - pressStartTime;
    touched = false;
    if (pressDuration >= 10000) {
        if (!isBright) {
          digitalWrite(27,HIGH);
        }
        tftSprite.deleteSprite();
       connect_wifi(true);
    } else if (pressDuration >= 5000) {
        if (!isBright) {
          digitalWrite(27,HIGH);
        }
        tftSprite.deleteSprite();
        displayIP();
    } else if (pressDuration > 0) {
        if (msgSent) {
        tftSprite.createSprite(435, 120);
        tftSprite.setTextColor(TFT_WHITE);
        tft.fillScreen(TFT_BLACK);
        displayDate(currentDate);
        msgSent = false;
        pauseFor = 0;
        } else {
        isBright = !isBright;
        if (isBright) {
          digitalWrite(27,HIGH); 
        } else {
          digitalWrite(27,LOW);
        }
      }
    }
  }
  if ((millis() > pauseFor) && (oldTime != currentTime)) {
    
    if (msgSent) {
      tftSprite.createSprite(435, 120);
      tftSprite.setTextColor(TFT_WHITE);
      tft.fillScreen(TFT_BLACK);
      displayDate(currentDate);
      msgSent = false; 
    }
    tftSprite.fillSprite(TFT_BLACK);
    tftSprite.setTextSize(2);
    tftSprite.drawString(currentTime, 0, 0, 7);
    tftSprite.pushSprite(20, 97);
    oldTime = currentTime;
    if (oldDate != currentDate) {
        displayDate(currentDate);
     }
  }
}
