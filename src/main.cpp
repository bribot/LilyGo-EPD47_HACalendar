#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <epd_driver.h>
#include <esp_heap_caps.h>
#include <JPEGDEC.h>
#include <PNGdec.h>
#include <ArduinoJson.h>
#include <vector>
#include <SD.h>
#include <SPI.h>

#include "../fonts/pokemongb40.h"
#include "../fonts/pokemongb30.h"
#include "../fonts/pokemongb16.h"
#include "../fonts/pokemongb24.h"
#include "../fonts/pokemongb12.h"
#include "../fonts/pokemongb8.h"

#include "secrets.h"

#define EPD_WIDTH  960
#define EPD_HEIGHT 540
#define FILESYSTEM SD

// ------------Config ------------
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

const char* HA_HOST = HA_HOST_ADDR;
const uint16_t   HA_PORT = HA_PORT_NUM;

const char* HA_TOKEN = HA_TOKEN_VALUE;

const uint16_t pokemonSize = 200;

const char* ENTITY_DATE = "sensor.date";
const char* ENTITY_WEEKDAY = "sensor.weekday";
const char* ENTITY_WEATHER = "weather.forecast_home"; 
const char* ENTITY_CALENDAR = "calendar.personal"; 
const char* ENTITY_TIME = "sensor.time";

const GFXfont DEFAULT_FONT = PokemonGB_16;
// ------------Layout ------------
int8_t yoffset_default = -15;
int8_t xoffset_default = 5;
const Rect_t DateRect = {  
    .x = 60, 
    .y = 380, 
    .width = 360, 
    .height = 120 
};

const Rect_t DateRect_day   = {  
    .x = 60, 
    .y = 350,
    .width = 250, 
    .height = 150 
};
const Rect_t DateRect_month = {  
    .x = 250, 
    .y = 350, 
    .width = 250, 
    .height = 150
};
const Rect_t DateRect_year = {  
    .x = 60, 
    .y = 410, 
    .width = 250, 
    .height = 150
};
// const Rect_t weatherRect = {  
//     .x = EPD_WIDTH/2, 
//     .y = 0, 
//     .width = EPD_WIDTH/2, 
//     .height = EPD_HEIGHT/2 
// };
// Weather sub-rects
const Rect_t weatherConditionRect = {
    .x = 45,// x and widht must be multiple of 16 to avoid artifactsbecause epd_push_pixels is dumb and rewrite it would be too much of a hassle
    .y = 20,
    .width = 302,
    .height = 35
};
const Rect_t weatherTempRect = {
    .x = 568,
    .y = 220,
    .width = 200,
    .height = 35
};
const Rect_t weatherHumidRect = {
    .x = 780,
    .y = 220,
    .width = 100,
    .height = 35
};
const Rect_t calendarRect = {  
    .x = 510, 
    .y = 380, 
    .width = 430, 
    .height = 130 
};
const Rect_t pkmnRect = {  
    .x = 616, 
    .y = 0, 
    .width = pokemonSize, 
    .height = pokemonSize 
};
const Rect_t pkmnRect_player = {  
    .x = 150, 
    .y = 150, 
    .width = pokemonSize, 
    .height = pokemonSize 
};
const Rect_t pkmnRect_playerbase = {  
    .x = 0, 
    .y = 150, 
    .width = EPD_WIDTH/2, 
    .height = pokemonSize 
};
const Rect_t pkmnRect_base = {
    .x = 450,
    .y = 130,
    .width = 510,
    .height = 130
};
const Rect_t player_hp = {
    .x = 696,
    .y = 267,
    .width = 190,
    .height = 13
};
const Rect_t pkmn_hp = {
    .x = 174,
    .y = 74,
    .width = 190,
    .height = 13
};

// ------------Globals ------------
uint8_t *framebuffer = nullptr;
JPEGDEC jpeg;
PNG png;
bool sdCard = false;

char* imgUrl = "http://homeassistant.local:8123/local/pokemonCalendar/";
char* scenePath = "scene.png"; // "http://homeassistant.local:8123/local/pokemonCalendar/scene.png";
char* basePath = "base.png"; // "http://homeassistant.local:8123/local/pokemonCalendar/base.png";
char* sdRootDir = "/HACalendar" ;
char* baseImgFile = "base.png";
char* errorPath = "error.png";


