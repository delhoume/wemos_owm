#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include "pixel-millenium5.h"
#include "LatoMedium8.h"
#include "LatoMedium12.h"
#include "digital12.h"
#include "invader.h"
#include "wemoslogo.h"
#include "wifilogo.h"  


#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <WiFiManager.h>         //https://github.com/tzapu/WiFiManager
#include <ESP8266HTTPClient.h>
#include <ArduinoJson.h>  // 5.13, not 6
#include <FS.h>
#include <ArduinoOTA.h>

#include <WiFiUdp.h>
#include <Timezone.h>

#include <Wire.h>

unsigned int localPort = 2390;      // local port to listen for UDP packets
IPAddress timeServerIP; // time.nist.gov NTP server address
const char* ntpServerName = "time.google.com";
const int NTP_PACKET_SIZE = 48; // NTP time stamp is in the first 48 bytes of the message
byte packetBuffer[ NTP_PACKET_SIZE]; //buffer to hold incoming and outgoing packets

WiFiUDP udp;

struct WeatherInfo {
  float temperature;
  float humidity;
  boolean error;
//  char description[16];
  char fulldescription[32];
  char dt[32];
  char icon[8];
  long time;
};

// can be up to 40 / 5 days 
const uint8_t numForecast = 20;
struct WeatherInfo OWMForecast[numForecast];
struct WeatherInfo NTPInfo;

// Central European Time (Frankfurt, Paris)
TimeChangeRule CEST = {"CEST", Last, Sun, Mar, 2, 120};     // Central European Summer Time
TimeChangeRule CET = {"CET ", Last, Sun, Oct, 3, 60};       // Central European Standard Time
Timezone CE(CEST, CET);
TimeChangeRule *tcr;

const char* days[] = { "Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"  };
const char* longdays[] = { "Dimanche", "Lundi", "Mardi", "Mercredi", "Jeudi", "Vendredi", "Samedi"  };
  
WiFiClient wifiClient;

// openweathermap
HTTPClient owm;

#define OLED_RESET LED_BUILTIN
Adafruit_SSD1306 display(OLED_RESET);


// absolute
inline uint8_t centerVA(uint8_t height) {
  return (display.height() - height) / 2;
}

inline uint8_t centerHA(uint8_t width) {
  return (display.width() - width) / 2;
}

inline void displayWifiLogo() {
  display.drawXBitmap(centerHA(WiFi_width), centerVA(WiFi_height) - 4, 
                     WiFi_Logo, WiFi_width, WiFi_height, WHITE);
}
 
inline void displayText(const char* text, uint16_t xpos, uint16_t ypos){
  display.setCursor(xpos, ypos);
  display.print(text);
}

void displayTextCenterH(const char* text, uint16_t ypos);
 
void configModeCallback (WiFiManager *myWiFiManager) {
  display.clearDisplay();
  displayWifiLogo();
  char buffer[32];
  sprintf(buffer, "SSID %s", myWiFiManager->getConfigPortalSSID().c_str());
  displayTextCenterH(buffer, 40);
  display.display();
}

const char* owmApiKey = "95b2ed7cdfc0136948c0a9d499f807eb";
const char* owmId     = "6613168";
const char* owmLang   = "fr"; // fr encodes accents in utf8, not easy to convert

char owmURL[128];

void initOWM() {
  sprintf(owmURL, 
	  "http://api.openweathermap.org/data/2.5/forecast?id=%s&lang=%s&appid=%s&units=metric&cnt=%d", 
	  owmId, 
	  owmLang, 
	  owmApiKey,
	  numForecast);
}

void initNTP() {
  NTPInfo.error = true;
}

/* UTF-8 to ISO-8859-1/ISO-8859-15 mapper.
 * Return 0..255 for valid ISO-8859-15 code points, 256 otherwise.
*/
static inline unsigned int to_latin9(const unsigned int code) {
    /* Code points 0 to U+00FF are the same in both. */
    if (code < 256U)
        return code;
    switch (code) {
    case 0x0152U: return 188U; /* U+0152 = 0xBC: OE ligature */
    case 0x0153U: return 189U; /* U+0153 = 0xBD: oe ligature */
    case 0x0160U: return 166U; /* U+0160 = 0xA6: S with caron */
    case 0x0161U: return 168U; /* U+0161 = 0xA8: s with caron */
    case 0x0178U: return 190U; /* U+0178 = 0xBE: Y with diaresis */
    case 0x017DU: return 180U; /* U+017D = 0xB4: Z with caron */
    case 0x017EU: return 184U; /* U+017E = 0xB8: z with caron */
    case 0x20ACU: return 164U; /* U+20AC = 0xA4: Euro */
    default:      return 256U;
    }
}

