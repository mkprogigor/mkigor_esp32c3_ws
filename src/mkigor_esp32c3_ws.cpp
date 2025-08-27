/************************************************************************************
A small project of weathe station on esp32 C3 + BME280 + VEML7700 + BMP280
by Igor Mkprog, mkprogigor@gmail.com

V1.1 from 20.06.2025
************************************************************************************/
#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <ThingSpeak.h>
#include <Adafruit_VEML7700.h>
#include <mkigor_aht20.h>
#include <mkigor_BMx280.h>
#include <mkigor_std.h>

WiFiClient        wifi_client;
cl_BME280         bme1;
cl_BMP280         bmp1;
Adafruit_VEML7700 veml;
cl_AHT20          aht1;

tph_stru     gv_stru_tph;
tp_stru      gv_stru_tp;
aht_stru     gv_aht_th;
const static float    gv_vbat_coef = 5.8;
static float gv_vbat;
static float gv_lux;

struct tm       gv_tist;      // time stamp structure from time.h
uint64_t        gv_sleep_time;
RTC_DATA_ATTR uint8_t gv_sleep_count = 0;

//=================================================================================================

void gf_meas_tphl() {
  bme1.do1Meas();
  delay(200);
  for (uint8_t i = 0; i < 100; i++) {
    if (bme1.isMeas()) delay(10);    else break;
  }
  gv_stru_tph = bme1.read_tph();

  gv_lux = veml.readLux(VEML_LUX_AUTO);

  gv_vbat = (analogRead(A1) * gv_vbat_coef) / 4096;
  
  aht1.do1Meas();      delay(40);
  for (uint8_t i = 0; i < 255; i++) {
    if (aht1.isMeas()) delay(1);    else break;
  }
  gv_aht_th = aht1.read_data();

  bmp1.do1Meas();
  delay(200);
  for (uint8_t i = 0; i < 100; i++) {
    if (bmp1.isMeas()) delay(10);    else break;
  }
  gv_stru_tp = bmp1.read_tp();

  Serial.print("bme T: ");      Serial.print(gv_stru_tph.temp1);
  Serial.print(", bme P: ");    Serial.print(gf_Pa2mmHg(gv_stru_tph.pres1));
  Serial.print(", bme H: "); Serial.print(gv_stru_tph.humi1);
  Serial.print(", lux: ");      Serial.print(gv_lux);
  Serial.print(", Vbat: ");     Serial.print(gv_vbat);
  Serial.print(", ant20 T: ");  Serial.print(gv_aht_th.temp1);
  Serial.print(", ant20 H: ");  Serial.print(gv_aht_th.humi1);
  Serial.print(", bmp T: ");    Serial.print(gv_stru_tp.temp1);
  Serial.print(", bmp H: ");    Serial.println(gf_Pa2mmHg(gv_stru_tp.pres1));
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
    ThingSpeak.setField(1, gv_stru_tph.temp1); // set the fields with the values
    ThingSpeak.setField(2, gf_Pa2mmHg(gv_stru_tph.pres1));
    ThingSpeak.setField(3, gv_stru_tph.humi1);
    ThingSpeak.setField(4, gv_lux);
    ThingSpeak.setField(5, gv_vbat);
    ThingSpeak.setField(6, gv_aht_th.temp1);
    ThingSpeak.setField(7, gv_aht_th.humi1);
    ThingSpeak.setField(8, gf_Pa2mmHg(gv_stru_tp.pres1));

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
  setCpuFrequencyMhz(80); //  must be First

  Serial.begin(115200);
  delay(3000);
  gf_prm_cpu_info();
  Serial.println("=============== Start Setup =================");

  analogSetAttenuation(ADC_11db);
  if (adcAttachPin(A1)) Serial.println("ADC attach to pin A1 success.");
  else Serial.println("ADC NOT attach to pin A1 !");

  Wire.begin();

  uint8_t k;

  Serial.print("Check a bme280 => "); // check bme280 and SW reset
  k = bme1.check(0x76);
  if (k == 0) Serial.print("not found, check cables.\n");
  else {
    gf_prn_byte(k);
    Serial.print("chip code.\n");
  }
  bme1.begin(cd_FOR_MODE, cd_SB_500MS, cd_FIL_x16, cd_OS_x16, cd_OS_x16, cd_OS_x16);

  Serial.print("Check a bmp280 => "); // check bmp280 and SW reset
  k = bmp1.check(0x77);
  if (k == 0) Serial.print("not found, check cables.\n");
  else {
    gf_prn_byte(k);
    Serial.print("chip code.\n");
  }

  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(wifi_client);      // Initialize ThingSpeak
 
  if (!veml.begin()) {
    Serial.println("Sensor VEML7700 not found.");
    veml.setPowerSaveMode(3);
  }
  else Serial.println("Sensor VEML7700 found.");

  configTime(3600, 3600, "pool.ntp.org");   // init time.h win NTP server, +1 GMT & +1 summer time

  gv_sleep_time = 600000000;    //  Light sleep mode time = 600 sec = 10 min
  // gv_sleep_time = 15000000;
  esp_sleep_enable_timer_wakeup(gv_sleep_time);

  aht1.begin();
  delay(20);
  if (aht1.isCalibr()) Serial.println("ANT20 calibrated.");
  else Serial.println("ANT20 not calibrated.");

  Serial.println("===============  End  Setup =================");
}

//=================================================================================================

void loop() {

  Serial.print("Sleep Count = ");   Serial.println(gv_sleep_count);

  gf_wifi_con();
  gf_meas_tphl();

  delay(500);
  gf_send2ts();

  Serial.println("WiFi disconect.");
  WiFi.disconnect();

  Serial.print("Go to light sleep mode for sec = ");  Serial.print(gv_sleep_time/1000000);
  delay(500);
  esp_light_sleep_start();
  delay(10000);
  delay(500);
  Serial.println("\nwakeUp from sleep mode.");
  gv_sleep_count++;
}

//=================================================================================================