struct ImageDrawContext {
    Rect_t rect;
    int    imgWidth;
    int    imgHeight;
};
static ImageDrawContext imgDrawCtx;

RTC_DATA_ATTR char lastDateStr[32]    = "";
RTC_DATA_ATTR char lastWeatherStr[64] = "";
RTC_DATA_ATTR bool bgDrawn = false;

const char* months[] = {"", 
    "Jan", 
    "Feb", 
    "Mar", 
    "Apr", 
    "May", 
    "Jun", 
    "Jul", 
    "Aug", 
    "Sep", 
    "Oct", 
    "Nov", 
    "Dec"
};

// -------------------------------------

uint8_t* downloadImage(const char* url, int& bytesRead) {
    HTTPClient http;
    http.begin(url);
    http.setTimeout(15000);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("ERROR: HTTP GET failed with code %d\n", httpCode);
        http.end();
        return nullptr;
    }
    int imageSize = http.getSize();
    int bufSize = (imageSize > 0) ? imageSize : 512000;
    uint8_t* buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    if (!buf) buf = (uint8_t*)heap_caps_malloc(bufSize, MALLOC_CAP_SPIRAM);
    if (!buf) {
        Serial.println("ERROR: No RAM for image buffer!");
        http.end();
        return nullptr;
    }
    WiFiClient* stream = http.getStreamPtr();
    bytesRead = 0;
    uint32_t timeout = millis();
    while ((http.connected() || stream->available()) && bytesRead < bufSize) {
        int avail = stream->available();
        if (avail) {
            bytesRead += stream->readBytes(buf + bytesRead, avail);
            timeout = millis();
        }
        if (millis() - timeout > 5000) { Serial.println("Stream timeout!"); break; }
        delay(1);
    }
    http.end();
    Serial.printf("Downloaded %d bytes\n", bytesRead);
    return buf;
}

// ----------- Image rendering -----------

// overloading the clear area function because the original one sucks
void epd_clear_area2(const Rect_t& rect) {
    int32_t cycle_time = 50;
    uint8_t cycle_count = 20;
    epd_fill_rect(rect.x, rect.y, rect.width, rect.height, 255, framebuffer);
    for (int i = 0; i < cycle_count; i++) {
        epd_push_pixels(rect, cycle_time, 1);
    }
    
}

inline void writePixel(int imgX, int imgY, uint8_t gray) {
    int rx = (imgX * imgDrawCtx.rect.width)  / imgDrawCtx.imgWidth;
    int ry = (imgY * imgDrawCtx.rect.height) / imgDrawCtx.imgHeight;
    if (rx < 0 || rx >= imgDrawCtx.rect.width || ry < 0 || ry >= imgDrawCtx.rect.height) return;
    int epx = imgDrawCtx.rect.x + rx;
    int epy = imgDrawCtx.rect.y + ry;
    if (epx >= EPD_WIDTH || epy >= EPD_HEIGHT) return;
    if (gray == 0xff) return;
    epd_draw_pixel(epx,epy,gray,framebuffer);
}

int jpegDrawCallback(JPEGDRAW *pDraw) {
    for (int y = 0; y < pDraw->iHeight; y++) {
        for (int x = 0; x < pDraw->iWidth; x++) {
            uint16_t pixel = pDraw->pPixels[y * pDraw->iWidth + x];
            uint8_t r = ((pixel >> 11) & 0x1F) << 3;
            uint8_t g = ((pixel >> 5)  & 0x3F) << 2;
            uint8_t b = ( pixel        & 0x1F) << 3;
            //uint8_t gray = (r * 299 + g * 587 + b * 114) / 1000;
            uint8_t gray = (r+g+b)/3;
            writePixel(pDraw->x + x, pDraw->y + y, gray);
        }
    }
    return 1;
}