/* Convert an UTF-8 string to ISO-8859-15.
 * All invalid sequences are ignored.
 * Note: output == input is allowed,
 * but   input < output < input + length
 * is not.
 * Output has to have room for (length+1) chars, including the trailing NUL byte.
*/
size_t utf8_to_latin9(char *const output, const char *const input, const size_t length) {
    unsigned char             *out = (unsigned char *)output;
    const unsigned char       *in  = (const unsigned char *)input;
    const unsigned char *const end = (const unsigned char *)input + length;
    unsigned int               c;

    while (in < end)
        if (*in < 128)
            *(out++) = *(in++); /* Valid codepoint */
        else
        if (*in < 192)
            in++;               /* 10000000 .. 10111111 are invalid */
        else
        if (*in < 224) {        /* 110xxxxx 10xxxxxx */
            if (in + 1 >= end)
                break;
            if ((in[1] & 192U) == 128U) {
                c = to_latin9( (((unsigned int)(in[0] & 0x1FU)) << 6U)
                             |  ((unsigned int)(in[1] & 0x3FU)) );
                if (c < 256)
                    *(out++) = c;
            }
            in += 2;

        } else
        if (*in < 240) {        /* 1110xxxx 10xxxxxx 10xxxxxx */
            if (in + 2 >= end)
                break;
            if ((in[1] & 192U) == 128U &&
                (in[2] & 192U) == 128U) {
                c = to_latin9( (((unsigned int)(in[0] & 0x0FU)) << 12U)
                             | (((unsigned int)(in[1] & 0x3FU)) << 6U)
                             |  ((unsigned int)(in[2] & 0x3FU)) );
                if (c < 256)
                    *(out++) = c;
            }
            in += 3;

        } else
        if (*in < 248) {        /* 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (in + 3 >= end)
                break;
            if ((in[1] & 192U) == 128U &&
                (in[2] & 192U) == 128U &&
                (in[3] & 192U) == 128U) {
                c = to_latin9( (((unsigned int)(in[0] & 0x07U)) << 18U)
                             | (((unsigned int)(in[1] & 0x3FU)) << 12U)
                             | (((unsigned int)(in[2] & 0x3FU)) << 6U)
                             |  ((unsigned int)(in[3] & 0x3FU)) );
                if (c < 256)
                    *(out++) = c;
            }
            in += 4;

        } else
        if (*in < 252) {        /* 111110xx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (in + 4 >= end)
                break;
            if ((in[1] & 192U) == 128U &&
                (in[2] & 192U) == 128U &&
                (in[3] & 192U) == 128U &&
                (in[4] & 192U) == 128U) {
                c = to_latin9( (((unsigned int)(in[0] & 0x03U)) << 24U)
                             | (((unsigned int)(in[1] & 0x3FU)) << 18U)
                             | (((unsigned int)(in[2] & 0x3FU)) << 12U)
                             | (((unsigned int)(in[3] & 0x3FU)) << 6U)
                             |  ((unsigned int)(in[4] & 0x3FU)) );
                if (c < 256)
                    *(out++) = c;
            }
            in += 5;

        } else
        if (*in < 254) {        /* 1111110x 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx 10xxxxxx */
            if (in + 5 >= end)
                break;
            if ((in[1] & 192U) == 128U &&
                (in[2] & 192U) == 128U &&
                (in[3] & 192U) == 128U &&
                (in[4] & 192U) == 128U &&
                (in[5] & 192U) == 128U) {
                c = to_latin9( (((unsigned int)(in[0] & 0x01U)) << 30U)
                             | (((unsigned int)(in[1] & 0x3FU)) << 24U)
                             | (((unsigned int)(in[2] & 0x3FU)) << 18U)
                             | (((unsigned int)(in[3] & 0x3FU)) << 12U)
                             | (((unsigned int)(in[4] & 0x3FU)) << 6U)
                             |  ((unsigned int)(in[5] & 0x3FU)) );
                if (c < 256)
                    *(out++) = c;
            }
            in += 6;

        } else
            in++;               /* 11111110 and 11111111 are invalid */

    /* Terminate the output string. */
    *out = '\0';
    return (size_t)(out - (unsigned char *)output);
}

