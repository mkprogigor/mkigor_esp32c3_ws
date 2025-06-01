#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include "time.h"
#include <ThingSpeak.h>
#include "Adafruit_VEML7700.h"
#include "mydef.h"
#include <mkigor_bme280.h>

WiFiClient        wifi_client;
bme280            bme;
Adafruit_VEML7700 veml;

#define GD_ENABLE_SLEEP 0 // 1 = enable sleep 15 sec
#define LED 8

char ssid[] = WIFI_SSID;
char pass[] = WIFI_PASS;
unsigned long my_channel_num = 1059748;
const char write_api_key[] = MYTS_WR_APIKEY;
const char read_api_key[]  = MYTS_RD_APIKEY;

static float    gv_bme_p = 0;
static float    gv_bme_t = 0;
static float    gv_bme_h = 0;
static float    gv_lux_a = 0;
static float    gv_vbat  = 0;
#define gk_vbat 5.8

struct_tph      gv_stru_tph;  // var structure for T, P, H
uint64_t        gv_sleep_time;
struct tm       gv_tist;      // time stamp

RTC_DATA_ATTR uint8_t gv_sleep_count = 0;

//=================================================================================================

char gf_byte2char(uint8_t lv_byte1){   // translate 0xBA => 'A'
  uint8_t lv_b1 = lv_byte1 & 0x0F;
  if (lv_b1>9) lv_b1 = lv_b1+55;
  else         lv_b1 = lv_b1+48;
  return lv_b1;
}

void gf_prn_byte(uint8_t lv_byte){   // print byte like "FCh "
  Serial.print(gf_byte2char(lv_byte>>4));
  Serial.print(gf_byte2char(lv_byte));
  Serial.print("h ");
}

