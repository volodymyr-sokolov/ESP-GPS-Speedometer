/*
 * GPS Speedometer  —  LilyGO T-Display-S3 (ESP32-S3)
 * GPS: u-blox NEO-M10 (NEO-M10-1-10)
 *
 * Connections:
 *   GPS TX  -> GPIO 18  (ESP32-S3 UART1 RX)
 *   GPS RX  -> GPIO 17  (ESP32-S3 UART1 TX)
 *   GPS VCC -> 3.3V
 *   GPS GND -> GND
 *
 * Hardware:
 *   - ESP32-S3, HardwareSerial(1) for GPS
 *   - Display ST7789V: 170×320px (landscape)
 *     Landscape (rotation=1): W=320, H=170
 *   - Backlight: GPIO 38 (LEDC), + GPIO 15 (PWR_EN) HIGH on battery power
 *   - No IP5306: built-in charger without I2C, Wire not used
 *   - Battery ADC: GPIO 4 (×2 divider)
 *   - Buttons: GPIO 14 (KEY) — sleep/wake; GPIO 0 (BOOT) — unused
 *
 * GPS (NEO-M10):
 *   - Default baud: 38400
 *   - Config: UBX-CFG-VALSET (Gen10 API)
 *   - Warm start: UBX-MGA-INI-POS_LLH + UBX-MGA-INI-TIME_UTC
 *
 * Screen layout (320×170, landscape):
 *   - Speed zone: y=0..109 (110px)
 *   - Divider:    y=110..112
 *   - Info panel: y=113..169 (57px)
 *     Left part:  x=0..124   — time + TZ + date
 *     Right part: x=119..319 — coordinates + direction (expanded 6px left)
 *       Lat/Lng decimal point at x=161 (COORD_LAT_DOT_X, 2 chars right of block)
 *       Alt/HDOP/VDOP aligned at x=137 (COORD_DOT_X-2*6)
 *       Direction right-aligned to x=319
 *
 * Libraries: TFT_eSPI (Setup206_LilyGo_T_Display_S3.h), TinyGPS++, Preferences
 */

#include <TFT_eSPI.h>
#include <TinyGPS++.h>
#include <HardwareSerial.h>
#include <Preferences.h>

// ── GPS cache structure ─────────────────────────────────────────
typedef struct {
  float    lat, lng, alt;
  uint32_t unixTs;
  bool     valid;
} GpsCache;

// ════════════════════════════════════════════════════════════════
//  LOCALE SETTINGS
//  useMetric: true  = km/h + m
//             false = mph + ft
//  use24h:    true  = 24-hour clock
//             false = AM/PM
//  rotation:  0 = normal, 2 = 180°
// ════════════════════════════════════════════════════════════════
#define LOCALE_METRIC   true
#define LOCALE_USE_24H  true
#define DISPLAY_ROTATION 180   // 0 = normal, 2 = 180°

// ════════════════════════════════════════════════════════════════
//  COUNTRY SELECTION — for warm start hint
// ════════════════════════════════════════════════════════════════
#define COUNTRY_CODE      CUSTOM

#include "country_profiles.h"

#define _COUNTRY_STR(x)  #x
#define COUNTRY_STR      _COUNTRY_STR(COUNTRY_CODE)

const CountryProfile* country = nullptr;

// ════════════════════════════════════════════════════════════════
//  TIMEZONE
// ════════════════════════════════════════════════════════════════
#include "timezone_data.h"
#include "timezone_lookup.h"

static TzResult tzCurrent;
static bool     tzInitialized = false;

static void tzDisplayStr(char* buf, size_t bufLen) {
  if (tzInitialized && tzCurrent.valid && tzCurrent.country[0])
    snprintf(buf, bufLen, "%s %s", tzCurrent.country, tzCurrent.abbr);
  else if (tzInitialized && tzCurrent.valid)
    snprintf(buf, bufLen, "%s", tzCurrent.abbr);
  else
    snprintf(buf, bufLen, "UTC");
}

// ════════════════════════════════════════════════════════════════
//  CONFIGURATION
// ════════════════════════════════════════════════════════════════
#define DEBUG_MODE          true

#define GPS_RX_PIN          18
#define GPS_TX_PIN          17
#define GPS_BAUD_INIT       38400
#define GPS_BAUD_TARGET     115200
#define GPS_UPDATE_HZ       5

#define FIX_HDOP_MAX        2.0f
#define FIX_SATS_MIN        4
#define SPD_AVG_N           3
#define SPD_OUTLIER_SEC     3.0f

#define NVS_NS              "gps"
#define NVS_KEY_LAT         "lat"
#define NVS_KEY_LNG         "lng"
#define NVS_KEY_ALT         "alt"
#define NVS_KEY_TIME        "ts"
#define NVS_KEY_VALID       "ok"
#define NVS_SAVE_INTERVAL   600000UL
#define HOME_HINT_ENABLED   true

#define BL_NIGHT_FACTOR         64
#define BL_FALLBACK_NIGHT_START 21
#define BL_FALLBACK_NIGHT_END    7

#define FIX_HOLD_MS         5000UL

#define GPS_AVAIL_TIMEOUT_MS  8000UL
#define GPS_REINIT_INTERVAL_MS 10000UL

#define DISP_PWR_PIN        15
#define DISP_BL_PIN         38
#define BAT_ADC_PIN         4
#define BAT_SAMPLES         32
#define BAT_V_FACTOR        2.0f
#define BAT_UPDATE_MS       5000UL
#define BAT_V_ABSENT        2.5f

// GPIO 0  (BOOT/left)   INPUT_PULLUP — press → sleep
// GPIO 14 (KEY/right)   INPUT_PULLUP — wake from sleep (ext0)
#define BTN_SLEEP_PIN    0
#define BTN_WAKE_PIN     14
#define BTN_HOLD_MS      500

#define BAT_WARN_PCT        40
#define BAT_LOW_PCT         20
#define BAT_CRIT_PCT        10
#define BAT_SLEEP_PCT        5
#define BAT_HIBERN_PCT       2
#define BAT_SLEEP_WARN_MS   10000UL

#define PWR_SRC_UNKNOWN     0
#define PWR_SRC_BATTERY     1
#define PWR_SRC_USB         2
#define PWR_SRC_PIN5V       3

// ════════════════════════════════════════════════════════════════
//  UBX utilities for NEO-M10
// ════════════════════════════════════════════════════════════════

void ubxChecksum(const uint8_t* buf, int len, uint8_t& ckA, uint8_t& ckB) {
  ckA = ckB = 0;
  for (int i = 0; i < len; i++) { ckA += buf[i]; ckB += ckA; }
}

void ubxSend(HardwareSerial& ser, const uint8_t* payload, int payLen) {
  uint8_t ckA, ckB;
  ubxChecksum(payload, payLen, ckA, ckB);
  ser.write(0xB5); ser.write(0x62);
  ser.write(payload, payLen);
  ser.write(ckA); ser.write(ckB);
}

void ubxValSetU4(HardwareSerial& ser, uint32_t keyId, uint32_t val,
                 uint8_t layers = 0x01) {
  uint8_t pay[16] = {
    0x06, 0x8A,
    0x0C, 0x00,
    0x00,
    layers,
    0x00, 0x00,
    (uint8_t)(keyId),       (uint8_t)(keyId >> 8),
    (uint8_t)(keyId >> 16), (uint8_t)(keyId >> 24),
    (uint8_t)(val),       (uint8_t)(val >> 8),
    (uint8_t)(val >> 16), (uint8_t)(val >> 24),
  };
  ubxSend(ser, pay, sizeof(pay));
}

void ubxValSetU1(HardwareSerial& ser, uint32_t keyId, uint8_t val,
                 uint8_t layers = 0x01) {
  uint8_t pay[13] = {
    0x06, 0x8A,
    0x09, 0x00,
    0x00, layers, 0x00, 0x00,
    (uint8_t)(keyId),       (uint8_t)(keyId >> 8),
    (uint8_t)(keyId >> 16), (uint8_t)(keyId >> 24),
    val,
  };
  ubxSend(ser, pay, sizeof(pay));
}

// ════════════════════════════════════════════════════════════════
//  NEO-M10 configuration keys
// ════════════════════════════════════════════════════════════════
#define CFG_UART1_BAUDRATE          0x40520001UL
#define CFG_RATE_MEAS               0x30210001UL
#define CFG_RATE_NAV                0x30210002UL
#define CFG_NAVSPG_DYNMODEL         0x20110021UL
#define CFG_MSGOUT_NMEA_ID_GGA_UART1  0x209100BAUL
#define CFG_MSGOUT_NMEA_ID_GLL_UART1  0x209100CFUL
#define CFG_MSGOUT_NMEA_ID_GSA_UART1  0x209100C0UL
#define CFG_MSGOUT_NMEA_ID_GSV_UART1  0x209100C5UL
#define CFG_MSGOUT_NMEA_ID_RMC_UART1  0x209100ACUL
#define CFG_MSGOUT_NMEA_ID_VTG_UART1  0x209100B9UL
#define CFG_SBAS_USE_TESTMODE       0x10360002UL
#define CFG_SBAS_USE_RANGING        0x10360003UL
#define CFG_SBAS_USE_DIFFCORR       0x10360004UL
#define CFG_SBAS_PRNSCANMASK        0x50360006UL
#define CFG_SBAS_PRNSCANMASK_HI     0x50360007UL
#define CFG_SIGNAL_SBAS_ENA         0x10310020UL

#define SBAS_EGNOS_MASK_LO          0x0001000AUL
#define SBAS_EGNOS_MASK_HI          0x00000000UL

void ubxSetBaud(HardwareSerial& ser, uint32_t targetBaud) {
  uint8_t pay[] = {
    0x06, 0x00, 0x14, 0x00,
    0x01, 0x00, 0x00, 0x00,
    0xC0, 0x08, 0x00, 0x00,
    (uint8_t)(targetBaud),       (uint8_t)(targetBaud >> 8),
    (uint8_t)(targetBaud >> 16), (uint8_t)(targetBaud >> 24),
    0x07, 0x00, 0x03, 0x00,
    0x00, 0x00, 0x00, 0x00
  };
  ubxSend(ser, pay, sizeof(pay));
}

void ubxSetRate(HardwareSerial& ser, uint16_t hz) {
  uint16_t measMs = 1000 / hz;
  ubxValSetU4(ser, CFG_RATE_MEAS, measMs);
  delay(20);
  ubxValSetU4(ser, CFG_RATE_NAV,  1);
  delay(20);
}

void ubxSetNav5Automotive(HardwareSerial& ser) {
  ubxValSetU1(ser, CFG_NAVSPG_DYNMODEL, 4);
  delay(20);
}

void ubxConfigNmeaMessages(HardwareSerial& ser) {
  ubxValSetU1(ser, CFG_MSGOUT_NMEA_ID_GGA_UART1, 1); delay(10);
  ubxValSetU1(ser, CFG_MSGOUT_NMEA_ID_GLL_UART1, 0); delay(10);
  ubxValSetU1(ser, CFG_MSGOUT_NMEA_ID_GSA_UART1, 1); delay(10);
  ubxValSetU1(ser, CFG_MSGOUT_NMEA_ID_GSV_UART1, 1); delay(10);
  ubxValSetU1(ser, CFG_MSGOUT_NMEA_ID_RMC_UART1, 1); delay(10);
  ubxValSetU1(ser, CFG_MSGOUT_NMEA_ID_VTG_UART1, 0); delay(10);
}

void ubxEnableSBAS(HardwareSerial& ser) {
  ubxValSetU1(ser, CFG_SIGNAL_SBAS_ENA,    1); delay(10);
  ubxValSetU1(ser, CFG_SBAS_USE_RANGING,   1); delay(10);
  ubxValSetU1(ser, CFG_SBAS_USE_DIFFCORR,  1); delay(10);
  ubxValSetU1(ser, CFG_SBAS_USE_TESTMODE,  0); delay(10);
  ubxValSetU4(ser, CFG_SBAS_PRNSCANMASK,    SBAS_EGNOS_MASK_LO); delay(10);
  ubxValSetU4(ser, CFG_SBAS_PRNSCANMASK_HI, SBAS_EGNOS_MASK_HI); delay(10);
  if (DEBUG_MODE)
    Serial.println("SBAS: enabled (EGNOS)");
}

// ════════════════════════════════════════════════════════════════
//  UBX-MGA-INI: warm start for NEO-M10
// ════════════════════════════════════════════════════════════════

