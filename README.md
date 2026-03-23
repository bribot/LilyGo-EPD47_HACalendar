# LilyGo EPD47 Home Assistant Calendar

A Pokemon-themed e-paper dashboard for the **LilyGo T5 4.7" e-paper display** (ESP32-S3). Displays today's date, current weather, and calendar events pulled from Home Assistant — rendered in a Pokemon battle scene style.

![Display Layout](https://github.com/bribot/LilyGo-EPD47_HACalendar/blob/master/img/wednesday_night.jpg)

## Features

- Date display (day / month / year) with a darkened background panel
- Weather condition, temperature, and humidity from Home Assistant
- Today's calendar events from a HA calendar entity
- Pokemon battle scene background that changes by weekday
- Pokemon sprite that changes with weather condition
- Wakes every hour via deep sleep, then refreshes the display

---

## Hardware Requirements

- [LilyGo T5 4.7" e-paper display (ESP32-S3 variant)](https://www.lilygo.cc/products/t5-4-7-inch-e-paper-v2-3)
- MicroSD card (optional — see Asset Hosting below)
- USB-C cable for flashing

---

## Software Requirements

- [PlatformIO](https://platformio.org/) (CLI or VS Code extension)
- Home Assistant instance accessible on the local network

---

## Installation

### 1. Clone the repository

```bash
git clone https://github.com/yourname/LilyGo-EPD47_HACalendar.git
cd LilyGo-EPD47_HACalendar
```

### 2. Create `src/secrets.h`

Create it with your credentials:

```cpp
#define WIFI_SSID     "your-wifi-ssid"
#define WIFI_PASSWORD "your-wifi-password"
#define HA_HOST_ADDR  "homeassistant.local"
#define HA_PORT_NUM   8123
#define HA_TOKEN_VALUE "your-long-lived-access-token"
```

To generate a long-lived access token in Home Assistant: go to your profile → **Long-Lived Access Tokens** → **Create Token**.

### 3. Configure Home Assistant entities

The code reads from these entities (configurable at the top of `src/main.cpp`):

| Constant | Entity ID | Notes |
|---|---|---|
| `ENTITY_DATE` | `sensor.date` | Built-in HA sensor — enable it in Settings → System → Date/time |
| `ENTITY_WEEKDAY` | `sensor.weekday` | Custom sensor — state must match your asset filenames (e.g. `Monday`) |
| `ENTITY_WEATHER` | `weather.forecast_home` | Built-in weather integration |
| `ENTITY_CALENDAR` | `calendar.personal` | Any HA calendar entity |

> To change any entity ID, edit the `const char* ENTITY_*` values near the top of `src/main.cpp`.

#### sensor.weekday

The easiest way I found was to make a Template sensor. Go to **Devices & services > Helpers > Create Helper > Template > Sensor** name it `weekday` and put this on the State:

```yaml
{{ now().strftime('%A') }}
```

### 4. Host the assets

The device loads images either from an **SD card** or over **HTTP from Home Assistant**. It automatically uses the SD card if one is detected; otherwise it falls back to HTTP.

#### Asset list

You need the following PNG files:

| File | Description |
|---|---|
| `scene.png` | Full background scene (960×540px recommended) |
| `base.png` | Battle base platform drawn under the weather Pokemon |
| `error.png` | Shown when an image file is missing |
| `<condition>.png` | One PNG per HA weather condition (e.g. `sunny.png`, `rainy.png`, `cloudy.png`, `snowy.png`, etc.) |
| `<weekday>.png` | One PNG per weekday matching your `sensor.weekday` state (e.g. `Monday.png` … `Sunday.png`) |

Weather condition names come directly from the HA `weather.forecast_home` state value — I'm using the mapping mentioned on the [weather integration](https://www.home-assistant.io/integrations/weather/).

#### Option A — SD card (recommended)

1. Format the SD card as FAT32.
2. Create a folder `/HACalendar/` in the root.
3. Copy all asset files into `/HACalendar/`.

```
SD:/
└── HACalendar/
    ├── scene.png
    ├── base.png
    ├── error.png
    ├── sunny.png
    ├── rainy.png
    ├── cloudy.png
    ├── ...
    ├── Monday.png
    ├── Tuesday.png
    └── ...
```

#### Option B — Home Assistant www folder

1. In your HA config directory, create `www/pokemonCalendar/`.
2. Copy all asset files into that folder.
3. Files will be served at `http://homeassistant.local:8123/local/pokemonCalendar/<filename>`.

The base URL is defined in `src/main.cpp`:
```cpp
char* imgUrl = "http://homeassistant.local:8123/local/pokemonCalendar/";
```
Change this if your HA host/port differs.

### 5. Build and flash

```bash
# Build for T5-ePaper-S3 (default)
pio run

# Flash to device
pio run --target upload

# Monitor serial output (115200 baud)
pio device monitor
```

---

## Deep Sleep

After rendering, the device sleeps for **60 minutes** then reboots and refreshes. The sleep interval is set in `loop()`:

```cpp
esp_sleep_enable_timer_wakeup(60ULL * 60 * 1000000); // 60 minutes
```

---