void displayHexString(const char* string) {
  int len = strlen(string);
  for (int idx = 0; idx < len; ++idx) {
    Serial.print(string[idx], HEX);
    Serial.print(" ");
    Serial.print(string[idx]);
    Serial.print(" ");
  }
  Serial.println();
}

char city[64] = { 0 }; 

void getOWMInfo() {
   for (uint8_t idx = 0; idx < numForecast; ++idx) {
          OWMForecast[idx].error = true;
    }
  if (WiFi.isConnected()) {
     owm.begin(owmURL);
    if (owm.GET()) {
      String json = owm.getString();
      Serial.println("--");
      Serial.println(json.length());
      DynamicJsonBuffer jsonBuffer(16384);
//      StaticJsonBuffer<20480> jsonBuffer;
      JsonObject& root = jsonBuffer.parseObject(json);
      static char utf8buf[64];
      if (root.success()) {
        strcpy(utf8buf, root["city"]["name"]);
        utf8_to_latin9(city, utf8buf, strlen(utf8buf));
        for (uint8_t idx = 0; idx < numForecast; ++idx) {
          struct WeatherInfo& OWMInfo = OWMForecast[idx];
          JsonObject& current = root["list"][idx];
          float temp = current["main"]["temp"];
          if (fabs(temp) <= 0.5)
            temp = 0.0;
          OWMInfo.temperature = temp;
          JsonObject& weather = current["weather"][0];
          strcpy(utf8buf, weather["description"]);
           utf8_to_latin9(OWMInfo.fulldescription, utf8buf, strlen(utf8buf));
          strcpy(OWMInfo.icon, weather["icon"]);

          // format time into dt
          time_t utc = atol(current["dt"]);
          time_t local = CE.toLocal(utc, &tcr);
          sprintf(OWMInfo.dt, "%s %dh", days[weekday(local) - 1], hour(local));
   	      OWMInfo.error = false;
        }
      } else {
        Serial.println("Failed reading JSON");
      }
    }
    owm.end();
  }
}

void displayTextCenter(const char* str);

void initOTA() {
    ArduinoOTA.setHostname("WemosOWM");
    ArduinoOTA.setPassword("wemos");
    ArduinoOTA.onStart([]() {});
    ArduinoOTA.onEnd([]() {});
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
      display.setFont(&pixel_millennium_regular5pt8b);
      display.clearDisplay();
      unsigned int realprogress = (progress / (total / 100));
      char buffer[16];
      sprintf(buffer, "OTA: %.2d %%", realprogress);
      displayTextCenter(buffer);
      display.display();
    });
    ArduinoOTA.onError([](ota_error_t error) {});
    ArduinoOTA.begin();
}

unsigned long lastUpdateTime = 0;

void displaySpace(int32_t offset) {
   // only update when displaying space
   unsigned long currentTime = millis();                 
   if ((currentTime - lastUpdateTime) > 20000) { 
      getOWMInfo();
      sendNTPpacket(timeServerIP); 
     lastUpdateTime = currentTime;
   }
   checkNTP();      

}

// with one byte padding
void displayInvader(int32_t offset) {
  display.drawBitmap(centerH(invaderWidth) + offset, centerV(invaderHeight), 
                     invaderBitmap, invaderWidth, invaderHeight, WHITE);
}

const char* config = "/config.json";

int rotation = 2;

void initFromSpiffs() {
  if (SPIFFS.exists(config)) {
    File file = SPIFFS.open(config, "r");
    StaticJsonBuffer<400> jb;
    JsonObject& obj = jb.parseObject(file);
    if (obj.success()) {
      // get options 
     if (obj.containsKey("rotation"))
        rotation = obj["rotation"];
     if (obj.containsKey("owmid"))
        owmId = obj["owmid"];
     if (obj.containsKey("owmkey"))
        owmApiKey = obj["owmkey"];
     if (obj.containsKey("owmlang"))
        owmLang = obj["owmlang"];
      if (obj.containsKey("ntpserver"))
        ntpServerName = obj["ntpserver"];
    } 
    file.close();
  }
}