void ubxMgaIniPosLlh(HardwareSerial& ser,
                     int32_t latE7, int32_t lngE7, int32_t altCm,
                     uint32_t posAccCm = 300000) {
  uint8_t pay[24] = {
    0x13, 0x40,
    0x14, 0x00,
    0x01,
    0x00,
    0x00, 0x00,
    (uint8_t)(latE7),       (uint8_t)(latE7 >> 8),
    (uint8_t)(latE7 >> 16), (uint8_t)(latE7 >> 24),
    (uint8_t)(lngE7),       (uint8_t)(lngE7 >> 8),
    (uint8_t)(lngE7 >> 16), (uint8_t)(lngE7 >> 24),
    (uint8_t)(altCm),       (uint8_t)(altCm >> 8),
    (uint8_t)(altCm >> 16), (uint8_t)(altCm >> 24),
    (uint8_t)(posAccCm),       (uint8_t)(posAccCm >> 8),
    (uint8_t)(posAccCm >> 16), (uint8_t)(posAccCm >> 24),
  };
  ubxSend(ser, pay, sizeof(pay));
}

void ubxMgaIniTimeUtc(HardwareSerial& ser,
                      uint16_t yr, uint8_t mo, uint8_t day,
                      uint8_t hr, uint8_t min, uint8_t sec) {
  uint32_t timeAccNs = 2000000000UL;
  uint8_t pay[28] = {
    0x13, 0x40,
    0x18, 0x00,
    0x10,
    0x00,
    0x00,
    0x80,
    (uint8_t)(yr), (uint8_t)(yr >> 8),
    mo, day, hr, min, sec,
    0x00,
    (uint8_t)(timeAccNs),       (uint8_t)(timeAccNs >> 8),
    (uint8_t)(timeAccNs >> 16), (uint8_t)(timeAccNs >> 24),
    0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00,
  };
  ubxSend(ser, pay, sizeof(pay));
}

// ════════════════════════════════════════════════════════════════
//  OBJECTS
// ════════════════════════════════════════════════════════════════
TFT_eSPI       tft;
TinyGPSPlus    gps;
HardwareSerial gpsSerial(1);
Preferences    prefs;

// ════════════════════════════════════════════════════════════════
//  COLORS RGB565
// ════════════════════════════════════════════════════════════════
#define C_BG        0x0000
#define C_SPEED     0x07FF
#define C_UNIT      0x528A
#define C_TIME      0xFFFF
#define C_TZ        0x8410
#define C_COORD     0x8410
#define C_ALT       0x8410
#define C_DIR       0xFFE0
#define C_SAT_OK    0x07E0
#define C_SAT_WARN  0xFD20
#define C_NO_FIX    0xF800
#define C_DIV       0x2945

bool nightModeActive = false;

inline uint16_t dimColor(uint16_t color, uint8_t factor) {
  if (color == 0x0000 || factor == 0) return 0x0000;
  uint8_t r = ((color >> 11) & 0x1F);
  uint8_t g = ((color >>  5) & 0x3F);
  uint8_t b = ( color        & 0x1F);
  r = (uint8_t)((r * factor) >> 8);
  g = (uint8_t)((g * factor) >> 8);
  b = (uint8_t)((b * factor) >> 8);
  return (uint16_t)(r << 11) | (uint16_t)(g << 5) | b;
}
inline uint16_t C(uint16_t color) {
  return nightModeActive ? dimColor(color, BL_NIGHT_FACTOR) : color;
}

// ════════════════════════════════════════════════════════════════
//  SCREEN ZONES 320×170 (landscape, rotation=1 or 3)
// ════════════════════════════════════════════════════════════════
#define SPD_ZONE_Y    0
#define SPD_ZONE_H   110
#define DIV_Y        110
#define INF_Y        113
#define INF_H         57
#define INF_SPLIT_X  125

#define SPD_RIGHT_X   230
#define SPD_Y           7
#define KMPH_Y         (SPD_Y + 96 - 26)

#define BAT_X         249
#define BAT_W          62
#define BAT_RIGHT     310

#define BAT_ROW_VOLT   22
#define BAT_ROW_ICON   33
#define BAT_ROW_PCT    45

#define BAT_BODY_W     46
#define BAT_BODY_H      9
#define BAT_SEG_N       5
#define BAT_SEG_W       7
#define BAT_SEG_H       5
#define BAT_SEG_GAP     1
#define BAT_SEG0_X    (BAT_X+2)
#define BAT_SEG_Y     (BAT_ROW_ICON+2)
#define BAT_ZONE_H     54

// ════════════════════════════════════════════════════════════════
//  TIMEZONE
// ════════════════════════════════════════════════════════════════
static int daysInMonth(int month, int year) {
  static const int dim[] = { 0,31,28,31,30,31,30,31,31,30,31,30,31 };
  if (month == 2) {
    bool leap = (year%4==0 && year%100!=0) || (year%400==0);
    return leap ? 29 : 28;
  }
  return dim[month];
}

// Returns local hour; modifies lD/lM/lY for day rollover.
// utc_offset_h from tz_lookup already includes DST correction.
// Supports fractional offsets (+5:30, +5:45, +9:30, etc.).
int applyTimezone(int utcH, int utcDay, int utcMon, int utcYr,
                  int &lD, int &lM, int &lY) {
  float offF = tzInitialized ? tzCurrent.utc_offset_h : 1.0f;
  int offTotalMin = (int)roundf(offF * 60.0f);
  lD=utcDay; lM=utcMon; lY=utcYr;
  int totalMin = utcH * 60 + offTotalMin;
  if (totalMin >= 1440) { totalMin -= 1440; lD++; if(lD>daysInMonth(lM,lY)){lD=1;lM++;} if(lM>12){lM=1;lY++;} }
  if (totalMin <     0) { totalMin += 1440; lD--; if(lD<1){lM--; if(lM<1){lM=12;lY--;} lD=daysInMonth(lM,lY);} }
  return totalMin / 60;
}

void updateTimezone(float lat, float lng,
                    uint16_t year, uint8_t month, uint8_t mday,
                    uint8_t hour, uint8_t minute) {
  TzResult r = tz_lookup(lat, lng, year, month, mday, hour, minute);
  if (r.valid) {
    tzCurrent     = r;
    tzInitialized = true;
    if (DEBUG_MODE)
      Serial.printf("[TZ] Updated: %s %s (UTC%+.1f%s) at %.5f,%.5f\n",
                    r.country, r.abbr, r.utc_offset_h,
                    r.is_dst ? " DST" : "", lat, lng);
  } else {
    if (!tzInitialized) {
      memset(&tzCurrent, 0, sizeof(tzCurrent));
      tzCurrent.valid = 1;
      strncpy(tzCurrent.abbr, "UTC", sizeof(tzCurrent.abbr)-1);
      tzCurrent.utc_offset_h = 0.0f;
      tzCurrent.is_dst       = 0;
      tzInitialized          = true;
      if (DEBUG_MODE) Serial.println("[TZ] Lookup failed at init — using UTC");
    } else {
      if (DEBUG_MODE)
        Serial.printf("[TZ] Lookup failed — keeping last: %s %s\n",
                      tzCurrent.country, tzCurrent.abbr);
    }
  }
}

const char* courseToDir(float deg) {
  static const char* dirs[8] = { "N","NE","E","SE","S","SW","W","NW" };
  return dirs[(int)((deg+22.5f)/45.0f)%8];
}

// ════════════════════════════════════════════════════════════════
//  DISPLAY STATE VARIABLES
// ════════════════════════════════════════════════════════════════
int   prevSpeed      = -1;
int   prevSat        = -1;
int   prevSbas2      = -2;
bool  prevFix        = false;
bool  prevFixGrace   = false;
bool  prevFixTime    = false;
float prevLat        = 1e9f, prevLng = 1e9f, prevAlt = -99999;
float prevCourse     = -1;
int   prevHour       = -1, prevMinute = -1;
bool  courseVisible  = false;
bool  prevNightMode  = false;

// Screen 2 (big speed) state
bool  screen2Active  = false;
int   prevScreen2Speed = -1;
bool  prevScreen2Fix   = false;

// Prev-state buffers for char-by-char rendering
char  prevTimeBuf[6]   = "";
int   prevTimeXBuf[8]  = {0};
char  prevDateBuf[11]  = "";
char  prevTzBuf[16]    = "";
char  prevLatBuf[12]   = "";
char  prevLngBuf[12]   = "";
char  prevAltBuf[12]   = "";
char  prevHdopBuf[12]  = "";
char  prevVdopBuf[12]  = "";
char  prevDirBuf[4]    = "";
char  prevSatBuf[16]   = "";
char  prevEgnosBuf[16] = "";
bool  prevSatFix       = false;

// Battery
float batVoltage     = 0.0f;
int   batPercent     = -1;
bool  batCharging    = false;
bool  batPresent     = false;
uint8_t pwrSource    = PWR_SRC_UNKNOWN;
uint8_t prevPwrSource = PWR_SRC_UNKNOWN;
int   prevBatPct     = -99;
bool  prevBatCharge  = false;
bool  prevBatPresent = false;
bool  prevShowLightning = false;
unsigned long lastBatMs = 0;

// Battery animation
int  chargeAnimSeg = 0;
unsigned long chargeAnimMs = 0;
#define CHARGE_ANIM_MS 400
bool batBlinkState = true;
unsigned long batBlinkMs = 0;
#define BAT_BLINK_MS 400

bool initError = false;

// GPS module availability
unsigned long gpsLastByteMs    = 0;
bool          gpsAvailable     = false;
bool          prevGpsAvailable = false;
unsigned long gpsLastNmeaMs    = 0;
unsigned long gpsLastReinitMs  = 0;
unsigned long gpsReinitDeadlineMs = 0;
uint8_t       gpsDumpCount     = 0;

// Fix grace period
unsigned long fixLostMs  = 0;
bool fixGracePrev        = false;
int  heldSpd             = 0;
int  heldUtcH=-1, heldUtcM=-1, heldUtcDay=1, heldUtcMon=1, heldUtcYr=2024;

static int gpsSatsVisible = 0;

// ════════════════════════════════════════════════════════════════
//  DATA FILTERING
// ════════════════════════════════════════════════════════════════
float spdHistory[SPD_AVG_N] = {0};
int   spdIdx = 0;

static float median3(float a, float b, float c) {
  if (a>b){float t=a;a=b;b=t;} if(b>c){float t=b;b=c;c=t;} if(a>b){float t=a;a=b;b=t;}
  return b;
}

float addSpeedSample(float kmph) {
  if (kmph < 0.0f) kmph = 0.0f;
  spdHistory[spdIdx % SPD_AVG_N] = kmph;
  spdIdx++;
  if (spdIdx < SPD_AVG_N) {
    float sum=0; for(int i=0;i<spdIdx;i++) sum+=spdHistory[i]; return sum/spdIdx;
  }
  return median3(spdHistory[0], spdHistory[1], spdHistory[2]);
}

float lastAcceptedLat=0, lastAcceptedLng=0;
bool  lastAcceptedValid=false;
unsigned long lastAcceptedMs=0;

float approxDistM(float lat1,float lng1,float lat2,float lng2) {
  const float R=6371000.0f;
  float dlat=(lat2-lat1)*(3.14159265f/180.0f);
  float dlng=(lng2-lng1)*(3.14159265f/180.0f);
  float mlat=(lat1+lat2)*0.5f*(3.14159265f/180.0f);
  float dx=dlng*R*cosf(mlat), dy=dlat*R;
  return sqrtf(dx*dx+dy*dy);
}

bool coordOutlierCheck(float lat,float lng,float spdKmph) {
  if (!lastAcceptedValid) return true;
  float dtSec = (millis() - lastAcceptedMs) / 1000.0f;
  if (dtSec < 0.05f) dtSec = 0.05f;
  if (dtSec > 10.0f) dtSec = 10.0f;
  float maxDist = (spdKmph / 3.6f) * dtSec;
  if (maxDist < 50.0f) maxDist = 50.0f;
  return approxDistM(lastAcceptedLat, lastAcceptedLng, lat, lng) <= maxDist;
}

// ════════════════════════════════════════════════════════════════
//  NVS
// ════════════════════════════════════════════════════════════════
GpsCache loadGpsCache() {
  GpsCache c={0,0,0,0,false};
  prefs.begin(NVS_NS,true);
  c.valid=prefs.getBool(NVS_KEY_VALID,false);
  if(c.valid){
    c.lat=prefs.getFloat(NVS_KEY_LAT,0.f);
    c.lng=prefs.getFloat(NVS_KEY_LNG,0.f);
    c.alt=prefs.getFloat(NVS_KEY_ALT,0.f);
    c.unixTs=prefs.getUInt(NVS_KEY_TIME,0);
  }
  prefs.end(); return c;
}