int pngDrawCallback(PNGDRAW *pDraw) {
    uint16_t lineBuffer[EPD_WIDTH];
    uint8_t  maskBuffer[(EPD_WIDTH + 7) / 8];

    png.getLineAsRGB565(pDraw, lineBuffer, PNG_RGB565_BIG_ENDIAN, 0xFFFFFFFF);

    bool hasAlpha = png.getAlphaMask(pDraw, maskBuffer, 128);

    for (int x = 0; x < pDraw->iWidth; x++) {
        uint8_t r, g, b;
        bool opaque = hasAlpha ? (maskBuffer[x >> 3] & (0x80 >> (x & 7))) : true;
        if (!opaque) {
            r = g = b = 0xFF;
        } else {
            uint16_t pixel = lineBuffer[x];
            r = ((pixel >> 11) & 0x1F) << 3;
            g = ((pixel >> 5)  & 0x3F) << 2;
            b = pixel;//( pixel        & 0x1F) << 3;
        }
        //uint8_t gray = (r * 299 + g * 587 + b * 114) / 1000;
        uint8_t gray = b;//9(r+g+b)/3;
        writePixel(x, pDraw->y, gray);
    }
    
    return 1;
}

enum ImageType { IMG_JPEG, IMG_PNG, IMG_BMP, IMG_UNKNOWN };

ImageType detectImageType(uint8_t *buf, int len) {
    if (len < 4) return IMG_UNKNOWN;
    if (buf[0] == 0xFF && buf[1] == 0xD8 && buf[2] == 0xFF) return IMG_JPEG;
    if (buf[0] == 0x89 && buf[1] == 0x50 && buf[2] == 0x4E && buf[3] == 0x47) return IMG_PNG;
    return IMG_UNKNOWN;
}


void drawImage(const char* url, const Rect_t& rect) {
    imgDrawCtx.rect = rect;

    int bytesRead = 0;
    uint8_t *imgBuf;
    if (!sdCard){
        String imgPath = String(imgUrl) + url;
        imgBuf = downloadImage(imgPath.c_str(), bytesRead);
    } else {
        String imgPath = String(sdRootDir) + "/" + url;
        File imgFile = FILESYSTEM.open(imgPath.c_str(), "r");
        if (!imgFile) {
            Serial.println("File not found!");
            imgPath = String(sdRootDir) + "/" + errorPath;
            imgFile = FILESYSTEM.open(imgPath.c_str(), "r");
        }
        bytesRead = imgFile.size();
        imgBuf = (uint8_t*)heap_caps_malloc(bytesRead, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (!imgBuf) imgBuf = (uint8_t*)heap_caps_malloc(bytesRead, MALLOC_CAP_SPIRAM);
        if (imgBuf) imgFile.read(imgBuf, bytesRead);
        imgFile.close();
    }
    if (!imgBuf) return;

    switch (detectImageType(imgBuf, bytesRead)) {
        case IMG_JPEG:
            Serial.println("Detected: JPEG");
            Serial.printf("First bytes: %02X %02X %02X %02X %02X\n",
                imgBuf[0], imgBuf[1], imgBuf[2], imgBuf[3], imgBuf[4]);
            if (jpeg.openRAM(imgBuf, bytesRead, jpegDrawCallback)) {
                jpeg.setPixelType(RGB565_BIG_ENDIAN);
                imgDrawCtx.imgWidth  = jpeg.getWidth();
                imgDrawCtx.imgHeight = jpeg.getHeight();
                Serial.printf("JPEG: %dx%d\n", imgDrawCtx.imgWidth, imgDrawCtx.imgHeight);
                jpeg.decode(0, 0, 0);
                jpeg.close();
            } else { Serial.printf("ERROR: Failed to open JPEG (err=%d)\n", jpeg.getLastError()); }
            break;

        case IMG_PNG:
            Serial.println("Detected: PNG");
            if (png.openRAM(imgBuf, bytesRead, pngDrawCallback) == PNG_SUCCESS) {
                imgDrawCtx.imgWidth  = png.getWidth();
                imgDrawCtx.imgHeight = png.getHeight();
                Serial.printf("PNG: %dx%d, %d bpp\n", imgDrawCtx.imgWidth, imgDrawCtx.imgHeight, png.getBpp());
                png.decode(nullptr, 0);
                png.close();
            } else { Serial.println("ERROR: Failed to open PNG"); }
            break;

        default:
            Serial.println("ERROR: Unknown image format!");
            break;
    }

    heap_caps_free(imgBuf);
}

 
// ----------- Drawing helpers -----------

void drawRect(const Rect_t& rect, uint8_t thickness = 1, uint8_t color = 0) {
    for (uint8_t t = 0; t < thickness; t++) {
        // top
        epd_draw_hline(rect.x + t, rect.y + t, rect.width - 2*t, color, framebuffer);
        // bottom
        epd_draw_hline(rect.x + t, rect.y + rect.height - 1 - t, rect.width - 2*t, color, framebuffer);
        // left
        epd_draw_vline(rect.x + t, rect.y + t, rect.height - 2*t, color, framebuffer);
        // right
        epd_draw_vline(rect.x + rect.width - 1 - t, rect.y + t, rect.height - 2*t, color, framebuffer);
    }
}


// ----------- HA API helpers -----------

struct HAEntity {
    String state;
    // weather attributes
    float  temperature        = 0;
    float  humidity           = 0;
    float  wind_speed         = 0;
    String wind_bearing;
    float  pressure           = 0;
    String condition;           // same as state for weather entities
    String temperature_unit;
    // generic sensor
    String unit_of_measurement;
    bool   ok = false;
};

HAEntity fetchHAEntity(const char* entity_id) {
    HAEntity result;
    HTTPClient http;
    String url = String("http://") + HA_HOST + ":" + HA_PORT + "/api/states/" + entity_id;

    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("ERROR: HA API GET failed with code %d\n", httpCode);
        http.end();
        return result;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("ERROR: JSON parse failed: %s\n", err.c_str());
        return result;
    }

    result.state = doc["state"] | "";
    JsonObject attr = doc["attributes"];

    result.temperature         = attr["temperature"]         | 0.0f;
    result.humidity            = attr["humidity"]            | 0.0f;
    result.wind_speed          = attr["wind_speed"]          | 0.0f;
    result.wind_bearing        = attr["wind_bearing"]        | "";
    result.pressure            = attr["pressure"]            | 0.0f;
    result.condition           = result.state;
    result.temperature_unit    = attr["temperature_unit"]    | "°C";
    result.unit_of_measurement = attr["unit_of_measurement"] | "";

    result.ok = true;
    Serial.printf("[HA] %s → state='%s' temp=%.1f%s hum=%.0f%% wind=%.1f\n",
        entity_id, result.state.c_str(), result.temperature,
        result.temperature_unit.c_str(), result.humidity, result.wind_speed);

    return result;
}

