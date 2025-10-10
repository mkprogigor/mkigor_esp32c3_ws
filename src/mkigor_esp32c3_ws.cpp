/*
	@brief		A small Arduino project: Easy DIY weather station on esp32-C3, BME280, BME680, VEML7700.
	@author		Igor Mkprog, mkprogigor@gmail.com
	@version	V1.1	@date	10.10.2025

	@remarks	Glossary, abbreviations used in the module.
	@details	Name of functions dont use symbol '_', only small or capital letters.
				Symbol '_' divide name in: prefix _ name _ suffix:
	@var		gv_*    -   Global Variable;
				lv_*    -   Local Variable (live inside statement);
				cl_*    -   CLass;
				cd_*    -   Class Definishion;
				cgv_*   -   Class public (Global) member (Variable);
				clv_*   -   Class private (Local) member (Variable);
				cgf_*   -   Class public (Global) metod (Function), not need, no usefull, becouse we see parenthesis => ();
				clf_*   -   Class private (Local) metod (Function);
				lp_		-	in function, local parameter
				*_stru  -   [or *_stru_t] suffix, as usual, point the type.
*/

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <time.h>
#include <ThingSpeak.h>
#include <Adafruit_VEML7700.h>
#include <mkigor_BMxx80.h>
#include <mkigor_std.h>

//#define enDEBUG   // uncoment it for debug info

WiFiClient        wifi_client;
Adafruit_VEML7700 veml;
cl_BME280         bme2;
cl_BME680         bme6;

tph_stru     gv_stru_tph;
tphg_stru    gv_stru_tphg;
const static float    gv_vbat_coef = 5.8;
static float gv_vbat;
static float gv_lux;

struct tm       gv_tist;      // time stamp structure from time.h
uint64_t        gv_sleep_time;
RTC_DATA_ATTR uint8_t gv_sleep_count = 0;

//=================================================================================================
/*  @brief  Print some status and config registers form BME690 (debug info) */
void lv_dispRegs(void) {
  // 	Status reg 0x1D =
  //		7		|		6		|		5		|	4	|	3	|	2	|	1	|	0	|
  //	new_data_0	| gas_measuring	| 	measuring	| 	x	|	gas_meas_index_0<3:0>		|
  //	Gas_r_lsb reg 0x2B =
  //	7	|	6	|		5		|		4		|	3	|	2	|	1	|	0	|
  //	gas_r<1:0>	| gas_valid_r	| head_stab_r	| 			gas_range_r			|
  //	Ctrl_gas_1 reg 0x71 =
  //	7	|	6	|	5	|	4	|	3	|	2	|	1	|	0	|
  //		|		|		|run_gas|  			nb_conv<3:0>		|
  Serial.print("Status= ");      mkistdf_prnByte(bme6.readReg(0x1D));
  Serial.print("Gas_r_lsb= ");   mkistdf_prnByte(bme6.readReg(0x2B));
  Serial.print("Ctrl_gas_0= ");  mkistdf_prnByte(bme6.readReg(0x70));
  Serial.print("Ctrl_gas_1= ");  mkistdf_prnByte(bme6.readReg(0x71));
  // Serial.print("Ctrl_meas= ");   mkistdf_prnByte(bme6.readReg(0x74));
  Serial.println();
}

/*  @brief  Read data from all sensors in project to global variables   */
void gf_readData() {

  bme2.do1Meas();
  unsigned long lv_measStart = millis(), lv_measDur;
  for (uint8_t i = 0; i < 3000; i++) {
    if (bme2.isMeas()) delay(1);
    else break;
  }
  lv_measDur = millis()-lv_measStart;
  printf("BME280 measuring TPHG time = %d\n", lv_measDur );
  gv_stru_tph = bme2.readTPH();
  printf("BME280 T:%f, P:%f, H:%f\n\n", gv_stru_tph.temp1, gv_stru_tph.pres1, gv_stru_tph.humi1);

  bme6.initGasPointX(0, 350, 100, (int16_t)gv_stru_tph.temp1);
  bme6.do1Meas();
  lv_measStart = millis();
  for (uint8_t i = 0; i < 3000; i++) {
    if (bme6.isMeas()) delay(1);
    else break;
  }
  lv_measDur = millis()-lv_measStart;
  printf("BME680 measuring TPHG time = %d\n", lv_measDur );
  gv_stru_tphg = bme6.readTPHG();
  printf("BME680 T:%f, P:%f, H:%f, G:%f\n\n", gv_stru_tphg.temp1, gv_stru_tphg.pres1, gv_stru_tphg.humi1, gv_stru_tphg.gasr1);

  gv_lux = veml.readLux(VEML_LUX_AUTO);
  gv_vbat = (analogRead(A1) * gv_vbat_coef) / 4096;
  printf("Lux:%f, Vbat: %f\n\n", gv_lux, gv_vbat);
}