void setup() {
  Serial.begin(115200);

  // start file system
  SPIFFS.begin(); 

  initFromSpiffs();

  display.begin(SSD1306_SWITCHCAPVCC, 0x3C); //initialize with the I2C addr 0x3C (128x64) - 64x48 for wemos oled
  Wire.setClock(400000);
  display.setRotation(rotation);
   // show splash screen
   display.clearDisplay();
   display.drawBitmap(0, 0, wemos_logo_64x48_normal, 64, 48, WHITE);
  display.display();
  delay(1000);
  display.setTextColor(WHITE, BLACK);
  display.setTextWrap(false);

  WiFiManager wifiManager;
  // production
  wifiManager.setConfigPortalTimeout(120);
  wifiManager.setDebugOutput(false);
  wifiManager.setMinimumSignalQuality(30);
  wifiManager.setAPCallback(configModeCallback);
  wifiManager.autoConnect("wemos"); //  no password
  
  initOWM();
  initNTP();

  if (WiFi.isConnected()) {
    initOTA();    
    udp.begin(localPort);
       
    getOWMInfo();

    WiFi.hostByName(ntpServerName, timeServerIP);
    sendNTPpacket(timeServerIP); 

  }
}

uint8_t rssiToQuality(long rssi) {
     // -120 to 0 DBm
   uint8_t quality;
   if(rssi <= -100)
     quality = 0;
   else if(rssi >= -50)
     quality = 100;
   else
     quality = 2 * (rssi + 100);
  return quality;
}

void displayConnectionStatus(uint16_t startx, uint16_t starty) {
    if (WiFi.isConnected()) {
      uint8_t quality = rssiToQuality(WiFi.RSSI());
      uint16_t offset = 4;
      if (quality >= 10)
        display.fillRect(startx, starty - 3, 3, 3, WHITE);
      else
        display.drawRect(startx, starty - 3, 3, 3, WHITE);
      startx += offset;
      if (quality >= 25)
        display.fillRect(startx, starty - 5, 3, 5, WHITE);
      else
        display.drawRect(startx, starty - 5, 3, 5, WHITE);
      startx += offset;
       if (quality >= 50)
        display.fillRect(startx, starty - 7, 3, 7, WHITE);
      else
        display.drawRect(startx, starty - 7, 3, 7, WHITE);
      startx += offset;
       if (quality >= 80)
        display.fillRect(startx, starty - 9, 3, 9, WHITE);
      else
        display.drawRect(startx, starty -9, 3, 9, WHITE);
      }
 }

unsigned long alternateTime = 0;
unsigned int hheader = 0;

void displayTime(int32_t x, int32_t y) {
  if (NTPInfo.error == false) {
    time_t utc = NTPInfo.time;
   time_t local = CE.toLocal(utc, &tcr);
   static char buffer[16];
   sprintf(buffer, "%.2d:%.2d", hour(local), minute(local));
  displayText(buffer, x, y);
  }
}

void displayDay(int32_t x, int32_t y) {
  if (NTPInfo.error == false) {
     time_t utc = NTPInfo.time;
     time_t local = CE.toLocal(utc, &tcr);
     static char buffer[32];
     sprintf(buffer, "%s %d", longdays[weekday(local) - 1], day(local)); // , monthes[month(local) -1]);
    displayText(buffer, x, y);
  }
}


void displayCityAndTime() {
  display.setFont(&pixel_millennium_regular5pt8b);
  unsigned long currentTime = millis();
  if ((currentTime - alternateTime) > 5000) {
    hheader = (hheader + 1) % 3;
    alternateTime = currentTime;
  }
  switch (hheader) {
    case 0: displayText(city, 0, 9); break;
    case 1: displayTime(0, 9); break;
    case 2: displayDay(0, 9); break;
  }
}

unsigned long lastDisplayTime = 0;

void displayContents();

void loop() {
  ArduinoOTA.handle();

    unsigned long currentTime = millis();
    
    if ((currentTime - lastDisplayTime) > 10) { // 100 fps
//      unsigned long startt = millis();
      displayContents();
//     display.display();
//      Serial.println(millis() - startt);
      lastDisplayTime = currentTime;
    }
  }