void gf_prm_cpu_info(){
  Serial.println("=====================  Start MCU Info  =====================");
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  Serial.print("Chip Model      = "); Serial.println( chip_info.model);
  Serial.print("Cores           = "); Serial.println( chip_info.cores);
  Serial.print("Revision number = "); Serial.println( chip_info.revision);
  Serial.print("Full rev.number = "); Serial.println( chip_info.full_revision);
  Serial.print("Features, BIN   = "); Serial.println( chip_info.features, BIN);
  Serial.print("CPU Freq, MHz   = ");   Serial.println(getCpuFrequencyMhz());
  Serial.print("XTAL Freq,  MHz = ");   Serial.println(getXtalFrequencyMhz());
  Serial.print("APB Freq, Hz    = ");   Serial.println(getApbFrequency());
  Serial.print("esp_get_idf_version()              = ");  Serial.println(esp_get_idf_version());
  Serial.print("esp_get_free_heap_size()           = ");  Serial.println(esp_get_free_heap_size());
  Serial.print("heap_caps_get_free_size()          = ");  Serial.println(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
  Serial.print("heap_caps_get_largest_free_block() = ");  Serial.println(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
  size_t spiram_size = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (spiram_size) {
    Serial.print("PSRAM Size: "); Serial.println(spiram_size);
  }
  else Serial.println("No PSRAM detected.");
  Serial.println("=====================   End MCU Info   =====================\n");
}

boolean gf_wifi_con() {
  if (WiFi.status() == WL_CONNECTED)  {
    Serial.print(WiFi.localIP());
    Serial.print(" => Conected\n\n");
    return true;
  }
  else  {
    WiFi.begin(ssid, pass);
    Serial.print("Connecting to WiFi => ");
    for (u8_t i = 0; i < 16; ++i) {
      if (WiFi.status() != WL_CONNECTED) {
        Serial.print("? ");
        vTaskDelay(1000);
      }
      else {
        Serial.println(WiFi.localIP());
        Serial.println();
        return true;
      }
    }
    Serial.println(" WiFi didn't connect.\n");
    return false;
  }
}

void gf_wifi_scan() {
  Serial.println("Scan WiFi networks =>");
  u8_t n = WiFi.scanNetworks();
  if (n > 0) {
    for (u8_t i = 0; i < n; ++i) {  // Print SSID and RSSI for each network found
      Serial.print(i + 1);
      Serial.print(": SSID=");
      Serial.print(WiFi.SSID(i));
      Serial.print(",\tRSSI=(");
      Serial.print(WiFi.RSSI(i));
      Serial.print("),\tEncr.= " );
      Serial.println(WiFi.encryptionType(i));
    }
  }
  else Serial.println("No networks found.");
}

void gf_wifi_status() {
  Serial.println("===================== WiFi Status Info =====================");
  byte tv_wifist = WiFi.status();
  Serial.print(tv_wifist); Serial.print(" - ");
  switch (tv_wifist)  {
  case WL_CONNECTED:
    Serial.println("WL_CONNECTED");
    break;
    case WL_NO_SHIELD:
    Serial.println("WL_NO_SHIELD");
    break;
    case WL_IDLE_STATUS:
    Serial.println("WL_IDLE_STATUS");
    break;
    case WL_CONNECT_FAILED:
    Serial.println("WL_CONNECT_FAILED");
    break;
    case WL_NO_SSID_AVAIL:
    Serial.println("WL_NO_SSID_AVAIL");
    break;
    case WL_SCAN_COMPLETED:
    Serial.println("WL_SCAN_COMPLETED");
    break;
    case WL_CONNECTION_LOST:
    Serial.println("WL_CONNECTION_LOST");
    break;
    case WL_DISCONNECTED:
    Serial.println("WL_DISCONNECTED");
    break;
    default:
    Serial.println("undefine.");
    break;
  }
  if (tv_wifist == 3)  {
    Serial.print("IP "); Serial.print(WiFi.localIP());
    Serial.print(", MASK "); Serial.print(WiFi.subnetMask());
    Serial.print(", GATE "); Serial.print(WiFi.gatewayIP());
    Serial.print(", DNS  "); Serial.print(WiFi.dnsIP());
    Serial.print(", MAC: ");
    uint8_t mac[6];
    WiFi.macAddress(mac);
    for (uint8_t i = 6; i > 0; i--) {
      Serial.print(mac[i-1],HEX); Serial.print(":");
    }
    Serial.println();
    gf_wifi_scan();
    Serial.println("=================== End WiFi Status Info ===================");
  }
}

float gf_Pa2mmHg(float pressure) {  // convert Pa to mmHg
	return (float)(pressure * 0.00750061683f);
}

void gf_meas_tphl() {
  bme.bme_do1meas();
  delay(200);
  for (uint8_t i = 0; i < 100; i++) {
    if (bme.bme_is_meas()) delay(10);
    else break;
  }

  gv_stru_tph = bme.bme_read_TPH();
  gv_bme_t = ((float)(gv_stru_tph.temp1)) / 100;
  gv_bme_p = gf_Pa2mmHg(((float)(gv_stru_tph.pres1)) / 100);
  gv_bme_h = ((float)(gv_stru_tph.humi1)) / 1000;
  gv_lux_a = veml.readLux(VEML_LUX_AUTO); 

  Serial.print("Temp., *C: ");      Serial.print(gv_bme_t);
  Serial.print(", Humid., %: ");    Serial.print(gv_bme_h);
  Serial.print(", Pres., mmHg: ");  Serial.print(gv_bme_p);
  Serial.print(", Light, lux: ");   Serial.println(gv_lux_a);
}

void gf_send2ts() {
  if (WiFi.status() != WL_CONNECTED) gf_wifi_con(); // Run only one time to switch ON wifi

  if (WiFi.status() == WL_CONNECTED) {
    // Forming STATUS string for ThingSpeak.com
    char lv_rtc_str[18] = "______-cFFrFsFzFF";
    uint8_t lv_hms;
    if (!getLocalTime(&gv_tist)) Serial.print("Failed to obtain time.\n");
    else {
      Serial.print(&gv_tist);   Serial.print(" => time update success.\n");
      lv_hms = gv_tist.tm_hour;
      lv_rtc_str[0] = (char)(lv_hms /10 +48);   lv_rtc_str[1] = (char)(lv_hms % 10 +48);
      lv_hms = gv_tist.tm_min;
      lv_rtc_str[2] = (char)(lv_hms /10 +48);   lv_rtc_str[3] = (char)(lv_hms % 10 +48);
      lv_hms = gv_tist.tm_sec;
      lv_rtc_str[4] = (char)(lv_hms /10 +48);   lv_rtc_str[5] = (char)(lv_hms % 10 +48);
    }
    lv_rtc_str[11] = gf_byte2char(esp_reset_reason());
    lv_rtc_str[13] = gf_byte2char(esp_sleep_get_wakeup_cause());
    lv_rtc_str[15] = gf_byte2char(gv_sleep_count >> 4);   lv_rtc_str[16] = gf_byte2char(gv_sleep_count);
    /*  // Function esp_reset_reason() return RESET reason:
    0 = ESP_RST_UNKNOWN       1 = ESP_RST_POWERON       2 = ESP_RST_EXT       3 = ESP_RST_SW
    4 = ESP_RST_PANIC         5 = ESP_RST_INT_WDT       6 = ESP_RST_TASK_WDT  7 = ESP_RST_WDT
    8 = ESP_RST_DEEPSLEEP     9 = ESP_RST_BROWNOUT      10 = ESP_RST_SDIO
    // Function esp_sleep_get_wakeup_cause() return SLEEP reason:
    0 = ESP_SLEEP_UNKNOWN         1 = ESP_SLEEP_WAKEUP_ALL        2 = ESP_SLEEP_WAKEUP_EXT0
    3 = ESP_SLEEP_WAKEUP_EXT1     4 = ESP_SLEEP_WAKEUP_TIMER      5 = ESP_SLEEP_WAKEUP_TOUCHPAD
    6 = ESP_SLEEP_WAKEUP_ULP      7 = ESP_SLEEP_WAKEUP_GPIO       8 = ESP_SLEEP_WAKEUP_UART
    9 = ESP_SLEEP_WAKEUP_WIFI     10 = ESP_SLEEP_WAKEUP_COCPU     11 = ESP_SLEEP_WAKEUP_COCPU_TRAP_TRIG
    12 = ESP_SLEEP_WAKEUP_BT                */
    Serial.println(lv_rtc_str);
    ThingSpeak.setStatus(lv_rtc_str);
    ThingSpeak.setField(1, gv_bme_t); // set the fields with the values
    ThingSpeak.setField(2, gv_bme_p);
    ThingSpeak.setField(3, gv_bme_h);
    ThingSpeak.setField(4, gv_lux_a);
    ThingSpeak.setField(5, gv_vbat);

    int t_ret_code = ThingSpeak.writeFields(my_channel_num, write_api_key);
    if (t_ret_code == 200) Serial.println("ThingSpeak ch. update successful.");
    else {
      Serial.print("Problem updating channel. HTTP error = "); Serial.println(t_ret_code);
    }
  }
  else   Serial.println(" -> No connection WiFi. Data not send to ThingSpeak.");
}

void gf_veml_disp() {
  Serial.println("------------------------------------");
  Serial.println("Settings used for reading:");
  Serial.print(F("Gain: "));
  switch (veml.getGain()) {
    case VEML7700_GAIN_1: Serial.println("1"); break;
    case VEML7700_GAIN_2: Serial.println("2"); break;
    case VEML7700_GAIN_1_4: Serial.println("1/4"); break;
    case VEML7700_GAIN_1_8: Serial.println("1/8"); break;
  }
  Serial.print(F("Integration Time (ms): "));
  switch (veml.getIntegrationTime()) {
    case VEML7700_IT_25MS: Serial.println("25"); break;
    case VEML7700_IT_50MS: Serial.println("50"); break;
    case VEML7700_IT_100MS: Serial.println("100"); break;
    case VEML7700_IT_200MS: Serial.println("200"); break;
    case VEML7700_IT_400MS: Serial.println("400"); break;
    case VEML7700_IT_800MS: Serial.println("800"); break;
  }

  Serial.print("Gain = "); Serial.println(veml.getGain());
  Serial.print("GainValue = "); Serial.println(veml.getGainValue());
  Serial.print("getIntegrationTime = "); Serial.println(veml.getIntegrationTime());
  Serial.print("getIntegrationTimeValue = "); Serial.println(veml.getIntegrationTimeValue());
  Serial.print("getLowThreshold = "); Serial.println(veml.getLowThreshold());
  Serial.print("getHighThreshold = "); Serial.println(veml.getHighThreshold());
  Serial.print("getPersistence = "); Serial.println(veml.getPersistence());
  Serial.print("getPowerSaveMode = "); Serial.println(veml.getPowerSaveMode());
  Serial.print("interruptStatus() = "); Serial.println(veml.interruptStatus());
  Serial.println();
}

//=================================================================================================
void setup() {
  setCpuFrequencyMhz(80); //  must be First. following frequencies as valid values:

  Serial.begin(115200);
  delay(3000);
  gf_prm_cpu_info();
  Serial.println("=============== Start Setup =================");
  // pinMode(LED, OUTPUT);
  pinMode(A0, INPUT);
  analogSetAttenuation(ADC_11db);
  Wire.begin();

  Serial.print("Check a bme280 => "); // check bme280 and SW reset
  uint8_t k = bme.bme_check();
  if (k == 0) Serial.print("not found, check cables.\n");
  else {
    gf_prn_byte(k);
    Serial.print("chip code.\n");
  }
  bme.begin(FOR_MODE, SB_500MS, FIL_x16, OS_x16, OS_x16, OS_x16);

  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(wifi_client);      // Initialize ThingSpeak
 
  if (!veml.begin()) {
    Serial.println("Sensor VEML7700 not found.");
    veml.setPowerSaveMode(3);
  }
  else Serial.println("Sensor VEML7700 found.");

  configTime(3600, 3600, "pool.ntp.org");   // init time.h win NTP server, +1 GMT & +1 summer time

  Serial.println("===============  End  Setup =================");
}

//=================================================================================================
void loop() {
  digitalWrite(LED, LOW);
  Serial.print("Sleep Count = ");   Serial.println(gv_sleep_count);
  gv_sleep_time = 600000000;
  // gv_sleep_time = 15000000;
  esp_sleep_enable_timer_wakeup(gv_sleep_time);

  gf_wifi_con();
  gf_meas_tphl();
  gv_vbat = (analogRead(A0) * gk_vbat) / 4096;
  Serial.print("Vbat = ");  Serial.println(gv_vbat);
  delay(500);
  gf_send2ts();

  Serial.println("WiFi disconect.");
  WiFi.disconnect();

  digitalWrite(LED, HIGH);
  Serial.println("Go to light sleep mode.");
  delay(500);
  esp_light_sleep_start();
  delay(500);
  Serial.println("\nwakeUp from sleep mode.");
  gv_sleep_count++;
}
//=================================================================================================