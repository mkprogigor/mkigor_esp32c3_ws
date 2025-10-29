/**
 * @brief	A small Arduino project: Easy DIY weather station on esp32-C3, BME280, BME680, VEML7700.
 * @author	Igor Mkprog, mkprogigor@gmail.com
 * @version	V1.1	@date	10.10.2025
 * 
 * @remarks	Glossary, abbreviations used in the module. Name has small or capital letters ("camelCase"),
 *	and consist only 2 or 1 symbol '_', that divide it in => prefix + name + suffix.
 * 	prefix: 
 * 		gv_*	- Global Variable;
 * 		lv_*	- Local Variable (live inside statement);
 * 		cl_*	- CLass;
 * 		cd_*	- Class Definition;
 * 		cgv_*	- Class public (Global) member (Variable);
 * 		clv_*	- Class private (Local) member (Variable);
 * 		clf_*	- Class private (Local) metod (Function);
 * 		lp_*	- in function, local parameter.
 * 	suffix:
 * 		like ending *_t, as usual, point to the type, informative, but not mandatory to use.
 * 		possible is: _i8, _i16, _i32, _i64, _u8, _u16, _u32, _u64, _f, _df, _c, _b, _stru, etc.
 * 		example:	- prefix_nameOfFuncOrVar_suffix, gv_tphg_stru => global var (tphg) structure.
 */

#include <Arduino.h>
#include <WiFi.h>
#include <Wire.h>
#include <ThingSpeak.h>
#include <mkigor_veml.h>
#include <mkigor_BMxx80.h>
#include <mkigor_std.h>

//#define DEBUG_EN		///	uncoment it for debug info

WiFiClient			wifi_client;
cl_VEML7700			veml;
cl_BME280			bme2;
cl_BME680			bme6;

AW_stru_t			gv_lux;
uint8_t				gv_luxGT;
tph_stru			gv_stru_tph;
tphg_stru			gv_stru_tphg;
const static float	gv_vbat_coef = 5.8;
static float		gv_vbat;

uint64_t			gv_sleep_time;
RTC_DATA_ATTR uint8_t gv_sleep_count = 0;

//=================================================================================================
/**	@brief	Print status and config registers form BME690 (debug info)	*/
void lv_dispRegs(void) {
	///	Status reg 0x1D =
	///		7		|		6		|		5		|	4		|	3	|	2	|	1	|	0	|
	///	new_data_0	| gas_measuring	| 	measuring	| 	x		|		gas_meas_index_0		|
	///	Gas_r_lsb reg 0x2B =
	///		7		|		6		|		5		|	4		|	3	|	2	|	1	|	0	|
	///			gas_r				| 	gas_valid_r	|head_stab_r|			gas_range_			|
	///	Ctrl_gas_1 reg 0x71 =
	///		7		|		6		|		5		|	4		|	3	|	2	|	1	|	0	|
	///				|				|				|	run_gas	|  			nb_conv				|
	Serial.print("Status= ");      mkistdf_prnByte(bme6.readReg(0x1D));
	Serial.print("Gas_r_lsb= ");   mkistdf_prnByte(bme6.readReg(0x2B));
	Serial.print("Ctrl_gas_0= ");  mkistdf_prnByte(bme6.readReg(0x70));
	Serial.print("Ctrl_gas_1= ");  mkistdf_prnByte(bme6.readReg(0x71));
	Serial.print("Ctrl_meas= ");   mkistdf_prnByte(bme6.readReg(0x74));
	Serial.println();
}

/**	@brief  Read data from all sensors in project to global variables	*/
void gf_readData() {
	unsigned long lv_measStart, lv_measDur;

	bme2.do1Meas();
	lv_measStart = millis();
	for (uint8_t i = 0; i < 3000; i++) {
		if (bme2.isMeas()) delay(1);
		else break;
	}
	lv_measDur = millis() - lv_measStart;
	printf("BME280 measuring TPHG time = %d\n", lv_measDur);
	gv_stru_tph = bme2.readTPH();
	printf("BME280 T:%f, P:%f, H:%f\n\n", gv_stru_tph.temp1, gv_stru_tph.pres1, gv_stru_tph.humi1);

	bme6.initGasPointX(0, 350, 100, (int16_t)gv_stru_tph.temp1);
	bme6.do1Meas();
	lv_measStart = millis();
	for (uint8_t i = 0; i < 3000; i++) {
		if (bme6.isMeas()) delay(1);
		else break;
	}
	lv_measDur = millis() - lv_measStart;
	printf("BME680 measuring TPHG time = %d\n", lv_measDur);
	gv_stru_tphg = bme6.readTPHG();
	printf("BME680 T:%f, P:%f, H:%f, G:%f\n\n", gv_stru_tphg.temp1, gv_stru_tphg.pres1, gv_stru_tphg.humi1, gv_stru_tphg.gasr1);

	gv_lux = veml.readAW();
	delay(1000);
	gv_lux = veml.readAW();
	GTidx_stru_t lv_gtIdx = veml.readGainTime();
	gv_luxGT = lv_gtIdx.idxGain1 << 4 | lv_gtIdx.idxTime1;

	gv_vbat = (analogRead(A1) * gv_vbat_coef) / 4096;
	
	printf("Lux:%d, Lux idx GT:0x%X, Vbat: %f\n\n", gv_lux.als1, gv_luxGT, gv_vbat);
}