void checkNTP() {
  if (WiFi.isConnected()) {
    if (udp.parsePacket() != 0) { 
      udp.read(packetBuffer, NTP_PACKET_SIZE);
      uint32_t NTPTime = (packetBuffer[40] << 24) | (packetBuffer[41] << 16) | (packetBuffer[42] << 8) | packetBuffer[43];
      const uint32_t seventyYears = 2208988800UL;
      // subtract seventy years:
      uint32_t epoch = NTPTime - seventyYears; 
     // Serial.print("Epoch "); // Serial.println(epoch);
      NTPInfo.time = epoch;
      NTPInfo.error = false;
    } else {
   //   Serial.println("No NTP");
    }
  }
}


void sendNTPpacket(IPAddress& address) {
  if (WiFi.isConnected()) {
//    Serial.println("Requesting NTP");
    memset(packetBuffer, 0, NTP_PACKET_SIZE);
    packetBuffer[0] = 0b11100011;   // LI, Version, Mode
    udp.beginPacket(address, 123); //NTP requests are to port 123
    udp.write(packetBuffer, NTP_PACKET_SIZE);
    udp.endPacket();
  }
}

// center in page area

int left = 0;
int top = 0;
int header = 10;

int centerH(int width) {
  return ((64 - width) / 2) + left;
}

// text, starts at bottom
int centerVT(int height) {
  return ((48 - height - header) / 2) + top + height + header; 
}

// image
int centerV(int height) {
  return ((48 - height - header) / 2) + top + header; 
}

void displayTextCenterH(const char* str, int ypos) {
    int16_t x1, y1;
    uint16_t w, h; 
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    displayText(str, centerH(w), ypos);
}

void displayTextCenterHOffset(const char* str, int ypos, int32_t offset) {
    int16_t x1, y1;
    uint16_t w, h; 
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    displayText(str, centerH(w) + offset, ypos);
}

void displayTextCenter(const char* str) {
    int16_t x1, y1;
    uint16_t w, h; 
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    displayText(str, centerH(w), centerVT(h));
}

void displayTextCenterOffset(const char* str, int16_t offset) {
    int16_t x1, y1;
    uint16_t w, h; 
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    displayText(str, centerH(w) + offset, centerVT(h));
}

void displayOWMTemp(int32_t offset, uint8_t index) {
    if (!WiFi.isConnected()) {
      displayInvader(offset);
      return;
    }
  if (index >= numForecast)
     return;
  display.setFont(&Lato_Medium12pt8b);
  struct WeatherInfo& OWMInfo = OWMForecast[index];
  if (OWMInfo.error == false) {
    displayTemp(OWMInfo.temperature, offset);
    display.setFont(&pixel_millennium_regular5pt8b);
//    displayTextCenterHOffset(OWMInfo.dt, 20, offset); 
  } else {
     display.setFont(&pixel_millennium_regular5pt8b);
    displayTextCenterOffset("no OWM temp", offset);
  }
}

void displayTemp(float temp, int32_t offset) {
    static char buffer[10];
    char* buf = buffer;
    if (temp < 0) {
      buffer[0] = '-';
      buf++;
    }
    dtostrf(fabs(round(temp)), 2, 0, buf);
//    buf[2] = ' ';
    buf[2] = 176;
    buf[3] = 0;
    displayTextCenterOffset(buffer, offset);
}

struct OWMIcon {
  const char* names; // comma separated
  uint8_t width;
  uint8_t height;
  const uint8_t* data;
};

#include "weathericonic.h"

struct OWMIcon* findIcon(const char* name) {
  int numIcons = sizeof(icons_weather_iconic) / sizeof(struct OWMIcon);
  for (int idx = 0; idx < numIcons; ++idx) {
    struct OWMIcon* current = &icons_weather_iconic[idx];
    char* names = strdup(current->names);
    char* tofree = names; // names will be modified
    char* found;
     while((found = strsep(&names, ",")) != NULL) {
      if (!strcmp(name, found)) {
        free(tofree);
         return current;
      }
    }
    free(tofree);
  }
  return NULL;
}