// simple state-only sensors
String fetchHASensorState(const char* entity_id) {
    return fetchHAEntity(entity_id).state;
}

// ----------- Calendar -----------

struct HACalendarEvent {
    String summary;    // event title
    String start;      // ISO datetime or date
    String end;
    String location;
    String description;
};

// Fetches today's events from a HA calendar entity
// Uses /api/calendars/<entity_id>?start=<date>T00:00:00&end=<date>T23:59:59
std::vector<HACalendarEvent> fetchCalendarEvents(const char* entity_id, const String& date) {
    std::vector<HACalendarEvent> events;
    HTTPClient http;

    String url = String("http://") + HA_HOST + ":" + HA_PORT
               + "/api/calendars/" + entity_id
               + "?start=" + date + "T00:00:00Z"
               + "&end="   + date + "T23:59:59Z";

    Serial.println("Calendar URL: " + url);
    http.begin(url);
    http.addHeader("Authorization", String("Bearer ") + HA_TOKEN);
    http.addHeader("Content-Type", "application/json");

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("ERROR: Calendar GET failed with code %d\n", httpCode);
        http.end();
        return events;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("ERROR: Calendar JSON parse failed: %s\n", err.c_str());
        return events;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject ev : arr) {
        HACalendarEvent event;
        event.summary     = ev["summary"]     | "";
        event.description = ev["description"] | "";
        event.location    = ev["location"]    | "";
        // start/end can be {"dateTime": ...} or {"date": ...}
        if (ev["start"]["dateTime"]) {
            event.start = ev["start"]["dateTime"] | "";
        } else {
            event.start = ev["start"]["date"] | "";
        }
        if (ev["end"]["dateTime"]) {
            event.end = ev["end"]["dateTime"] | "";
        } else {
            event.end = ev["end"]["date"] | "";
        }
        Serial.printf("[Calendar] Event: '%s' start=%s\n", event.summary.c_str(), event.start.c_str());
        events.push_back(event);
    }

    Serial.printf("[Calendar] Fetched %d events for %s\n", events.size(), date.c_str());
    return events;
}


