/**
* \file
*
*
*/

#ifndef MAIN_H_INCLUDED
#define MAIN_H_INCLUDED

#ifdef __cplusplus
extern "C" {
	#endif
	//Wifi mode in provisioning state
	#define MAIN_M2M_AP_SEC                               M2M_WIFI_SEC_OPEN

	//Key if device prompts for key
	#define MAIN_M2M_AP_WEP_KEY                           "1234567890"

	//SSID mode is visible
	#define MAIN_M2M_AP_SSID_MODE                         SSID_MODE_VISIBLE

	//Buffer sizes
	#define MAIN_CHAT_BUFFER_SIZE						  128

	#define MAIN_MQTT_BUFFER_SIZE						  512

	#define MAIN_WIFI_M2M_BUFFER_SIZE					  1460

	//Port the TCP client will connect to
	#define MAIN_WIFI_M2M_EXTERNAL_SERVER_PORT			  (2323)

	#define MAIN_HTTP_PROV_SERVER_DOMAIN_NAME             "arrowconfig.com"

	#define MAIN_M2M_DEVICE_NAME						  "WINC1500_00:00"

	#define MAIN_MAC_ADDRESS						      {0xf8, 0xf0, 0x05, 0x45, 0xD4, 0x84}
	
	#define MAIN_WIFI_M2M_PRODUCT_NAME					  "ACK-WINC\r\n"

	#define MAIN_WIFI_M2M_SERVER_IP						  0xFFFFFFFF /* 255.255.255.255 */

	//Port the server will use
	#define MAIN_WIFI_M2M__INTERNAL_SERVER_PORT		      (2323)

	#define CONF_EVENT_GENERATOR    EVSYS_ID_GEN_TC4_OVF

	#define CONF_EVENT_USER         EVSYS_ID_USER_DMAC_CH_0

	#define CONF_TC_MODULE TC4

	//Topic to where the device will publish data to
	//#define MAIN_CHAT_TOPIC "/devices/samw25-iot/state"

	//MQTT details for google cloud
	#define main_mqtt_broker  "mqtt.googleapis.com"

	//NTP pool to get UTC time that is synchronized with the google servers
	#define MAIN_WORLDWIDE_NTP_POOL_HOSTNAME        "time.google.com"

	//Details for the project in google cloud
	//#define PROJECT_ID				"altron-atwinc1500"

//	#define REGION_ID				"europe-west1"

//	#define REGISTRY_ID				"arrow-registry"

//	#define DEVICE_ID				"samw25-iot"

	static tstrM2MAPConfig gstrM2MAPConfig = {
		MAIN_M2M_DEVICE_NAME, 1, 0, WEP_40_KEY_STRING_SIZE, MAIN_M2M_AP_WEP_KEY, (uint8)MAIN_M2M_AP_SEC, MAIN_M2M_AP_SSID_MODE
	};

	static CONST char gacHttpProvDomainName[] = MAIN_HTTP_PROV_SERVER_DOMAIN_NAME;

	static uint8 gau8MacAddr[] = MAIN_MAC_ADDRESS;

	static sint8 gacDeviceName[] = MAIN_M2M_DEVICE_NAME;

	#ifdef __cplusplus
}
#endif

#endif /* MAIN_H_INCLUDED */