void saveGpsCache(float lat,float lng,float alt,uint32_t ts) {
  prefs.begin(NVS_NS,false);
  prefs.putBool(NVS_KEY_VALID,true); prefs.putFloat(NVS_KEY_LAT,lat);
  prefs.putFloat(NVS_KEY_LNG,lng);   prefs.putFloat(NVS_KEY_ALT,alt);
  prefs.putUInt(NVS_KEY_TIME,ts);    prefs.end();
}

uint32_t gpsToUnix(int d,int mo,int y,int h,int mi,int s) {
  int yy=y,mm=mo;
  if(mm<3){yy--;mm+=12;}
  long days=(long)(365.25*(double)(yy+4716))+(long)(30.6001*(double)(mm+1))+d-1524;
  days-=2440588L;
  return (uint32_t)(days*86400L+h*3600L+mi*60L+s);
}

unsigned long lastNvsSaveMs=0;
static bool tzFirstRun = true;

// ════════════════════════════════════════════════════════════════
//  BATTERY — ADC only
// ════════════════════════════════════════════════════════════════
struct AdcCalPoint { float raw; float real; };

#define ADC_CAL_MAX_N  8
#define NVS_NS_CAL     "adccal"
#define NVS_KEY_CAL    "pts"

static const AdcCalPoint ADC_CAL_DEFAULT[] = {
  { 2.80f, 3.30f }, { 3.10f, 3.55f }, { 3.35f, 3.75f },
  { 3.55f, 3.86f }, { 3.78f, 4.13f }, { 3.92f, 4.20f },
};
static const int ADC_CAL_DEFAULT_N = sizeof(ADC_CAL_DEFAULT)/sizeof(ADC_CAL_DEFAULT[0]);

static AdcCalPoint adcCal[ADC_CAL_MAX_N];
static int         adcCalN = 0;

void loadAdcCal() {
  Preferences p;
  p.begin(NVS_NS_CAL, true);
  size_t len = p.getBytesLength(NVS_KEY_CAL);
  bool loaded = false;
  if (len > 0 && len <= sizeof(adcCal) && (len % sizeof(AdcCalPoint)) == 0) {
    p.getBytes(NVS_KEY_CAL, adcCal, len);
    adcCalN = (int)(len / sizeof(AdcCalPoint));
    loaded = true;
  }
  p.end();
  if (!loaded) {
    memcpy(adcCal, ADC_CAL_DEFAULT, sizeof(ADC_CAL_DEFAULT));
    adcCalN = ADC_CAL_DEFAULT_N;
  }
  if (DEBUG_MODE) Serial.printf("[ADC_CAL] %s, %d points\n", loaded ? "NVS" : "default", adcCalN);
}

void saveAdcCal() {
  Preferences p;
  p.begin(NVS_NS_CAL, false);
  p.putBytes(NVS_KEY_CAL, adcCal, adcCalN * sizeof(AdcCalPoint));
  p.end();
  if (DEBUG_MODE) Serial.printf("[ADC_CAL] Saved %d points to NVS\n", adcCalN);
}

// Parses "CAL:raw,real;raw,real;..." from Serial and updates NVS.
bool tryParseAdcCalCmd(const char* line) {
  if (strncmp(line, "CAL:", 4) != 0) return false;
  AdcCalPoint tmp[ADC_CAL_MAX_N];
  int n = 0;
  const char* p = line + 4;
  while (*p && n < ADC_CAL_MAX_N) {
    float r = atof(p);
    const char* comma = strchr(p, ',');
    if (!comma) break;
    float v = atof(comma + 1);
    tmp[n++] = {r, v};
    const char* semi = strchr(comma, ';');
    if (!semi) break;
    p = semi + 1;
  }
  if (n < 2) { Serial.println("[ADC_CAL] Parse error: need at least 2 points"); return false; }
  memcpy(adcCal, tmp, n * sizeof(AdcCalPoint));
  adcCalN = n;
  saveAdcCal();
  Serial.printf("[ADC_CAL] Updated: %d points\n", n);
  return true;
}

float adcCorrect(float raw) {
  if (adcCalN < 1) return raw;
  if (raw <= adcCal[0].raw)          return adcCal[0].real;
  if (raw >= adcCal[adcCalN-1].raw)  return adcCal[adcCalN-1].real;
  for (int i = 0; i < adcCalN-1; i++) {
    if (raw <= adcCal[i+1].raw) {
      float t = (raw - adcCal[i].raw) / (adcCal[i+1].raw - adcCal[i].raw);
      return adcCal[i].real + t * (adcCal[i+1].real - adcCal[i].real);
    }
  }
  return raw;
}

int voltageToPercent(float v) {
  if (v>=4.20f) return 100;
  if (v>=4.06f) return (int)(80+(v-4.06f)/(4.20f-4.06f)*20);
  if (v>=3.86f) return (int)(60+(v-3.86f)/(4.06f-3.86f)*20);
  if (v>=3.75f) return (int)(40+(v-3.75f)/(3.86f-3.75f)*20);
  if (v>=3.55f) return (int)(20+(v-3.55f)/(3.75f-3.55f)*20);
  if (v>=3.30f) return (int)((v-3.30f)/(3.55f-3.30f)*20);
  return 0;
}

void readBattery() {
  long sum=0;
  for(int i=0;i<BAT_SAMPLES;i++){sum+=analogRead(BAT_ADC_PIN);delay(1);}
  float adc=(float)sum/BAT_SAMPLES;
  float vRaw=(adc/4095.0f)*3.3f*BAT_V_FACTOR;
  batVoltage=adcCorrect(vRaw);

  if (vRaw >= 4.20f) {
    pwrSource   = PWR_SRC_USB;
    batCharging = true;
  } else {
    pwrSource   = PWR_SRC_BATTERY;
    batCharging = false;
  }
  batPresent = (vRaw >= BAT_V_ABSENT);
  batPercent = batPresent ? voltageToPercent(batVoltage) : -1;

  if (DEBUG_MODE && pwrSource != prevPwrSource) {
    prevPwrSource = pwrSource;
    Serial.printf("[PWR] %s\n", pwrSource==PWR_SRC_USB ? "USB/EXT" : "BATTERY");
  }
  if (DEBUG_MODE)
    Serial.printf("[BAT] ADC=%.0f vRaw=%.3fV vCal=%.3fV pct=%d%% src=%s\n",
                  adc, vRaw, batVoltage, batPercent,
                  pwrSource==PWR_SRC_USB ? "USB" : "BAT");
}

// ════════════════════════════════════════════════════════════════
//  DEEP SLEEP
// ════════════════════════════════════════════════════════════════
void saveGpsCacheOnSleep() {
  if (!lastAcceptedValid || !gps.date.isValid() || !gps.time.isValid()) return;
  float alt=gps.altitude.isValid()?gps.altitude.meters():0.f;
  uint32_t ts=gpsToUnix(gps.date.day(),gps.date.month(),gps.date.year(),
                        gps.time.hour(),gps.time.minute(),gps.time.second());
  saveGpsCache(lastAcceptedLat,lastAcceptedLng,alt,ts);
  if(DEBUG_MODE) Serial.printf("[NVS] Sleep-save: %.5f, %.5f\n",lastAcceptedLat,lastAcceptedLng);
}

void enterDeepSleep(const char* reason="Manual") {
  if(DEBUG_MODE) Serial.printf("[POWER] Deep sleep: %s\n",reason);
  saveGpsCacheOnSleep();
  tft.fillScreen(C_BG);
  uint8_t pmreq[]={0x02,0x41,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00};
  ubxSend(gpsSerial,pmreq,sizeof(pmreq));
  delay(100);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_WAKE_PIN, 0);
  esp_deep_sleep_start();
}

void enterHibernation() {
  if(DEBUG_MODE) Serial.println("[POWER] Hibernation");
  saveGpsCacheOnSleep();
  tft.fillScreen(C_BG);
  uint8_t pmreq[]={0x02,0x41,0x08,0x00,0x00,0x00,0x00,0x00,0x02,0x00,0x00,0x00};
  ubxSend(gpsSerial,pmreq,sizeof(pmreq));
  delay(50);
  esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)BTN_WAKE_PIN, 0);
  esp_deep_sleep_start();
}

// ════════════════════════════════════════════════════════════════
//  BUTTONS
//  GPIO 0  (BOOT/left)  INPUT_PULLUP — hold → sleep
//  GPIO 14 (KEY/right)  INPUT_PULLUP — wake source (ext0) / screen 2 toggle
//
//  Both pins active LOW. Debounce via delay(10) at end of loop().
//  Action fires on release after holding BTN_HOLD_MS.
// ════════════════════════════════════════════════════════════════
unsigned long pressTime0   = 0;
bool          button0Pressed = false;

unsigned long pressTime14   = 0;
bool          button14Pressed = false;