void drawInitScreen() {
    epd_poweron();
    if (!bgDrawn){
        epd_clear();
        bgDrawn = true;
    }
    epd_poweroff();
}

void drawTextArea(const Rect_t& rect, const char* text, const GFXfont* font = &DEFAULT_FONT, int8_t xoffset = xoffset_default, int8_t yoffset = yoffset_default, bool white = false) {
    int32_t cursor_x = rect.x + xoffset;
    int32_t cursor_y = rect.y + yoffset + font->advance_y + font->descender;

    if (white) {
        // Write text into a temp framebuffer, then stamp only the text pixels
        // as white onto the real framebuffer, leaving the background untouched.
        uint8_t *tmp = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
        if (!tmp) { Serial.println("ERROR: No RAM for white text buffer"); return; }
        memset(tmp, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);
        writeln(font, text, &cursor_x, &cursor_y, tmp);
        // Where tmp has a black pixel (nibble == 0), write white (0xF) to real framebuffer
        for (int y = rect.y; y < rect.y + rect.height && y < EPD_HEIGHT; y++) {
            for (int x = rect.x; x < rect.x + rect.width && x < EPD_WIDTH; x++) {
                int idx = (y * EPD_WIDTH + x) / 2;
                if (x % 2 == 0) {
                    if ((tmp[idx] & 0xF0) == 0x00)
                        framebuffer[idx] |= 0xF0;
                } else {
                    if ((tmp[idx] & 0x0F) == 0x00)
                        framebuffer[idx] |= 0x0F;
                }
            }
        }
        heap_caps_free(tmp);
    } else {
        epd_clear_area2(rect);
        //epd_fill_rect(rect.x, rect.y, rect.width, rect.height, 255, framebuffer);
        write_string(font, text, &cursor_x, &cursor_y, framebuffer);
        //writeln(font, text, &cursor_x, &cursor_y, framebuffer);
    }
}


void drawCalendar(){
    String dateStr = fetchHASensorState(ENTITY_DATE);
    if (dateStr.isEmpty()) {
        Serial.println("ERROR: No date for calendar fetch");
        return;
    }

    auto events = fetchCalendarEvents(ENTITY_CALENDAR, dateStr);
    String display = "";
    if (events.empty()) {
        display = "No events today";
    } else {
        for (int i = 0; i < (int)events.size(); i++) {
            if (i > 0) display += "\n";
            // [Calendar] Event: 'Meeting with the team' start=2026-03-18T14:00:00-06:00
            String timeStr = "";
            if (events[i].start.length() > 10 && events[i].start.indexOf('T') >= 0) {
                timeStr = events[i].start.substring(11, 16) + " ";
            } else if (events[i].start.length() > 0 && events[i].start.length() <= 10){
                timeStr = "[allday] ";
            }
            if (events[i].summary.length() > 16){
                events[i].summary = events[i].summary.substring(0, 16) + "\n" + events[i].summary.substring(16, -1);
            }
            display += timeStr + events[i].summary;
        }
    }

    Serial.println("Calendar display: " + display);
    drawTextArea(calendarRect, display.c_str(), &PokemonGB_12, 0, 0);
}

void drawPkmn(const char* imgPath){
    epd_clear_area2(pkmnRect);
    drawImage(basePath, pkmnRect_base);
    drawImage(imgPath, pkmnRect);
}

void drawScene(){
        String weekdayStr = fetchHASensorState(ENTITY_WEEKDAY);
        static char weekdayUrl[64];
        snprintf(weekdayUrl, sizeof(weekdayUrl),
        "%s.png",
        weekdayStr.c_str());
        drawImage(scenePath, epd_full_screen());
        epd_clear_area2(pkmnRect_playerbase);
        drawImage(weekdayUrl, pkmnRect_player);
        
}