/*  @brief  Send all data and status info to thingspeak.com   */
void gf_send2ts() {
  if (WiFi.status() != WL_CONNECTED) mkistdf_wifiCon(); // Run only one time to switch ON wifi

  if (WiFi.status() == WL_CONNECTED) {
    // Forming STATUS string for ThingSpeak.com
    char lv_rtc_str[23] = "______-cFFrFsFzFFv4.00";
    uint8_t lv_hms;
    if (!getLocalTime(&gv_tist)) Serial.print("Failed to obtain time.\n");
    else {
      Serial.print(&gv_tist);   Serial.print(" => time update success.\n");
      lv_hms = gv_tist.tm_hour;
      lv_rtc_str[0] = (char)(lv_hms / 10 + 48);   lv_rtc_str[1] = (char)(lv_hms % 10 + 48);
      lv_hms = gv_tist.tm_min;
      lv_rtc_str[2] = (char)(lv_hms / 10 + 48);   lv_rtc_str[3] = (char)(lv_hms % 10 + 48);
      lv_hms = gv_tist.tm_sec;
      lv_rtc_str[4] = (char)(lv_hms / 10 + 48);   lv_rtc_str[5] = (char)(lv_hms % 10 + 48);
    }
    lv_rtc_str[11] = mkistdf_byte2char(esp_reset_reason());
    lv_rtc_str[13] = mkistdf_byte2char(esp_sleep_get_wakeup_cause());
    lv_rtc_str[15] = mkistdf_byte2char(gv_sleep_count >> 4);   lv_rtc_str[16] = mkistdf_byte2char(gv_sleep_count);
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
    uint16_t lv_vbat = (gv_vbat * 100);
    lv_rtc_str[21] = (char)(lv_vbat % 10 + 48);
    lv_vbat = lv_vbat / 10;
    lv_rtc_str[20] = (char)(lv_vbat % 10 + 48);
    lv_vbat = lv_vbat / 10;
    lv_rtc_str[18] = (char)(lv_vbat % 10 + 48);

    Serial.println(lv_rtc_str);

    ThingSpeak.setStatus(lv_rtc_str);
    ThingSpeak.setField(1, gv_stru_tph.temp1); // set the fields with the values
    ThingSpeak.setField(2, gv_stru_tph.pres1);
    ThingSpeak.setField(3, gv_stru_tph.humi1);
    ThingSpeak.setField(4, gv_stru_tphg.temp1);
    ThingSpeak.setField(5, gv_stru_tphg.pres1);
    ThingSpeak.setField(6, gv_stru_tphg.humi1);
    ThingSpeak.setField(7, gv_lux);
    ThingSpeak.setField(8, gv_stru_tphg.gasr1);

    int t_ret_code = ThingSpeak.writeFields(my_channel_num, write_api_key);
    if (t_ret_code == 200) Serial.println("ThingSpeak ch. update successful.");
    else {
      Serial.print("Problem updating channel. HTTP error = "); Serial.println(t_ret_code);
    }
  }
  else   Serial.println(" -> No connection WiFi. Data not send to ThingSpeak.");
}

/*  @brief  Test VEML7700 (Debug info)    */
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

/*  @brief  Standart Arduino function setup() */
void setup() {
  setCpuFrequencyMhz(80); //  must be First

  Serial.begin(115200);
  delay(3000);
  Serial.println("======================= Start Setup ========================");
#ifdef enDEBUG
  mkistdf_cpuInfo();
#endif

  analogSetAttenuation(ADC_11db);
  if (adcAttachPin(A1)) Serial.println("ADC attach to pin A1 success.");
  else Serial.println("ADC NOT attach to pin A1 !");

  Wire.begin();

  if (!veml.begin()) {
    Serial.println("Sensor VEML7700 not found.");
    veml.setPowerSaveMode(3);
  }
  else Serial.println("Sensor VEML7700 found.");

  uint8_t k;  //  code chip

  Serial.print("Check a bme280 => "); 
  k = bme2.check(0x76);               // check bme280 and SW reset
  if (k == 0) Serial.print("not found, check cables.\n");
  else {
    mkistdf_prnByte(k);
    Serial.print(" found chip code.\n");
  }
  bme2.begin(cd_FOR_MODE, cd_SB_500MS, cd_FIL_x2, cd_OS_x16, cd_OS_x16, cd_OS_x16);

  Serial.print("Check a bme680 => "); 
  k = bme6.check(0x77);               // check bmp680 and SW reset
  if (k == 0) Serial.print("not found, check cables.\n");
  else {
    mkistdf_prnByte(k);
    Serial.print(" found chip code.\n");
  }
  bme6.begin(cd_FIL_x2, cd_OS_x16, cd_OS_x16, cd_OS_x16);
  // Init ALL 10 heat set point
  for (uint8_t i = 0; i < 10; i++) bme6.initGasPointX(i, 250+i*10, 60+i*10);
#ifdef enDEBUG
  printf("10 set Points = idac_heat_, res_heat_, gas_wait_ :\n");
  for (uint8_t i = 0; i < 10; i++) printf("Heat Point %d = %d %d %d\n", i, bme6.readReg(0x50+i), bme6.readReg(0x5A+i), bme6.readReg(0x64+i));
#endif

  WiFi.mode(WIFI_STA);
  ThingSpeak.begin(wifi_client);      // Initialize ThingSpeak

  configTime(3600, 3600, "pool.ntp.org");   // init time.h win NTP server, +1 GMT & +1 summer time

  gv_sleep_time = 600000000;    //  Light sleep mode time = 600 sec = 10 min
  // gv_sleep_time = 20000000;
  esp_sleep_enable_timer_wakeup(gv_sleep_time);
  Serial.println("========================= End Setup =======================\n");
}

//=================================================================================================

/*  @brief  Standart Arduino function loop() */
void loop() {
  mkistdf_wifiCon();
  gf_readData();

  delay(500);
  gf_send2ts();

  Serial.println("WiFi disconect.");
  WiFi.disconnect();

  Serial.print("Go to light sleep mode for sec = ");  Serial.print(gv_sleep_time/1000000);
  delay(500);
  esp_light_sleep_start();
  delay(500);
  Serial.print("\n...WakeUp from sleep mode, Ncount = ");
  Serial.println(gv_sleep_count);
  Serial.println();

  gv_sleep_count++;
  // delay(30000);
}

//=================================================================================================