/**	@brief  Send all data and status info to thingspeak.com	*/
void gf_send2ts() {
	if (WiFi.status() != WL_CONNECTED) mkistdf_wifiCon(); // Run only one time to switch ON wifi

	if (WiFi.status() == WL_CONNECTED) {		/// Forming STATUS string for ThingSpeak.com
		char lv_status[23] = "______-rFsFzFFv4.00l00";

		DT_stru_t lv_DT;						/// get time
		if (mkistdf_getDateTime(lv_DT)) {
			lv_status[0] = (char)(lv_DT.hour / 10 + 48);	lv_status[1] = (char)(lv_DT.hour % 10 + 48);
			lv_status[2] = (char)(lv_DT.min / 10 + 48);		lv_status[3] = (char)(lv_DT.min % 10 + 48);
			lv_status[4] = (char)(lv_DT.sec / 10 + 48);		lv_status[5] = (char)(lv_DT.sec % 10 + 48);
		}
		else Serial.println("Failed to obtain http GET time.\n");

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
		lv_status[8]  = mkistdf_byte2char(esp_reset_reason());
		lv_status[10] = mkistdf_byte2char(esp_sleep_get_wakeup_cause());
		lv_status[12] = mkistdf_byte2char(gv_sleep_count >> 4);
		lv_status[13] = mkistdf_byte2char(gv_sleep_count);

		uint16_t lv_vbat = (gv_vbat * 100);
		lv_status[18] = (char)(lv_vbat % 10 + 48);
		lv_vbat = lv_vbat / 10;
		lv_status[17] = (char)(lv_vbat % 10 + 48);
		lv_vbat = lv_vbat / 10;
		lv_status[15] = (char)(lv_vbat % 10 + 48);
		lv_status[20] = mkistdf_byte2char(gv_luxGT >> 4);
		lv_status[21] = mkistdf_byte2char(gv_luxGT);
		Serial.println(lv_status);

		ThingSpeak.setStatus(lv_status);
		ThingSpeak.setField(1, gv_stru_tph.temp1); // set the fields with the values
		ThingSpeak.setField(2, gv_stru_tph.pres1);
		ThingSpeak.setField(3, gv_stru_tph.humi1);
		ThingSpeak.setField(4, gv_stru_tphg.temp1);
		ThingSpeak.setField(5, gv_stru_tphg.pres1);
		ThingSpeak.setField(6, gv_stru_tphg.humi1);
		ThingSpeak.setField(7, float(gv_lux.als1));
		ThingSpeak.setField(8, gv_stru_tphg.gasr1);

		int t_ret_code = ThingSpeak.writeFields(my_channel_num, write_api_key);
		if (t_ret_code == 200) Serial.println("ThingSpeak ch. update successful.");
		else {
			Serial.print("Problem updating channel. HTTP error = "); Serial.println(t_ret_code);
		}
	}
	else   Serial.println(" -> No connection WiFi. Data not send to ThingSpeak.");
}

//=================================================================================================

/**	@brief  Standart Arduino function setup()	*/
void setup() {
	setCpuFrequencyMhz(80); //  must be First

	Serial.begin(115200);
	delay(3000);
	Serial.println("======================= Start Setup ========================");
#ifdef DEBUG_EN
	mkistdf_cpuInfo();
#endif

	analogSetAttenuation(ADC_11db);
	if (adcAttachPin(A1)) Serial.println("ADC attach to pin A1 success.");
	else Serial.println("ADC NOT attach to pin A1 !");

	Wire.begin();

	Serial.print("\nCheck the sensor VEML7700 => ");
	uint16_t lv_chipCode = veml.check(0x10);
	if ( lv_chipCode == 0)	Serial.println("Not found, check cables.");
	else {
		Serial.print("found with chip code (command code 0x07) = ");
		Serial.println(lv_chipCode, HEX);
	}

	uint8_t lv_kc;	///	code chip

	Serial.print("Check a bme280 => ");
	lv_kc = bme2.check(0x76);               // check bme280 and SW reset
	if (lv_kc == 0) Serial.print("not found, check cables.\n");
	else {
		mkistdf_prnByte(lv_kc);
		Serial.print(" found chip code.\n");
	}
	bme2.begin(cd_FOR_MODE, cd_SB_500MS, cd_FIL_x2, cd_OS_x16, cd_OS_x16, cd_OS_x16);

	Serial.print("Check a bme680 => ");
	lv_kc = bme6.check(0x77);               // check bmp680 and SW reset
	if (lv_kc == 0) Serial.print("not found, check cables.\n");
	else {
		mkistdf_prnByte(lv_kc);
		Serial.print(" found chip code.\n");
	}
	bme6.begin(cd_FIL_x2, cd_OS_x16, cd_OS_x16, cd_OS_x16);
	// Init ALL 10 heat set point
	for (uint8_t i = 0; i < 10; i++) bme6.initGasPointX(i, 250 + i * 10, 60 + i * 10);
#ifdef DEBUG_EN
	printf("10 set Points = idac_heat_, res_heat_, gas_wait_ :\n");
	for (uint8_t i = 0; i < 10; i++) printf("Heat Point %d = %d %d %d\n", i, bme6.readReg(0x50 + i), bme6.readReg(0x5A + i), bme6.readReg(0x64 + i));
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

/**	@brief  Standart Arduino function loop()	*/
void loop() {
	Serial.print("Ncount (0-255) = ");	Serial.println(gv_sleep_count);
	veml.writeGainTime(2, 2);	// set gain = 1, time = 100 ms,
	veml.wakeUp();				// in my case sensor doesn't have straight sunlight < 500 lux
	delay(1000);

	mkistdf_wifiCon();
	gf_readData();
	gf_send2ts();

	veml.sleep();
	Serial.println("WiFi disconect.");
	WiFi.disconnect();
	Serial.print("Go to light sleep mode for sec = ");  Serial.print(gv_sleep_time / 1000000);
	delay(200);
	esp_light_sleep_start();
	// delay(20000);
	delay(200);
	Serial.println("\nWakeUp from sleep mode.");
	gv_sleep_count++;
}

//=================================================================================================