void checkSleepButton() {
  if (digitalRead(BTN_SLEEP_PIN) == LOW) {
    if (!button0Pressed) {
      pressTime0    = millis();
      button0Pressed = true;
      if (DEBUG_MODE) Serial.println("[BTN0] pressed");
    }
  } else {
    if (button0Pressed) {
      unsigned long holdTime = millis() - pressTime0;
      button0Pressed = false;
      if (DEBUG_MODE) Serial.printf("[BTN0] released, hold=%lums\n", holdTime);
      if (holdTime >= BTN_HOLD_MS) {
        enterDeepSleep("Manual");
      }
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  SCREEN 2 BUTTON (GPIO 14 / KEY)
//  During normal operation toggles screen 2 (big speed display).
//  Also used as wake source from deep sleep.
// ════════════════════════════════════════════════════════════════

// Forward declarations
void drawUI();
void drawScreen2(int speed, bool hasFixGrace, bool forceRedraw);

void checkWakeButton() {
  if (digitalRead(BTN_WAKE_PIN) == LOW) {
    if (!button14Pressed) {
      pressTime14     = millis();
      button14Pressed = true;
      if (DEBUG_MODE) Serial.println("[BTN14] pressed");
    }
  } else {
    if (button14Pressed) {
      unsigned long holdTime = millis() - pressTime14;
      button14Pressed = false;
      if (DEBUG_MODE) Serial.printf("[BTN14] released, hold=%lums\n", holdTime);
      if (holdTime < BTN_HOLD_MS * 4) {  // short press — toggle screen 2
        screen2Active = !screen2Active;
        if (screen2Active) {
          tft.fillScreen(C_BG);
          prevScreen2Speed = -1;
          prevScreen2Fix   = false;
          if (DEBUG_MODE) Serial.println("[SCR] Screen 2 ON");
        } else {
          // Restore screen 1
          prevSpeed = -1; prevSat = -1; prevSbas2 = -2; prevHour = -1;
          prevTimeBuf[0]='\0'; prevDateBuf[0]='\0'; prevTzBuf[0]='\0';
          prevLatBuf[0]='\0';  prevLngBuf[0]='\0';  prevAltBuf[0]='\0';
          prevHdopBuf[0]='\0'; prevVdopBuf[0]='\0';
          prevDirBuf[0]='\0';  prevSatBuf[0]='\0';  prevEgnosBuf[0]='\0';
          prevSatFix=false; prevFix=false; prevFixGrace=false; prevFixTime=false;
          prevLat=1e9f; prevLng=1e9f; prevAlt=-99999;
          prevBatPct=-99; prevShowLightning=!prevShowLightning;
          memset(prevTimeXBuf, 0, sizeof(prevTimeXBuf));
          drawUI();
          if (DEBUG_MODE) Serial.println("[SCR] Screen 1 restored");
        }
      }
    }
  }
}

unsigned long batSleepWarnStart=0;
void checkLowBatterySleep() {
  if (!batPresent||batCharging){batSleepWarnStart=0;return;}
  if (batPercent<0){batSleepWarnStart=0;return;}
  if (batPercent<=BAT_HIBERN_PCT){enterHibernation();return;}
  if (batPercent>=BAT_SLEEP_PCT){batSleepWarnStart=0;return;}
  unsigned long now=millis();
  if(batSleepWarnStart==0){
    batSleepWarnStart=now;
    tft.fillRect(0,0,SPD_RIGHT_X,SPD_ZONE_H,C_BG);
    tft.setTextFont(4);tft.setTextSize(1);tft.setTextColor(C(C_NO_FIX),C_BG);
    tft.setCursor(4,10);tft.print("LOW BATTERY");
    tft.setCursor(4,45);tft.printf("  %d%%",batPercent);
    tft.setCursor(4,75);tft.print("Sleeping...");
    return;
  }
  if(now-batSleepWarnStart>=BAT_SLEEP_WARN_MS) enterDeepSleep("Low battery");
}

// ════════════════════════════════════════════════════════════════
//  NMEA buffer and SBAS parser
// ════════════════════════════════════════════════════════════════
#define NMEA_BUF 128
char    nmeaBuf[NMEA_BUF];
uint8_t nmeaPos=0;

int  sbasSatsVisible=0, sbasSatsUsed=0;
#define SBAS_TIMEOUT_MS 5000UL
unsigned long lastGsvMs=0;

static uint64_t gsvSbasSeenVis    = 0;
static uint64_t gsvSbasSeenUsd    = 0;
#define GSV_TALKER_N  6
static int      gsvTalkerMaxTotal[GSV_TALKER_N] = {0};
#define GSV_CYCLE_MIN_MS  100UL
static unsigned long gsvCycleOpenedMs = 0;
static bool     gsvCycleStarted   = false;
static uint64_t gsvPendingSbasVis = 0;
static uint64_t gsvPendingSbasUsd = 0;
static int      gsvPendingNonSbas = 0;
static bool     gsvPendingReady   = false;

void checkSbasTimeout() {
  if((sbasSatsVisible>0||sbasSatsUsed>0||gpsSatsVisible>0)&&(millis()-lastGsvMs>=SBAS_TIMEOUT_MS)){
    sbasSatsVisible=0; sbasSatsUsed=0; gpsSatsVisible=0;
    gsvPendingSbasVis=0; gsvPendingSbasUsd=0; gsvPendingNonSbas=0; gsvPendingReady=false;
    gsvSbasSeenVis=0;    gsvSbasSeenUsd=0;
    memset(gsvTalkerMaxTotal, 0, sizeof(gsvTalkerMaxTotal));
    gsvCycleStarted=false; gsvCycleOpenedMs=0;
  }
}

void parseGsvForSbas(const char* line) {
  if(line[0]!='$') return;
  if(strncmp(line+3,"GSV",3)!=0) return;
  lastGsvMs=millis();

  char talker[3] = {line[1], line[2], '\0'};
  int talker_idx = 5;
  if      (strcmp(talker,"GP")==0) talker_idx=0;
  else if (strcmp(talker,"GA")==0) talker_idx=1;
  else if (strcmp(talker,"GL")==0) talker_idx=2;
  else if (strcmp(talker,"GB")==0) talker_idx=3;
  else if (strcmp(talker,"GQ")==0) talker_idx=4;

  int field=0, pos=0, tokLen=0, currentPrn=0, msgNum=0, totalInView=0;
  bool inMsg=true;
  char tok[8];

  while(inMsg){
    char c=line[pos++];
    if(c==','||c=='*'||c=='\0'||c=='\r'||c=='\n'){
      tok[tokLen]='\0'; tokLen=0;
      if(field==2){
        msgNum=atoi(tok);
        if(msgNum==1 && strcmp(talker,"GP")==0){
          unsigned long nowMs = millis();
          if(nowMs - gsvCycleOpenedMs >= GSV_CYCLE_MIN_MS){
            int nonSbas = 0;
            for(int i=0;i<GSV_TALKER_N;i++) nonSbas += gsvTalkerMaxTotal[i];
            gsvPendingSbasVis = gsvSbasSeenVis;
            gsvPendingSbasUsd = gsvSbasSeenUsd;
            gsvPendingNonSbas = nonSbas;
            gsvPendingReady   = gsvCycleStarted;
            gsvSbasSeenVis    = 0;
            gsvSbasSeenUsd    = 0;
            memset(gsvTalkerMaxTotal, 0, sizeof(gsvTalkerMaxTotal));
            gsvCycleStarted   = true;
            gsvCycleOpenedMs  = nowMs;
          }
        }
      }
      if(field==3){
        totalInView = atoi(tok);
        if(totalInView > gsvTalkerMaxTotal[talker_idx])
          gsvTalkerMaxTotal[talker_idx] = totalInView;
      }
      if(field>=4){
        int sub=(field-4)%4;
        if(sub==0){
          currentPrn=atoi(tok);
        } else if(sub==3){
          int snr = (tok[0] != '\0') ? atoi(tok) : 0;
          int realPrn = currentPrn;
          if(currentPrn >= 33 && currentPrn <= 64)
            realPrn = currentPrn + 87;
          if(realPrn >= 120 && realPrn <= 158){
            int bit = realPrn - 120;
            uint64_t mask = (uint64_t)1 << bit;
            bool isNew = !(gsvSbasSeenVis & mask);
            gsvSbasSeenVis |= mask;
            if(snr > 0) gsvSbasSeenUsd |= mask;
            if(DEBUG_MODE)
              Serial.printf("[SBAS] NMEA=%d PRN=%d SNR=%d%s\n",
                            currentPrn, realPrn, snr, isNew ? "" : " dup");
          }
        }
      }
      field++;
      if(c=='*'||c=='\0'||c=='\r'||c=='\n') inMsg=false;
    } else { if(tokLen<(int)(sizeof(tok)-1)) tok[tokLen++]=c; }
  }
}

// ════════════════════════════════════════════════════════════════
//  $GPGSA / $GNGSA parser — extracts VDOP (field 17)
//  NEO-M10 emits one GSA per active constellation; we keep the
//  maximum VDOP seen in the current epoch (worst-case vertical DOP).
//  Values are reset when no GSA arrives within SBAS_TIMEOUT_MS.
// ════════════════════════════════════════════════════════════════
static float  gsaVdop        = 0.0f;
static bool   gsaVdopValid   = false;
static float  gsaVdopPending = 0.0f;
static bool   gsaVdopPendingValid = false;
static unsigned long lastGsaMs = 0;

void parseGsaForVdop(const char* line) {
  // Accept $G?GSA (GPGSA, GLGSA, GAGSA, GNGSA, …)
  if (line[0] != '$') return;
  if (strncmp(line + 3, "GSA", 3) != 0) return;
  lastGsaMs = millis();

  // Fields: $--GSA,mode1,mode2,sv1..sv12,pdop,hdop,vdop[,systemId]*cs
  // field indices (0-based after '$--GSA'):
  //   0: talker+msgid (already consumed by position)
  //   1: mode1  (A/M)
  //   2: mode2  (1/2/3)
  //   3-14: SV PRNs (12 fields)
  //   15: PDOP
  //   16: HDOP
  //   17: VDOP
  int field = 0, pos = 0, tokLen = 0;
  char tok[12];
  bool inMsg = true;

  while (inMsg) {
    char c = line[pos++];
    if (c == ',' || c == '*' || c == '\0' || c == '\r' || c == '\n') {
      tok[tokLen] = '\0'; tokLen = 0;
      if (field == 17 && tok[0] != '\0') {
        float v = atof(tok);
        if (v > 0.0f) {
          // Keep highest VDOP seen this epoch (multiple GSA sentences)
          if (!gsaVdopPendingValid || v > gsaVdopPending) {
            gsaVdopPending      = v;
            gsaVdopPendingValid = true;
          }
        }
      }
      field++;
      if (c == '*' || c == '\0' || c == '\r' || c == '\n') inMsg = false;
    } else {
      if (tokLen < (int)(sizeof(tok) - 1)) tok[tokLen++] = c;
    }
  }
}

// Publish pending VDOP (called once per loop, same as applyGsvPending).
void applyGsaPending() {
  if (gsaVdopPendingValid) {
    gsaVdop             = gsaVdopPending;
    gsaVdopValid        = true;
    gsaVdopPending      = 0.0f;
    gsaVdopPendingValid = false;
  }
  // Expire stale VDOP
  if (gsaVdopValid && (millis() - lastGsaMs) >= SBAS_TIMEOUT_MS) {
    gsaVdopValid = false;
    gsaVdop      = 0.0f;
  }
}

void applyGsvPending() {
  if (gsvPendingReady) {
    sbasSatsVisible = __builtin_popcountll(gsvPendingSbasVis);
    sbasSatsUsed    = __builtin_popcountll(gsvPendingSbasUsd);
    gpsSatsVisible  = gsvPendingNonSbas;
    gsvPendingReady = false;
  }
}

// ════════════════════════════════════════════════════════════════
//  BATTERY DISPLAY
// ════════════════════════════════════════════════════════════════
static void drawBatteryIcon(int filledSegs,uint16_t col,int blinkSeg=-1,int glowSeg=-1){
  tft.fillRect(BAT_X,BAT_ROW_ICON,BAT_BODY_W+4,BAT_BODY_H,C_BG);
  tft.drawRect(BAT_X,BAT_ROW_ICON,BAT_BODY_W,BAT_BODY_H,C(C_TZ));
  tft.fillRect(BAT_X+BAT_BODY_W,BAT_ROW_ICON+2,3,BAT_BODY_H-4,C(C_TZ));
  for(int i=0;i<BAT_SEG_N;i++){
    int sx=BAT_SEG0_X+i*(BAT_SEG_W+BAT_SEG_GAP);
    bool filled=(i<filledSegs),isBlink=(i==blinkSeg),isGlow=(i==glowSeg);
    if(filled||isGlow) tft.fillRect(sx,BAT_SEG_Y,BAT_SEG_W,BAT_SEG_H,isBlink?C_BG:col);
    else               tft.drawRect(sx,BAT_SEG_Y,BAT_SEG_W,BAT_SEG_H,C(0x2104));
  }
}

static void drawLightningIcon(){
  int cx=BAT_X+23, y0=BAT_ROW_ICON+1;
  tft.fillRect(BAT_X,BAT_ROW_ICON,BAT_BODY_W+4,BAT_BODY_H,C_BG);
  tft.drawRect(BAT_X,BAT_ROW_ICON,BAT_BODY_W,BAT_BODY_H,C(C_TZ));
  tft.fillRect(BAT_X+BAT_BODY_W,BAT_ROW_ICON+2,3,BAT_BODY_H-4,C(C_TZ));
  tft.drawFastHLine(cx-1,y0-1,4,C_BG);tft.drawFastHLine(cx-2,y0,2,C_BG);
  tft.drawFastHLine(cx+2,y0,1,C_BG);  tft.drawFastHLine(cx-3,y0+1,2,C_BG);
  tft.drawFastHLine(cx+2,y0+1,1,C_BG);tft.drawFastHLine(cx-4,y0+2,2,C_BG);
  tft.drawFastHLine(cx+1,y0+2,4,C_BG);tft.drawFastHLine(cx-4,y0+3,1,C_BG);
  tft.drawFastHLine(cx+4,y0+3,1,C_BG);tft.drawFastHLine(cx-4,y0+4,4,C_BG);
  tft.drawFastHLine(cx+3,y0+4,2,C_BG);tft.drawFastHLine(cx-2,y0+5,1,C_BG);
  tft.drawFastHLine(cx+2,y0+5,2,C_BG);tft.drawFastHLine(cx-2,y0+6,1,C_BG);
  tft.drawFastHLine(cx+1,y0+6,2,C_BG);tft.drawFastHLine(cx-2,y0+7,4,C_BG);
  uint16_t col=C(C_SAT_OK);
  tft.drawFastHLine(cx+1,y0-1,1,col);tft.drawFastHLine(cx,y0,2,col);
  tft.drawFastHLine(cx-1,y0+1,3,col);tft.drawFastHLine(cx-2,y0+2,3,col);
  tft.drawFastHLine(cx-3,y0+3,7,col);tft.drawFastHLine(cx,y0+4,3,col);
  tft.drawFastHLine(cx-1,y0+5,3,col);tft.drawFastHLine(cx-1,y0+6,2,col);
  tft.drawFastHLine(cx-1,y0+7,1,col);
}

void updateBatteryDisplay(bool forceRedraw=false) {
  // Never draw battery widgets on screen 2
  if (screen2Active) return;

  unsigned long now=millis();
  bool externalPower=(pwrSource==PWR_SRC_USB||pwrSource==PWR_SRC_PIN5V);
  bool showLightning=externalPower||(batVoltage>=4.20f);

  if(!batPresent&&!externalPower){
    if(prevBatPresent||prevPwrSource!=pwrSource){
      tft.fillRect(BAT_X,BAT_ROW_VOLT,BAT_W,BAT_ZONE_H,C_BG);
      prevBatPresent=false;prevBatPct=-99;prevBatCharge=false;
    }
    return;
  }
  bool blinkTick=false,animTick=false;
  if(!showLightning){
    if(batPercent<BAT_CRIT_PCT&&!batCharging){
      if(now-batBlinkMs>=BAT_BLINK_MS){batBlinkMs=now;batBlinkState=!batBlinkState;blinkTick=true;}
    } else batBlinkState=true;
    if(batCharging){
      if(now-chargeAnimMs>=CHARGE_ANIM_MS){chargeAnimMs=now;chargeAnimSeg=(chargeAnimSeg+1)%BAT_SEG_N;animTick=true;}
    } else chargeAnimSeg=0;
  }
  bool pwrChanged=(externalPower!=(prevPwrSource==PWR_SRC_USB||prevPwrSource==PWR_SRC_PIN5V));
  bool needDraw=forceRedraw||blinkTick||animTick||pwrChanged||
                (showLightning!=prevShowLightning)||(batPercent!=prevBatPct)||
                (batCharging!=prevBatCharge)||(batPresent!=prevBatPresent);
  if(!needDraw) return;

  prevBatPct=batPercent; prevBatCharge=batCharging;
  prevBatPresent=true;   prevShowLightning=showLightning;

  tft.fillRect(BAT_X,BAT_ROW_VOLT,BAT_W,BAT_ZONE_H,C_BG);

  char voltBuf[8]; snprintf(voltBuf,sizeof(voltBuf),"%.2fV",batVoltage);
  tft.setTextFont(1);tft.setTextSize(1);tft.setTextColor(C(C_TZ),C_BG);
  tft.setCursor(BAT_X,BAT_ROW_VOLT); tft.print(voltBuf);

  if(showLightning){ drawLightningIcon(); return; }

  uint16_t col;
  if     (batPercent>=BAT_WARN_PCT) col=C(C_SAT_OK);
  else if(batPercent>=BAT_LOW_PCT)  col=C(C_DIR);
  else                              col=C(C_NO_FIX);

  int filled=0;
  if     (batPercent>80)  filled=5;
  else if(batPercent>60)  filled=4;
  else if(batPercent>40)  filled=3;
  else if(batPercent>20)  filled=2;
  else if(batPercent>0)   filled=1;

  if(batCharging){
    int animIdx=filled+chargeAnimSeg%(BAT_SEG_N-filled+1);
    if(animIdx>=BAT_SEG_N) animIdx=BAT_SEG_N-1;
    drawBatteryIcon(filled,C(C_SAT_OK),-1,animIdx);
  } else if(batPercent<BAT_CRIT_PCT){
    drawBatteryIcon(batBlinkState?filled:0,col);
  } else {
    drawBatteryIcon(filled,col);
  }
  if(batPercent>=0){
    char pctBuf[6]; snprintf(pctBuf,sizeof(pctBuf),"%d%%",batPercent);
    tft.setTextFont(1);tft.setTextSize(1);tft.setTextColor(C(C_TZ),C_BG);
    tft.setCursor(BAT_X,BAT_ROW_PCT); tft.print(pctBuf);
  }
}

// ════════════════════════════════════════════════════════════════
//  SUNRISE / SUNSET
// ════════════════════════════════════════════════════════════════
static inline float _bl_deg2rad(float d){return d*(float)M_PI/180.0f;}
static inline float _bl_rad2deg(float r){return r*180.0f/(float)M_PI;}

static int _bl_dayOfYear(int day,int month,int year){
  static const int dpm[]={0,31,59,90,120,151,181,212,243,273,304,334};
  int n=dpm[month-1]+day;
  bool leap=(year%4==0&&year%100!=0)||(year%400==0);
  if(leap&&month>2) n++;
  return n;
}

static float _bl_sunEventUTC(float lat,float lng,int day,int month,int year,bool rising){
  int N=_bl_dayOfYear(day,month,year);
  float gamma=_bl_deg2rad(360.0f/365.0f*(N-1+(rising?6.0f:18.0f)/24.0f));
  float eqtMin=229.18f*(0.000075f+0.001868f*cosf(gamma)-0.032077f*sinf(gamma)
               -0.014615f*cosf(2*gamma)-0.04089f*sinf(2*gamma));
  float decl=0.006918f-0.399912f*cosf(gamma)+0.070257f*sinf(gamma)
             -0.006758f*cosf(2*gamma)+0.000907f*sinf(2*gamma)
             -0.002697f*cosf(3*gamma)+0.00148f*sinf(3*gamma);
  float cosHa=(cosf(_bl_deg2rad(90.833f))-sinf(_bl_deg2rad(lat))*sinf(decl))
              /(cosf(_bl_deg2rad(lat))*cosf(decl));
  if(cosHa<-1.0f||cosHa>1.0f) return -1.0f;
  float ha=_bl_rad2deg(acosf(cosHa));
  if(!rising) ha=-ha;
  return 720.0f-4.0f*(lng+ha)-eqtMin;
}

int calcSunriseMin(float lat,float lng,int day,int month,int year,int tzOff){
  float utcMin=_bl_sunEventUTC(lat,lng,day,month,year,true);
  if(utcMin<0) return -1;
  int locMin=(int)roundf(utcMin)+tzOff*60;
  return ((locMin%1440)+1440)%1440;
}
int calcSunsetMin(float lat,float lng,int day,int month,int year,int tzOff){
  float utcMin=_bl_sunEventUTC(lat,lng,day,month,year,false);
  if(utcMin<0) return -1;
  int locMin=(int)roundf(utcMin)+tzOff*60;
  return ((locMin%1440)+1440)%1440;
}

// ════════════════════════════════════════════════════════════════
//  NIGHT MODE
// ════════════════════════════════════════════════════════════════
bool  sunCalcNeeded=true;
int   sunriseMin=-2, sunsetMin=-2;
int   prevBlDay=-1, prevBlMin=-1;

void requestSunCalc(){sunCalcNeeded=true;}

void updateBacklight(int localHour,int localMin,int localDay,
                     float lat,float lng,bool coordValid,
                     int day,int month,int year,int tzOff){
  if(localDay!=prevBlDay&&prevBlDay>=0) sunCalcNeeded=true;
  prevBlDay=localDay;
  if(sunCalcNeeded&&coordValid){
    sunriseMin=calcSunriseMin(lat,lng,day,month,year,tzOff);
    sunsetMin =calcSunsetMin (lat,lng,day,month,year,tzOff);
    sunCalcNeeded=false;
    if(DEBUG_MODE&&sunriseMin>=0)
      Serial.printf("[BL] Sunrise %02d:%02d Sunset %02d:%02d\n",
                    sunriseMin/60,sunriseMin%60,sunsetMin/60,sunsetMin%60);
  }
  int curMin=localHour*60+localMin;
  if(curMin==prevBlMin) return;
  prevBlMin=curMin;
  bool isNight;
  if(sunriseMin>=0&&sunsetMin>=0) isNight=(curMin<sunriseMin)||(curMin>=sunsetMin);
  else if(sunriseMin<0&&sunsetMin<0){
    isNight=(curMin>=BL_FALLBACK_NIGHT_START*60)||(curMin<BL_FALLBACK_NIGHT_END*60);
  } else isNight=false;

  if(isNight!=nightModeActive){
    nightModeActive=isNight;
    if (!screen2Active) {
      drawUI();
      prevSpeed=-1; prevSat=-1; prevSbas2=-2; prevHour=-1;
      prevTimeBuf[0]='\0'; prevDateBuf[0]='\0'; prevTzBuf[0]='\0';
      prevLatBuf[0]='\0';  prevLngBuf[0]='\0';  prevAltBuf[0]='\0';
      prevHdopBuf[0]='\0'; prevVdopBuf[0]='\0';
      prevDirBuf[0]='\0';  prevSatBuf[0]='\0';  prevEgnosBuf[0]='\0';
      prevSatFix=false;
      memset(prevTimeXBuf, 0, sizeof(prevTimeXBuf));
      prevLat=0;prevLng=0;prevAlt=-99999;
      prevBatPct=-99;prevPwrSource=PWR_SRC_UNKNOWN;
      prevShowLightning=!prevShowLightning;
    } else {
      prevScreen2Speed = -1;
      prevScreen2Fix   = false;
    }
    if(DEBUG_MODE) Serial.printf("[BL] %s mode\n",isNight?"Night":"Day");
  }
}

// ════════════════════════════════════════════════════════════════
//  SCREEN 1 — static frame
// ════════════════════════════════════════════════════════════════
void drawUI(){
  tft.fillScreen(C_BG);
  tft.drawFastHLine(0,DIV_Y,  320,C(C_DIV));
  tft.drawFastHLine(0,DIV_Y+1,320,C(0x2104));
  tft.drawFastHLine(0,DIV_Y+2,320,C_BG);
  tft.setTextFont(4); tft.setTextSize(1);
  tft.setTextColor(C(C_UNIT), C_BG);
  tft.setCursor(SPD_RIGHT_X+4, KMPH_Y);
  tft.print(LOCALE_METRIC ? "km/h" : "mph");
}

// ════════════════════════════════════════════════════════════════
//  SCREEN 2 — big speed digits only, right-aligned (x=315)
// ════════════════════════════════════════════════════════════════
void drawScreen2(int speed, bool hasFixGrace, bool forceRedraw) {
  float spdVal = LOCALE_METRIC ? (float)speed : (float)speed * 0.621371f;
  int   spdInt = (int)spdVal;

  bool fixChanged = (hasFixGrace != prevScreen2Fix);
  if (!forceRedraw && spdInt == prevScreen2Speed && !fixChanged) return;

  prevScreen2Speed = spdInt;
  prevScreen2Fix   = hasFixGrace;

  tft.setTextFont(7); tft.setTextSize(3);
  int fontH = tft.fontHeight();

  char buf[6];
  if (!gpsAvailable) snprintf(buf, sizeof(buf), "-");
  else               snprintf(buf, sizeof(buf), "%d", spdInt);
  int numW = tft.textWidth(buf);

  int spdY = (170 - fontH) / 2;
  int numX = 320 - numW;   // right edge pinned to x=315

  uint16_t col = hasFixGrace ? C(C_SPEED) : C(C_SAT_WARN);
  if (!gpsAvailable) col = C(C_NO_FIX);

  tft.fillScreen(C_BG);

  tft.setTextColor(col, C_BG);
  tft.setCursor(numX, spdY);
  tft.print(buf);
}

// ════════════════════════════════════════════════════════════════
//  HELPER: char-by-char string diff rendering
//
//  Compares newStr with oldBuf. For each position:
//    - if changed (or forceRedraw=true): erase via fillRect, draw new char.
//    - if unchanged and forceRedraw=false: leave pixels alone.
//  Use forceRedraw=true when color changes (fix↔no-fix).
//  oldBuf is updated at end. Cursor set to (x, y).
//  charW = width of one character in pixels for current font.
// ════════════════════════════════════════════════════════════════
static void drawStringDiff(int x, int y, int charW,
                           const char* newStr, char* oldBuf,
                           uint16_t col, bool forceRedraw = false) {
  int newLen = strlen(newStr);
  int oldLen = strlen(oldBuf);
  int maxLen = newLen > oldLen ? newLen : oldLen;
  int fontH  = tft.fontHeight();

  for (int i = 0; i < maxLen; i++) {
    char nc = (i < newLen) ? newStr[i] : 0;
    char oc = (i < oldLen) ? oldBuf[i] : 0;
    if (nc == oc && !forceRedraw) continue;
    int cx = x + i * charW;
    if (oc) tft.fillRect(cx, y, charW, fontH, C_BG);
    if (nc) {
      tft.setTextColor(col, C_BG);
      tft.setCursor(cx, y);
      tft.print(nc);
    }
  }
  strncpy(oldBuf, newStr, newLen + 1);
}

// ════════════════════════════════════════════════════════════════
//  SPEED DISPLAY (screen 1)
// ════════════════════════════════════════════════════════════════
void drawSpeedArea(int speed, int oldSpeed, bool hasFixGrace, bool visible,
                   bool forceRedraw = false) {
  if(initError){
    tft.fillRect(0,SPD_ZONE_Y,SPD_RIGHT_X,SPD_ZONE_H,C_BG);
    tft.setTextFont(4);tft.setTextSize(3);tft.setTextColor(C(C_NO_FIX),C_BG);
    int ew=tft.textWidth("E");
    tft.setCursor(SPD_RIGHT_X-ew,SPD_Y+20);tft.print("E");
    tft.setTextFont(4);tft.setTextSize(1);tft.setTextColor(C(C_UNIT),C_BG);
    tft.setCursor(SPD_RIGHT_X+4,KMPH_Y);
    tft.print(LOCALE_METRIC ? "km/h" : "mph");
    return;
  }

  tft.setTextFont(7); tft.setTextSize(2);
  int charW = tft.textWidth("0");
  int fontH = tft.fontHeight();

  if (!gpsAvailable) {
    tft.fillRect(0, SPD_Y, SPD_RIGHT_X, fontH, C_BG);
    tft.setTextColor(C(C_NO_FIX), C_BG);
    int cx = SPD_RIGHT_X - charW;
    if (cx < 2) cx = 2;
    tft.setCursor(cx, SPD_Y);
    tft.print("-");
    return;
  }

  // Convert speed to display units
  float spdVal = LOCALE_METRIC ? (float)(speed > 0 ? speed : 0)
                               : (float)(speed > 0 ? speed : 0) * 0.621371f;
  float oldVal = LOCALE_METRIC ? (float)(oldSpeed > 0 ? oldSpeed : 0)
                               : (float)(oldSpeed > 0 ? oldSpeed : 0) * 0.621371f;

  char newBuf[5], oldBuf[5];
  snprintf(newBuf, sizeof(newBuf), "%d", (int)spdVal);
  if (oldSpeed >= 0)
    snprintf(oldBuf, sizeof(oldBuf), "%d", (int)oldVal);
  else
    oldBuf[0] = '\0';

  int newLen = (int)strlen(newBuf);
  int oldLen = (int)strlen(oldBuf);

  uint16_t col = visible
    ? (hasFixGrace ? C(C_SPEED) : C(C_SAT_WARN))
    : C_BG;

  int maxLen = newLen > oldLen ? newLen : oldLen;
  for (int i = 0; i < maxLen; i++) {
    char nc = (i < newLen) ? newBuf[newLen - 1 - i] : 0;
    char oc = (i < oldLen) ? oldBuf[oldLen - 1 - i] : 0;
    int cx = SPD_RIGHT_X - (i + 1) * charW;
    if (cx < 2) cx = 2;
    if (nc == oc && !forceRedraw) continue;
    if (oc) tft.fillRect(cx, SPD_Y, charW, fontH, C_BG);
    if (nc) {
      tft.setTextColor(col, C_BG);
      tft.setCursor(cx, SPD_Y);
      tft.print(nc);
    }
  }
}

void updateSpeed(int speed, bool hasFixGrace) {
  bool fixChanged    = (hasFixGrace  != prevFixGrace);
  bool availChanged  = (gpsAvailable != prevGpsAvailable);

  if (speed != prevSpeed || fixChanged || availChanged) {
    int oldSpd = prevSpeed;
    prevSpeed        = speed;
    prevFixGrace     = hasFixGrace;
    prevGpsAvailable = gpsAvailable;

    drawSpeedArea(speed, oldSpd, hasFixGrace, true, fixChanged || availChanged);

    if (fixChanged || availChanged) {
      tft.setTextFont(4); tft.setTextSize(1);
      tft.setTextColor(C(C_UNIT), C_BG);
      tft.setCursor(SPD_RIGHT_X + 4, KMPH_Y);
      tft.print(LOCALE_METRIC ? "km/h" : "mph");
    }
    return;
  }
}

// ════════════════════════════════════════════════════════════════
//  TIME + TZ + DATE
// ════════════════════════════════════════════════════════════════

void updateTime(bool hasFix, int utcH, int utcM, int utcDay, int utcMon, int utcYr) {
  if (utcH==prevHour && utcM==prevMinute && hasFix==prevFixTime) return;
  prevHour=utcH; prevMinute=utcM; prevFixTime=hasFix;

  tft.fillRect(0, INF_Y, INF_SPLIT_X, INF_H, C_BG);

  if (hasFix && utcH >= 0) {
    int lD, lM, lY;
    int lH = applyTimezone(utcH, utcDay, utcMon, utcYr, lD, lM, lY);
    char tzStr[16];
    tzDisplayStr(tzStr, sizeof(tzStr));

    tft.setTextFont(6); tft.setTextSize(1);
    tft.setTextColor(C(C_TIME), C_BG);

    int dispH = lH;
    bool isPM = false;
    if (!LOCALE_USE_24H) {
      isPM  = (lH >= 12);
      dispH = lH % 12;
      if (dispH == 0) dispH = 12;
    }

    char hbuf[3], mbuf[3];
    snprintf(hbuf, sizeof(hbuf), "%d",  dispH);
    snprintf(mbuf, sizeof(mbuf), "%02d", utcM);

    int wDigit = tft.textWidth("0");
    int wColon = tft.textWidth(":");
    int xColon = 2 + wDigit * 2;

    int wH = tft.textWidth(hbuf);
    tft.setCursor(xColon - wH, INF_Y+1);
    tft.print(hbuf);

    tft.setCursor(xColon, INF_Y+1);
    tft.print(":");

    tft.setCursor(xColon + wColon, INF_Y+1);
    tft.print(mbuf);

    // AM/PM indicator: Font1 size=2, positioned right of minutes
    if (!LOCALE_USE_24H) {
      tft.setTextFont(1); tft.setTextSize(2);
      tft.setTextColor(C(C_TZ), C_BG);
      int ampmX = xColon + wColon + wDigit * 2 + 4;
      tft.setCursor(ampmX, INF_Y + 1);
      tft.print(isPM ? "PM" : "AM");
    }

    char tbuf[6]; snprintf(tbuf, sizeof(tbuf), "%d:%02d", dispH, utcM);
    strncpy(prevTimeBuf, tbuf, sizeof(prevTimeBuf)-1);
    memset(prevTimeXBuf, 0, sizeof(prevTimeXBuf));

    char dbuf[11]; snprintf(dbuf, sizeof(dbuf), "%02d.%02d.%04d", lD, lM, lY);
    tft.setTextFont(1); tft.setTextSize(1);
    tft.setTextColor(C(C_TZ), C_BG);
    tft.setCursor(2, INF_Y+49);
    tft.print(dbuf);
    strncpy(prevDateBuf, dbuf, sizeof(prevDateBuf)-1);

    int newW = strlen(tzStr) * 6;
    tft.setTextColor(C(C_TZ), C_BG);
    tft.setCursor(INF_SPLIT_X - 2 - newW, INF_Y+49);
    tft.print(tzStr);
    strncpy(prevTzBuf, tzStr, sizeof(prevTzBuf)-1);

  } else {
    tft.setTextFont(6); tft.setTextSize(1);
    tft.setTextColor(C(C_TZ), C_BG);

    int wDigit = tft.textWidth("0");
    int wColon = tft.textWidth(":");
    int wDash  = tft.textWidth("-");
    int xColon = 2 + wDigit * 2;

    tft.setCursor(xColon - wDash * 2, INF_Y+1);
    tft.print("--");

    tft.setCursor(xColon, INF_Y+1);
    tft.print(":");

    tft.setCursor(xColon + wColon, INF_Y+1);
    tft.print("--");

    strncpy(prevTimeBuf, "--:--", sizeof(prevTimeBuf)-1);
    memset(prevTimeXBuf, 0, sizeof(prevTimeXBuf));
    prevDateBuf[0] = '\0';
    prevTzBuf[0]   = '\0';
  }
}

// ════════════════════════════════════════════════════════════════
//  COORDINATES + ALTITUDE + DIRECTION + HDOP + VDOP
//
//  Right info block layout (Font1, 6px per char, y positions):
//    INF_Y+1  — latitude
//    INF_Y+13 — longitude
//    INF_Y+25 — altitude
//    INF_Y+37 — HDOP
//    INF_Y+49 — VDOP  (same row as date/TZ on left)
//
//  Format (10 chars, left-padded with spaces to fill fixed width):
//    Lat/Lng: "___---.----N"  (12 chars: 3+1+5+H)
//    Alt:     "Alt:__-----m"  (12 chars, metric) / "Alt:_-----ft" (american)
//    HDOP:    "HDOP:_--.-__"  (12 chars)
//    VDOP:    "VDOP:_--.-__"  (12 chars)
// ════════════════════════════════════════════════════════════════
// Screen layout right block:
//   COORD_X_START   — left edge of right info block
//   COORD_DOT_X     — anchor X for Alt/HDOP/VDOP label alignment
//   COORD_LAT_DOT_X — decimal-point X for lat/lng (2 chars right of COORD_DOT_X)
//   All values shifted 4 chars (24px) right vs previous revision.
#define COORD_X_START   142   // direction label left edge
#define COORD_DOT_X     172   // Alt/HDOP/VDOP draw origin anchor
#define COORD_LAT_DOT_X (COORD_DOT_X + 2*6)  // lat/lng decimal point (2 chars right)

// Draws one coordinate line char-by-char.
// Buffer format: "DDD.DDDDD H" (11 chars, Font1 6px)
static void updateCoordLine(bool hasFix, float val, char hPos, char hNeg,
                            int y, char* buf) {
  char newBuf[12];
  if (hasFix) {
    float absV = fabsf(val);
    int deg  = (int)absV;
    int frac = (int)roundf((absV - deg) * 100000.0f);
    snprintf(newBuf, sizeof(newBuf), "%3d.%05d%c", deg, frac, val>=0?hPos:hNeg);
  } else {
    snprintf(newBuf, sizeof(newBuf), "---.-----%c", hPos);
  }

  tft.setTextFont(1); tft.setTextSize(1);

  // Lat/lng lines use COORD_LAT_DOT_X (2 chars right of generic block start)
  int startX = COORD_LAT_DOT_X - 3*6;

  int newLen = strlen(newBuf);
  int oldLen = strlen(buf);
  int maxLen = newLen > oldLen ? newLen : oldLen;

  for (int i = 0; i < maxLen; i++) {
    char nc = (i < newLen) ? newBuf[i] : ' ';
    char oc = (i < oldLen) ? buf[i]    : ' ';
    if (nc == oc) continue;
    int cx = startX + i * 6;
    uint16_t col = (i < newLen-1) ? C(C_COORD) : C(C_ALT);
    tft.fillRect(cx, y, 6, tft.fontHeight(), C_BG);
    if (nc != ' ') { tft.setTextColor(col, C_BG); tft.setCursor(cx, y); tft.print(nc); }
  }
  strncpy(buf, newBuf, 12);
}

// Formats HDOP/VDOP value as "XX.X" with mandatory tenths digit.
// Returns "HDOP: XX.X  " or placeholder "HDOP: --.-  ".
// Output is always exactly 12 chars (COORD_X_START..319 space, Font1 6px = ~32 chars available).
// We use 12 chars: "HDOP: XX.X  " — label(5) + space(1) + value(4) + 2 trailing spaces.
static void formatDopLine(const char* label, bool valid, float val, char* outBuf, int outLen) {
  if (valid) {
    // Format value as "XX.X" (always show one decimal, even if .0)
    int intPart  = (int)val;
    int fracPart = (int)roundf((val - intPart) * 10.0f);
    if (fracPart >= 10) { intPart++; fracPart = 0; }
    snprintf(outBuf, outLen, "%s %2d.%1d  ", label, intPart, fracPart);
  } else {
    snprintf(outBuf, outLen, "%s --.-  ", label);
  }
}

void updateCoords(bool hasFix, float lat, float lng, float alt,
                  float course, float spdKmph,
                  float hdop, float vdop, bool hdopValid, bool vdopValid) {
  bool coordChanged = (lat!=prevLat || lng!=prevLng || alt!=prevAlt || hasFix!=prevFix);
  bool dirChanged   = (fabsf(course-prevCourse) > 0.5f);
  bool visChanged   = false;

  bool newCourseVisible = courseVisible;
  if (hasFix && gps.course.isValid()) {
    if (spdKmph > 3.0f) newCourseVisible = true;
    if (spdKmph < 2.0f) newCourseVisible = false;
  } else {
    newCourseVisible = false;
  }
  if (newCourseVisible != courseVisible) {
    visChanged   = true;
    courseVisible = newCourseVisible;
  }

  if (!coordChanged && !dirChanged && !visChanged) return;

  if (coordChanged) { prevLat=lat; prevLng=lng; prevAlt=alt; }
  if (dirChanged)   { prevCourse=course; }

  tft.setTextFont(1); tft.setTextSize(1);

  if (coordChanged) {
    // Latitude  — row 0
    updateCoordLine(hasFix, lat, 'N', 'S', INF_Y+1,  prevLatBuf);
    // Longitude — row 1
    updateCoordLine(hasFix, lng, 'E', 'W', INF_Y+13, prevLngBuf);

    // Altitude "Alt:  XXXXm" or "Alt: XXXXft" — row 2
    // Fixed layout: label "Alt:" (4) + space(s) + value right-aligned + unit
    // Total field width = 12 chars × 6px from COORD_DOT_X-3*6
    // Metric:   "Alt:__-----m"
    // American: "Alt:_-----ft"
    char newAlt[12];
    if (hasFix) {
      float altVal = LOCALE_METRIC ? alt : alt * 3.28084f;
      int   altInt = (int)roundf(altVal);
      if (LOCALE_METRIC)
        snprintf(newAlt, sizeof(newAlt), "Alt:%6dm", altInt);
      else
        snprintf(newAlt, sizeof(newAlt), "Alt:%5dft", altInt);
    } else {
      if (LOCALE_METRIC)
        snprintf(newAlt, sizeof(newAlt), "Alt:  -----m");
      else
        snprintf(newAlt, sizeof(newAlt), "Alt: -----ft");
    }
    // Align "Alt:" label so 't' (index 3) sits at COORD_DOT_X - 2*6 (same as before)
    drawStringDiff(COORD_DOT_X - 2*6, INF_Y+25, 6, newAlt, prevAltBuf, C(C_ALT));

    // HDOP — row 3
    char newHdop[12];
    formatDopLine("HDOP:", hdopValid, hdop, newHdop, sizeof(newHdop));
    drawStringDiff(COORD_DOT_X - 2*6, INF_Y+37, 6, newHdop, prevHdopBuf, C(C_COORD));

    // VDOP — row 4 (same Y as date/TZ on left side)
    char newVdop[12];
    formatDopLine("VDOP:", vdopValid, vdop, newVdop, sizeof(newVdop));
    drawStringDiff(COORD_DOT_X - 2*6, INF_Y+49, 6, newVdop, prevVdopBuf, C(C_COORD));
  }

  // Direction — Font4 size=2, placed 8px right of the coord block right edge.
  // Coord block right edge = COORD_LAT_DOT_X - 3*6 + 11*6 = COORD_LAT_DOT_X + 48.
  // Direction start X = coord right edge + 3px gap.
  #define DIR_X (COORD_LAT_DOT_X + 48 + 3)
  if (dirChanged || visChanged) {
    const char* newDir = (courseVisible && hasFix) ? courseToDir(course) : "";

    if (strcmp(newDir, prevDirBuf) != 0) {
      if (prevDirBuf[0]) {
        tft.setTextFont(4); tft.setTextSize(2);
        int dw = tft.textWidth(prevDirBuf);
        tft.fillRect(DIR_X, INF_Y+1, dw, tft.fontHeight(), C_BG);
      }
      if (newDir[0]) {
        tft.setTextFont(4); tft.setTextSize(2);
        tft.setTextColor(C(C_DIR), C_BG);
        tft.setCursor(DIR_X, INF_Y+1);
        tft.print(newDir);
      }
      strncpy(prevDirBuf, newDir, sizeof(prevDirBuf)-1);
    }
  }
}

// ════════════════════════════════════════════════════════════════
//  INFO PANEL — wrapper
// ════════════════════════════════════════════════════════════════
void updateStatus(bool hasFix, bool hasFixGrace, int sats,
                  float lat, float lng, float alt, float course, float spdKmph,
                  int utcH, int utcM, int utcDay, int utcMon, int utcYr,
                  float hdop, float vdop, bool hdopValid, bool vdopValid) {

  // Satellite count "Sat:used/visible"
  char newSat[16] = "";
  snprintf(newSat, sizeof(newSat), "Sat:%d/%d", sats, gpsSatsVisible);

  bool satChanged    = (strcmp(newSat, prevSatBuf) != 0);
  bool fixColChanged = (hasFix != prevSatFix);

  if (satChanged || fixColChanged) {
    tft.setTextFont(1); tft.setTextSize(1);
    uint16_t col = hasFix ? C(C_SAT_OK) : C(C_SAT_WARN);
    drawStringDiff(249, 0, 6, newSat, prevSatBuf, col, fixColChanged);
    prevSatFix = hasFix;
  }

  // SBAS "SBAS:used/visible"
  char newEgnos[16] = "";
  snprintf(newEgnos, sizeof(newEgnos), "SBAS:%d/%d", sbasSatsUsed, sbasSatsVisible);

  bool egnosChanged = (strcmp(newEgnos, prevEgnosBuf) != 0);
  if (egnosChanged) {
    tft.setTextFont(1); tft.setTextSize(1);
    drawStringDiff(249, 10, 6, newEgnos, prevEgnosBuf, C(C_SAT_OK));
  }

  prevSat   = sats;
  prevSbas2 = sbasSatsVisible;

  updateTime(hasFixGrace, utcH, utcM, utcDay, utcMon, utcYr);
  updateCoords(hasFix, lat, lng, alt, course, spdKmph, hdop, vdop, hdopValid, vdopValid);
  prevFix      = hasFix;
  prevFixGrace = hasFixGrace;
}

// ════════════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════════════
void setup(){
  Serial.begin(115200);
  loadAdcCal();

  esp_sleep_wakeup_cause_t wakeup=esp_sleep_get_wakeup_cause();
  if(wakeup!=ESP_SLEEP_WAKEUP_UNDEFINED){
    requestSunCalc();
    if(DEBUG_MODE) Serial.printf("[POWER] Wakeup: %d\n",(int)wakeup);
  }

  country=findCountryProfile(COUNTRY_STR);
  if(DEBUG_MODE) Serial.printf("Country: %s (home %.4f,%.4f)\n",
                               country->code,country->homeLat,country->homeLng);

  pinMode(BTN_SLEEP_PIN, INPUT_PULLUP);
  pinMode(BTN_WAKE_PIN,  INPUT_PULLUP);

  pinMode(DISP_PWR_PIN,OUTPUT);
  digitalWrite(DISP_PWR_PIN,HIGH);
  pinMode(DISP_BL_PIN,OUTPUT);
  digitalWrite(DISP_BL_PIN,HIGH);

  analogReadResolution(12);

  tft.init();
  // DISPLAY_ROTATION: 0=normal, 2=180°; TFT_eSPI landscape = rotation 1 or 3
  tft.setRotation(DISPLAY_ROTATION == 0 ? 1 : 3);
  tft.fillScreen(C_BG);
  drawUI();
  updateSpeed(0,false);
  updateStatus(false,false,0,0,0,0,0,0,-1,-1,1,1,2024,0,0,false,false);

  readBattery();
  updateBatteryDisplay(true);

  if(DEBUG_MODE) Serial.println("=== GPS Speedometer (T-Display-S3 + NEO-M10) ===");

  gpsSerial.begin(GPS_BAUD_INIT, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  if(!gpsSerial){
    initError=true;
    if(DEBUG_MODE) Serial.println("ERROR: GPS Serial init failed!");
    drawSpeedArea(0, 0, false, true);
    return;
  }
  delay(200);

  GpsCache cache=loadGpsCache();
  if(cache.valid){
    int32_t latE7=(int32_t)(cache.lat*1e7f);
    int32_t lngE7=(int32_t)(cache.lng*1e7f);
    int32_t altCm=(int32_t)(cache.alt*100.0f);
    ubxMgaIniPosLlh(gpsSerial,latE7,lngE7,altCm);
    delay(50);
    uint32_t ts=cache.unixTs;
    uint32_t days=ts/86400; uint32_t secs=ts%86400;
    int hr=(int)(secs/3600); int mi=(int)((secs%3600)/60); int sc=(int)(secs%60);
    uint32_t z=days+719468; uint32_t era=(z>=0?z:z-146096)/146097;
    uint32_t doe=z-era*146097;
    uint32_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    uint32_t y=yoe+era*400;
    uint32_t doy=doe-365*yoe-yoe/4+yoe/100;
    uint32_t mp=(5*doy+2)/153; uint32_t d=doy-((153*mp+2)/5)+1;
    uint32_t m=mp<10?mp+3:mp-9; y+=m<=2;
    ubxMgaIniTimeUtc(gpsSerial,(uint16_t)y,(uint8_t)m,(uint8_t)d,hr,mi,sc);
    delay(50);
    if(DEBUG_MODE) Serial.printf("Warm start MGA: %.5f, %.5f\n",cache.lat,cache.lng);
  } else {
#if HOME_HINT_ENABLED
    int32_t latE7=(int32_t)(country->homeLat*1e7f);
    int32_t lngE7=(int32_t)(country->homeLng*1e7f);
    int32_t altCm=(int32_t)(country->homeAlt*100.0f);
    ubxMgaIniPosLlh(gpsSerial,latE7,lngE7,altCm);
    delay(50);
    if(DEBUG_MODE) Serial.printf("Cold hint: %s %.4f,%.4f\n",
                                 country->code,country->homeLat,country->homeLng);
#endif
  }

  ubxSetBaud(gpsSerial,GPS_BAUD_TARGET);
  delay(150);
  gpsSerial.end();
  gpsSerial.begin(GPS_BAUD_TARGET,SERIAL_8N1,GPS_RX_PIN,GPS_TX_PIN);
  delay(100);

  ubxSetRate(gpsSerial,GPS_UPDATE_HZ);
  delay(50);
  ubxSetNav5Automotive(gpsSerial);
  delay(50);
  ubxConfigNmeaMessages(gpsSerial);
  delay(100);
  ubxEnableSBAS(gpsSerial);
  delay(50);

  if(DEBUG_MODE){
    Serial.printf("GPS baud  : %d\n",GPS_BAUD_TARGET);
    Serial.printf("GPS rate  : %d Hz\n",GPS_UPDATE_HZ);
    Serial.printf("Config    : VALSET (Gen10 API)\n");
    Serial.println("Waiting for fix...");
  }
  updateTimezone(0.0f, 0.0f, 2024, 1, 1, 0, 0);
}

// ════════════════════════════════════════════════════════════════
//  GPS RE-INITIALISATION
// ════════════════════════════════════════════════════════════════
static void gpsReinit() {
  if (DEBUG_MODE) Serial.println("[GPS] Reinitialising...");

  uint32_t foundBaud = 0;
  for (int attempt = 0; attempt < 2 && foundBaud == 0; attempt++) {
    const uint32_t bauds[] = { GPS_BAUD_TARGET, GPS_BAUD_INIT };
    for (int b = 0; b < 2 && foundBaud == 0; b++) {
      gpsSerial.end(); delay(30);
      gpsSerial.begin(bauds[b], SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN); delay(100);
      while (gpsSerial.available()) gpsSerial.read();
      unsigned long t = millis();
      while (millis() - t < 400) {
        if (gpsSerial.available() && gpsSerial.read() == '$') {
          foundBaud = bauds[b];
          while (gpsSerial.available()) gpsSerial.read();
          break;
        }
      }
    }
  }

  if (foundBaud == 0) {
    if (DEBUG_MODE) Serial.println("[GPS] No response — will retry later");
    gpsSerial.end(); delay(30);
    gpsSerial.begin(GPS_BAUD_TARGET, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN); delay(50);
    while (gpsSerial.available()) gpsSerial.read();
    gpsLastByteMs = 0; gpsLastNmeaMs = 0;
    gpsReinitDeadlineMs = millis() + 200;
    return;
  }

  if (DEBUG_MODE) Serial.printf("[GPS] Found at %u baud\n", foundBaud);

  if (foundBaud == GPS_BAUD_INIT) {
    ubxSetBaud(gpsSerial, GPS_BAUD_TARGET);
    gpsSerial.flush(); delay(150);
    gpsSerial.end(); delay(30);
    gpsSerial.begin(GPS_BAUD_TARGET, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN); delay(100);
    while (gpsSerial.available()) gpsSerial.read();
    if (DEBUG_MODE) Serial.printf("[GPS] Switched %u -> %u\n", GPS_BAUD_INIT, GPS_BAUD_TARGET);
  }

  GpsCache cache = loadGpsCache();
  if (cache.valid) {
    int32_t latE7 = (int32_t)(cache.lat * 1e7f);
    int32_t lngE7 = (int32_t)(cache.lng * 1e7f);
    int32_t altCm = (int32_t)(cache.alt * 100.0f);
    ubxMgaIniPosLlh(gpsSerial, latE7, lngE7, altCm); delay(50);
    uint32_t ts = cache.unixTs;
    uint32_t days = ts/86400; uint32_t secs = ts%86400;
    int hr=(int)(secs/3600); int mi=(int)((secs%3600)/60); int sc=(int)(secs%60);
    uint32_t z=days+719468; uint32_t era=(z>=0?z:z-146096)/146097;
    uint32_t doe=z-era*146097;
    uint32_t yoe=(doe-doe/1460+doe/36524-doe/146096)/365;
    uint32_t y=yoe+era*400;
    uint32_t doy=doe-365*yoe-yoe/4+yoe/100;
    uint32_t mp=(5*doy+2)/153; uint32_t d=doy-((153*mp+2)/5)+1;
    uint32_t m=mp<10?mp+3:mp-9; y+=m<=2;
    ubxMgaIniTimeUtc(gpsSerial,(uint16_t)y,(uint8_t)m,(uint8_t)d,hr,mi,sc); delay(50);
    if (DEBUG_MODE) Serial.printf("[GPS] Warm start: %.5f, %.5f\n", cache.lat, cache.lng);
  }

  ubxSetRate(gpsSerial, GPS_UPDATE_HZ);      delay(50);
  ubxSetNav5Automotive(gpsSerial);           delay(50);
  ubxConfigNmeaMessages(gpsSerial);          delay(100);
  ubxEnableSBAS(gpsSerial);                  delay(50);

  gps = TinyGPSPlus();
  spdIdx = 0; memset(spdHistory, 0, sizeof(spdHistory));
  fixGracePrev = false; fixLostMs = 0; tzFirstRun = true;

  gpsLastByteMs = 0; gpsLastNmeaMs = 0;
  while (gpsSerial.available()) gpsSerial.read();
  gpsReinitDeadlineMs = millis() + 500;
  if (DEBUG_MODE) Serial.println("[GPS] Reinit complete — waiting for NMEA...");
}

// ════════════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════════════
void loop(){
  if (DEBUG_MODE && Serial.available()) {
    static char serialBuf[64];
    static uint8_t serialPos = 0;
    while (Serial.available()) {
      char sc = Serial.read();
      if (sc == '\n' || sc == '\r') {
        if (serialPos > 0) {
          serialBuf[serialPos] = '\0';
          tryParseAdcCalCmd(serialBuf);
          serialPos = 0;
        }
      } else if (serialPos < sizeof(serialBuf) - 1) {
        serialBuf[serialPos++] = sc;
      }
    }
  }

  while(gpsSerial.available()){
    char c=gpsSerial.read();
    if(millis() >= gpsReinitDeadlineMs) {
      gpsLastByteMs = millis();
      if(c == '$') gpsLastNmeaMs = millis();
    }

    if(DEBUG_MODE && gpsAvailable){
      if(gpsDumpCount < 64){
        if(gpsDumpCount == 0) Serial.print("[GPS] Raw bytes: ");
        Serial.printf("%02X ", (uint8_t)c);
        gpsDumpCount++;
        if(gpsDumpCount == 64) Serial.println();
      }
    }

    gps.encode(c);
    if(c=='$') nmeaPos=0;
    if(nmeaPos<NMEA_BUF-1) nmeaBuf[nmeaPos++]=c;
    if(c=='\n'){
      nmeaBuf[nmeaPos]='\0';
      int len=nmeaPos;
      while(len>0&&(nmeaBuf[len-1]=='\n'||nmeaBuf[len-1]=='\r')) nmeaBuf[--len]='\0';
      if(len>6&&nmeaBuf[0]=='$'&&DEBUG_MODE){
        if(strncmp(nmeaBuf+3,"RMC",3)==0||strncmp(nmeaBuf+3,"GGA",3)==0)
          Serial.printf("[NMEA] %s\n",nmeaBuf);
        if(strncmp(nmeaBuf+3,"GSV",3)==0)
          Serial.printf("[GSV] %s\n",nmeaBuf);
        if(strncmp(nmeaBuf+3,"GSA",3)==0)
          Serial.printf("[GSA] %s\n",nmeaBuf);
      }
      if(len>6&&nmeaBuf[0]=='$'&&strncmp(nmeaBuf+3,"GSV",3)==0)
        parseGsvForSbas(nmeaBuf);
      if(len>6&&nmeaBuf[0]=='$'&&strncmp(nmeaBuf+3,"GSA",3)==0)
        parseGsaForVdop(nmeaBuf);
      if(tzFirstRun && gps.location.isValid() && gps.date.isValid() && gps.time.isValid()){
        tzFirstRun = false;
        updateTimezone(gps.location.lat(), gps.location.lng(),
                       (uint16_t)gps.date.year(), (uint8_t)gps.date.month(),
                       (uint8_t)gps.date.day(),   (uint8_t)gps.time.hour(),
                       (uint8_t)gps.time.minute());
        prevBlMin = -1;
        {
          int lD2, lM2, lY2;
          int lH2 = applyTimezone((int)gps.time.hour(),
                                  (int)gps.date.day(), (int)gps.date.month(), (int)gps.date.year(),
                                  lD2, lM2, lY2);
          int tzOff2 = tzInitialized ? (int)roundf(tzCurrent.utc_offset_h) : 1;
          updateBacklight(lH2, (int)gps.time.minute(), lD2,
                          gps.location.lat(), gps.location.lng(), true,
                          (int)gps.date.day(), (int)gps.date.month(), (int)gps.date.year(),
                          tzOff2);
        }
      }
      nmeaPos=0;
    }
  }

  checkSleepButton();
  checkWakeButton();
  checkLowBatterySleep();
  checkSbasTimeout();

  {
    unsigned long now0 = millis();
    bool avail = (now0 >= gpsReinitDeadlineMs) &&
                 (gpsLastNmeaMs > 0) &&
                 ((now0 - gpsLastNmeaMs) < GPS_AVAIL_TIMEOUT_MS);

    if (avail != gpsAvailable) {
      gpsAvailable = avail;
      if (DEBUG_MODE)
        Serial.printf("[GPS] Module %s\n", avail ? "available" : "unavailable — no data");
      if (!avail) {
        gpsLastNmeaMs   = 0;
        gpsDumpCount    = 0;
        gpsLastReinitMs = now0;
      }
    }

    if (!gpsAvailable && !avail) {
      bool firstTry  = (gpsLastReinitMs == 0) && (now0 > GPS_AVAIL_TIMEOUT_MS);
      bool retryTime = (gpsLastReinitMs > 0) && ((now0 - gpsLastReinitMs) >= GPS_REINIT_INTERVAL_MS);
      if (firstTry || retryTime) {
        gpsLastReinitMs = now0;
        gpsReinit();
      }
    }
  }

  unsigned long now=millis();
  if(now-lastBatMs>=BAT_UPDATE_MS){lastBatMs=now;readBattery();}
  updateBatteryDisplay();

  bool hdopOk  = gps.hdop.isValid() && gps.hdop.hdop() < FIX_HDOP_MAX;
  bool satsOk  = gps.satellites.isValid() && gps.satellites.value() >= FIX_SATS_MIN;
  bool locValid= gps.location.isValid();
  bool hasFix  = locValid && hdopOk && satsOk;
  int  sats    = gps.satellites.isValid() ? gps.satellites.value() : 0;

  // HDOP / VDOP for display
  bool  hdopValid = gps.hdop.isValid();
  float hdopVal   = hdopValid ? gps.hdop.hdop() : 0.0f;
  bool  vdopValid = gsaVdopValid;
  float vdopVal   = gsaVdopValid ? gsaVdop : 0.0f;

  float filtSpd;
  if(hasFix&&gps.speed.isValid()){
    filtSpd=addSpeedSample(gps.speed.kmph());
  } else {
    filtSpd=(spdIdx>=SPD_AVG_N)
      ?median3(spdHistory[0],spdHistory[1],spdHistory[2])
      :(spdIdx>0?spdHistory[(spdIdx-1)%SPD_AVG_N]:0.0f);
  }
  int spd=(int)filtSpd;

  float lat=0,lng=0,alt=0;
  if(hasFix){
    float rawLat=gps.location.lat(), rawLng=gps.location.lng();
    if(coordOutlierCheck(rawLat,rawLng,filtSpd)){
      lat=rawLat;lng=rawLng;lastAcceptedLat=rawLat;lastAcceptedLng=rawLng;lastAcceptedValid=true;lastAcceptedMs=now;
    } else { lat=lastAcceptedLat;lng=lastAcceptedLng; }
    alt=gps.altitude.isValid()?gps.altitude.meters():0.f;
  }
  float course=gps.course.isValid()?gps.course.deg():0.f;
  int utcH  =gps.time.isValid()?gps.time.hour()  :-1;
  int utcM  =gps.time.isValid()?gps.time.minute():-1;
  int utcDay=gps.date.isValid()?gps.date.day()   : 1;
  int utcMon=gps.date.isValid()?gps.date.month() : 1;
  int utcYr =gps.date.isValid()?gps.date.year()  :2024;

  // Grace period
  if(hasFix){
    if(!fixGracePrev) fixLostMs=0;
    fixGracePrev=true;
    heldSpd=spd; heldUtcH=utcH; heldUtcM=utcM;
    heldUtcDay=utcDay; heldUtcMon=utcMon; heldUtcYr=utcYr;
  } else {
    if(fixGracePrev) fixLostMs=now;
    fixGracePrev=false;
  }
  bool hasFixGrace=hasFix||(fixLostMs>0&&(now-fixLostMs)<FIX_HOLD_MS);

  int dispSpd   =hasFixGrace?(hasFix?spd   :heldSpd)   :0;
  int dispUtcH  =hasFixGrace?(hasFix?utcH  :heldUtcH)  :utcH;
  int dispUtcM  =hasFixGrace?(hasFix?utcM  :heldUtcM)  :utcM;
  int dispUtcDay=hasFixGrace?(hasFix?utcDay:heldUtcDay):utcDay;
  int dispUtcMon=hasFixGrace?(hasFix?utcMon:heldUtcMon):utcMon;
  int dispUtcYr =hasFixGrace?(hasFix?utcYr :heldUtcYr) :utcYr;

  if (screen2Active) {
    // Screen 2: big speed only
    drawScreen2(dispSpd, hasFixGrace, false);
  } else {
    // Screen 1: full info
    updateSpeed(dispSpd, hasFixGrace);
    applyGsvPending();
    applyGsaPending();
    updateStatus(hasFix,hasFixGrace,sats,lat,lng,alt,course,filtSpd,
                 dispUtcH,dispUtcM,dispUtcDay,dispUtcMon,dispUtcYr,
                 hdopVal,vdopVal,hdopValid,vdopValid);
  }

  if(utcH>=0&&utcM>=0){
    int lD,lM,lY;
    int lH=applyTimezone(utcH,utcDay,utcMon,utcYr,lD,lM,lY);
    int tzOff=tzInitialized?(int)roundf(tzCurrent.utc_offset_h):1;
    updateBacklight(lH,utcM,lD,lastAcceptedLat,lastAcceptedLng,lastAcceptedValid,
                    utcDay,utcMon,utcYr,tzOff);
  }

  if(hasFix&&gps.date.isValid()&&gps.time.isValid()&&(now-lastNvsSaveMs>=NVS_SAVE_INTERVAL)){
    lastNvsSaveMs=now;
    uint32_t ts=gpsToUnix(utcDay,utcMon,utcYr,utcH,utcM,gps.time.second());
    saveGpsCache(lat,lng,alt,ts);
    if(DEBUG_MODE) Serial.printf("[NVS] Saved: %.5f, %.5f\n",lat,lng);
    updateTimezone(lat, lng,
                   (uint16_t)utcYr, (uint8_t)utcMon, (uint8_t)utcDay,
                   (uint8_t)utcH, (uint8_t)utcM);
  }

  if(!tzFirstRun && hasFix && gps.date.isValid() && gps.time.isValid()){
    static int tzLastMinute = -1;
    int curTzMinute = utcH * 60 + utcM;
    if(curTzMinute != tzLastMinute){
      tzLastMinute = curTzMinute;
      updateTimezone(lat, lng,
                     (uint16_t)utcYr, (uint8_t)utcMon, (uint8_t)utcDay,
                     (uint8_t)utcH, (uint8_t)utcM);
    }
  }

  if(DEBUG_MODE){
    static unsigned long lastDbgMs=0;
    if(now-lastDbgMs>=1000){
      lastDbgMs=now;
      Serial.printf("Fix:%s Grace:%s HDOP:%.1f Sats:%d/%d SBAS:%d/%d Spd:%dkm/h Scr:%d\n",
                    hasFix?"Y":"N",hasFixGrace?"Y":"N",
                    gps.hdop.isValid()?gps.hdop.hdop():0.f,
                    sats,gpsSatsVisible,sbasSatsUsed,sbasSatsVisible,spd,
                    screen2Active?2:1);
      if(hasFix) Serial.printf("Pos:%.6f,%.6f Alt:%.0fm Crs:%.0f\n",lat,lng,alt,course);
      if(utcH>=0) Serial.printf("UTC:%02d:%02d Bat:%d%%(%.2fV)\n",utcH,utcM,batPercent,batVoltage);
    }
  }

  delay(10);
}