void displayOWMIcon(int32_t offset, uint8_t index) {
    if (!WiFi.isConnected()) {
      displayInvader(offset);
      return;
    }
  if (index >= numForecast)
     return;
   display.setFont(&pixel_millennium_regular5pt8b);
  struct WeatherInfo& OWMInfo = OWMForecast[index];
  if (OWMInfo.error == false) {
     struct OWMIcon* icon = findIcon(OWMInfo.icon);
    if (icon != NULL) {
      display.drawBitmap(centerH(icon->width) + offset, centerV(icon->height) - 2, 
                          icon->data, icon->width, icon->height, WHITE);
      displayTextCenterHOffset(OWMInfo.fulldescription, 44, offset);
     } else { 
      displayTextCenterOffset(OWMInfo.icon, offset);
    }
  } else {
    displayTextCenterOffset("no icon", offset);
  }
}

void displayNTPTime(int32_t offset) {
    if (!WiFi.isConnected()) {
      displayInvader(offset);
      return;
    }
   display.setFont(&DS_DIGIB12pt7b);
  if (NTPInfo.error == false) {
   time_t utc = NTPInfo.time;
   time_t local = CE.toLocal(utc, &tcr);
   static char buffer[16];
   sprintf(buffer, "%.2d:%.2d", hour(local), minute(local));
   displayTextCenterOffset(buffer, offset);
  } else {
    display.setFont(&pixel_millennium_regular5pt8b);
    displayTextCenterOffset("no NTP time", offset);
  }
}

void displayNTPDate(int32_t offset) {
    if (!WiFi.isConnected()) {
      displayInvader(offset);
      return;
    }
   display.setFont(&Lato_Medium8pt8b);
   if (NTPInfo.error == false) {
     time_t utc = NTPInfo.time;
     time_t local = CE.toLocal(utc, &tcr);
     static char buffer[16];
     sprintf(buffer, "%s %d", days[weekday(local) - 1], day(local)); // , monthes[month(local) -1]);
     displayTextCenterOffset(buffer, offset);
  } else {
    display.setFont(&pixel_millennium_regular5pt8b);
    displayTextCenterOffset("no NTP date", offset);
  }
}

void displayOWMDate(int32_t offset, uint8_t index) {
  if (index >= numForecast)
    return;
    if (!WiFi.isConnected()) {
      displayInvader(offset);
      return;
    }
    // another font ?
   display.setFont(&Lato_Medium8pt8b);
   struct WeatherInfo& OWMInfo = OWMForecast[index];
   if (OWMInfo.error == false) {
     displayTextCenterOffset(OWMInfo.dt, offset);
  } else {
    display.setFont(&pixel_millennium_regular5pt8b);
    displayTextCenterOffset("no OWM date", offset);
  }
}

typedef void (*DISPLAYFUN)(int32_t offset);

DISPLAYFUN displayFuns[] = {
  displaySpace,
  displayInvader
};

uint32_t curpos = 0;

#define CHECKVISIBLE if (abs(offset) <= display.width()) { 
#define CHECKVISIBLEEND }

void displayContents() {
  display.clearDisplay();
  // englobing rectangle
  //display.drawRect(0, 0, 64, 48, WHITE);
  //display.drawFastHLine(0, 10, 64, WHITE);
  uint16_t numFixedPages = sizeof(displayFuns) / sizeof(DISPLAYFUN);
  uint16_t numVarPages = 3 * numForecast;
  uint16_t idx;
  int32_t offset;
  for (idx = 0; idx < numFixedPages; ++idx) {
    uint32_t pagepos = idx * display.width();
    // test if visible to speed-up things
    // TODO: check if this works
    offset = pagepos - curpos;
    CHECKVISIBLE
      displayFuns[idx](offset);
    CHECKVISIBLEEND
  }
  for (uint16_t idx2 = 0; idx2 < numForecast; ++idx2) {
     uint32_t pagepos = idx * display.width();
     offset = pagepos - curpos;
     CHECKVISIBLE
     displayOWMDate(offset, idx2);
     CHECKVISIBLEEND
     ++idx;
     pagepos = idx * display.width();
     offset = pagepos - curpos;
     CHECKVISIBLE
     displayOWMTemp(offset, idx2);
     CHECKVISIBLEEND
     ++idx;
     pagepos = idx * display.width();
     offset = pagepos - curpos;
     CHECKVISIBLE
     displayOWMIcon(offset, idx2);
     CHECKVISIBLEEND
     ++idx;
  }
  curpos += 1;
  curpos = curpos % ((numFixedPages + numVarPages) * display.width());
  displayConnectionStatus(48, 10);
  displayCityAndTime();
  display.display();
}