void drawWeather() {
    HAEntity weather = fetchHAEntity(ENTITY_WEATHER);
    if (!weather.ok) {
        Serial.println("ERROR: Failed to fetch weather from HA API");
        return;
    }

    // Single string for change detection
    String weatherStr = weather.condition
        + "|" + String(weather.temperature, 1)
        + "|" + String((int)weather.humidity);

    if (strcmp(weatherStr.c_str(), lastWeatherStr) == 0) {
        Serial.println("Weather unchanged, skipping redraw");
        return;
    }
    static char weatherPath[64];
    snprintf(weatherPath, sizeof(weatherPath), "%s.png", weather.condition.c_str());

    drawPkmn(weatherPath);
    drawTextArea(weatherConditionRect, weather.condition.c_str());

    // Temperature
    String tempStr = String(weather.temperature, 1) + " º"+weather.temperature_unit;
    drawTextArea(weatherTempRect, tempStr.c_str());

    // // Humidity at bottom
    String humStr = String((int)weather.humidity) + "$";
    drawTextArea(weatherHumidRect, humStr.c_str());

    strcpy(lastWeatherStr, weatherStr.c_str());
}

void drawTime(){
    String timeNow = fetchHASensorState(ENTITY_TIME);
    String hour = timeNow.substring(0,2);
    // Serial.println(hour);
    uint8_t hpLenght = player_hp.width * (24-hour.toInt())/24;
    epd_fill_rect(player_hp.x,player_hp.y,player_hp.width,player_hp.height,255,framebuffer);
    epd_fill_rect(player_hp.x,player_hp.y,hpLenght,player_hp.height,50,framebuffer);

}

void drawDate() {
    String dateStr = fetchHASensorState(ENTITY_DATE);
    
    Serial.println("Fetched date: " + dateStr);
    if (dateStr.isEmpty()) {
        Serial.println("ERROR: Failed to fetch date from HA API");
        return;
    } else if (strcmp(dateStr.c_str(), lastDateStr) == 0) {
        Serial.println("Date unchanged, skipping redraw");
        return;
    }
    drawScene();

    Serial.println("Updating DATE!");
    // Date fromat 2020-12-31
    String day = dateStr.substring(8,10);
    String month = dateStr.substring(5,7);
    String year = dateStr.substring(0,4);

    // Foe's hp is how much of the month is left
    uint8_t hpLenght = pkmn_hp.width * (31-day.toInt())/31;
    epd_fill_rect(pkmn_hp.x,pkmn_hp.y,pkmn_hp.width,pkmn_hp.height,255,framebuffer);
    epd_fill_rect(pkmn_hp.x,pkmn_hp.y,hpLenght,pkmn_hp.height,50,framebuffer);

    epd_clear_area2(DateRect);
    epd_fill_rect(DateRect.x-10, DateRect.y, DateRect.width+20, DateRect.height, 70, framebuffer);
    drawTextArea(DateRect_day, day.c_str(), &PokemonGB_30, 0, 0, true);
    drawTextArea(DateRect_month, months[month.toInt()], &PokemonGB_30, 0, 0, true);
    drawTextArea(DateRect_year, year.c_str(), &PokemonGB_30, 0, 0, true);
    strcpy(lastDateStr, dateStr.c_str());
    return ;
}

void setup() {
    Serial.begin(115200);
    delay(2000);

    epd_init();
    framebuffer = (uint8_t *)heap_caps_malloc(EPD_WIDTH * EPD_HEIGHT / 2, MALLOC_CAP_SPIRAM);
    if (!framebuffer) { Serial.println("ERROR: No framebuffer!"); return; }
    memset(framebuffer, 0xFF, EPD_WIDTH * EPD_HEIGHT / 2);

    Serial.printf("Connecting to %s", ssid);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED) { delay(300); Serial.print("."); }
    Serial.printf("\nConnected to %s! \nIP: %s\n", ssid, WiFi.localIP().toString().c_str());

    SPI.begin(SD_SCLK, SD_MISO, SD_MOSI);
    bool SDready = FILESYSTEM.begin(SD_CS, SPI);
    if (!SDready){
        Serial.println("SD Not detected!");
        sdCard = false;
    } else {
        Serial.println("SD Card Initialized!");
        sdCard = true;
    }

    drawInitScreen();
    //drawScene();
    drawDate();
    drawWeather();
    drawCalendar();
    drawTime();
    epd_draw_image(epd_full_screen(), framebuffer, BLACK_ON_WHITE);
}
    
void loop() {
    esp_sleep_enable_timer_wakeup(60ULL * 30 * 1000000);
    esp_deep_sleep_start();
}