/**
This application makes use of various example projects supplied by Microchip
*/



#include "asf.h"
#include "common/include/nm_common.h"
#include "driver/include/m2m_wifi.h"
#include "main.h"
#include "cryptoauthlib.h"
#include "jwt/atca_jwt.h"
#include "at25dfx.h"
#include "config/conf_at25dfx.h"
#include "socket/include/socket.h"
#include "MQTTClient/Wrapper/mqtt.h"


//Header of the application
#define STRING_EOL    "\r\n"
#define STRING_HEADER "Google Cloud Connect Example"STRING_EOL \
"-- With HTTP provisioning --"STRING_EOL	\
"-- Compiled: "__DATE__ " "__TIME__ " --"STRING_EOL




//UART module for debug.
static struct usart_module cdc_uart_module;

//Details of the Google cloud account connection, where the clientID should be unique to the device.
char mqtt_user[64] = "unused";

//Enough space is needed for the password as it is a large JWT
char mqtt_password[1024];

//Each client ID is unique to the device
char clientID[256] = "projects/altron-atwinc1500/locations/europe-west1/registries/arrow-registry/devices/samw25-iot";

// Instance of MQTT service.
static struct mqtt_module mqtt_inst;

// Receive buffer of the MQTT service.
static unsigned char mqtt_read_buffer[MAIN_MQTT_BUFFER_SIZE];

static unsigned char mqtt_send_buffer[MAIN_MQTT_BUFFER_SIZE];

// UART buffer.
static char uart_buffer[MAIN_CHAT_BUFFER_SIZE];

// Written size of UART buffer.
static int uart_buffer_written = 0;

// A buffer of character from the serial.
static uint16_t uart_ch_buffer;

//The global utc variable which is used in the JWT.
uint32_t utc;

//Configurations for the ATECC108A
#define LOCK_DATA 0

#define ECC108_I2C_ADDR 0x60

struct rtc_module    rtc_instance;

//Indication if the mqtt bridge is connected or not.
int mqtt_connected =0;

// Sockets that are used for the server and the client, these are not secure sockets
static SOCKET tcp_server_socket = -1;

static uint8_t tcp_connected = 0;

static uint8_t gau8SocketBuffer[MAIN_CHAT_BUFFER_SIZE];

static SOCKET tcp_client_socket = -1;

static SOCKET tcp_client_socket_external = -1;

static uint8_t gau8SocketTestBuffer[MAIN_WIFI_M2M_BUFFER_SIZE];

//IP address is converted to a number that can be used by the WINC
long long int IPnumber;

char* CommandArray[3];

#define AT25DFX_BUFFER_SIZE  (100)

char *SSID ;

char *Password;

char *SSID_Read[AT25DFX_BUFFER_SIZE];

char *Password_Read[AT25DFX_BUFFER_SIZE];

char *testInput;

char *SSID_read[AT25DFX_BUFFER_SIZE];

char *Password_read[AT25DFX_BUFFER_SIZE];

//default values, it can be changed through a tcp connection 
char PROJECT_ID[64] = "altron-atwinc1500";

char REGION_ID[64]	= "europe-west1";

char REGISTRY_ID[64] = "arrow-registry";

char DEVICE_ID[64]	=	"samw25-iot";

char MAIN_CHAT_TOPIC[128] = "/devices/samw25-iot/state";

static volatile uint32_t event_count = 0;

//Boolean variables to indicate the connection and publish status to google cloud, only once both are activated can the
//interrupt publish messages to google cloud
bool canPublish = false;

bool gcpConnected = false;

bool publishInterruptBool = false;


typedef struct s_msg_wifi_product {
	uint8_t name[9];
} t_msg_wifi_product;

static t_msg_wifi_product msg_wifi_product = {
	.name = MAIN_WIFI_M2M_PRODUCT_NAME,
};

struct spi_module at25dfx_spi;

struct at25dfx_chip_module at25dfx_chip;

static uint8_t wifi_connected = 0;



void handle_tcp_command(char* inputMessage);

// Prototype for MQTT subscribe Callback
void SubscribeHandler(MessageData *msgData);

int config_mqtt_password(char* buf, size_t buflen);

void sendTCP(char* message);

int commandTCP(char* IPAddress);

void connectToGCP(void);

void publishToGCP(void);

void disconnectGCP(void);

void event_counter(struct events_resource *resource);

static void uart_callback(const struct usart_module *const module)
{
	static uint8_t ignore_cnt = 0;
	if (ignore_cnt > 0) {
		ignore_cnt--;
		return;
		} else if (uart_ch_buffer == 0x1B) { // Ignore escape and following 2 characters.
		ignore_cnt = 2;
		return;
		} else if (uart_ch_buffer == 0x8) { // Ignore backspace.
		return;
	}
	// If input string is bigger than buffer size limit, ignore the excess part.
	if (uart_buffer_written < MAIN_CHAT_BUFFER_SIZE) {
		uart_buffer[uart_buffer_written++] = uart_ch_buffer & 0xFF;
	}
}

//Function to convert a returned date into a utc timestamp
uint32_t getTimestamp(uint32_t year, uint32_t month, uint32_t day,uint32_t hour, uint32_t minute, uint32_t second ){
	
	uint32_t ret = 0;

	//January and February are counted as months 13 and 14 of the previous year
	if(month <= 2)
	{
		month += 12;
		year -= 1;
	}
	
	//Convert years to days
	ret = (365 * year) + (year / 4) - (year / 100) + (year / 400);
	//Convert months to days
	ret += (30 * month) + (3 * (month + 1) / 5) + day;
	//Unix time starts on January 1st, 1970
	ret -= 719561;
	//Convert days to seconds
	ret *= 86400;
	//Add hours, minutes and seconds
	ret += (3600 * hour) + (60 * minute) + second;
	
	return ret;
	
}

#define HEX2ASCII(x) (((x) >= 10) ? (((x) - 10) + 'A') : ((x) + '0'))
static void set_dev_name_to_mac(uint8 *name, uint8 *mac_addr)
{
	// Name must be in the format WINC1500_00:00
	uint16 len;

	len = m2m_strlen(name);
	if (len >= 5) {
		name[len - 1] = HEX2ASCII((mac_addr[5] >> 0) & 0x0f);
		name[len - 2] = HEX2ASCII((mac_addr[5] >> 4) & 0x0f);
		name[len - 4] = HEX2ASCII((mac_addr[4] >> 0) & 0x0f);
		name[len - 5] = HEX2ASCII((mac_addr[4] >> 4) & 0x0f);
	}
}

void updateClientID(void)
{
	printf("Old clientID %s\r\n", clientID);
	snprintf(clientID, 256, "projects/%s/locations/%s/registries/%s/devices/%s",PROJECT_ID, REGION_ID, REGISTRY_ID, DEVICE_ID);
	printf("New clientID %s\r\n", clientID);
}


//Wifi callback for the wifi-system
static void wifi_callback(uint8 msg_type, void *msg_data)
{
	tstrM2mWifiStateChanged *msg_wifi_state;
	uint8 *msg_ip_addr;

	switch (msg_type) {
		case M2M_WIFI_RESP_CON_STATE_CHANGED:
		msg_wifi_state = (tstrM2mWifiStateChanged *)msg_data;
		if (msg_wifi_state->u8CurrState == M2M_WIFI_CONNECTED) {
			printf("Wi-Fi connected\r\n");
			} else if (msg_wifi_state->u8CurrState == M2M_WIFI_DISCONNECTED) {
			printf("Wi-Fi disconnected\r\n");
		}
		break;

		case M2M_WIFI_REQ_DHCP_CONF:
		msg_ip_addr = (uint8 *)msg_data;
		printf("Wi-Fi IP is %u.%u.%u.%u\r\n",
		msg_ip_addr[0], msg_ip_addr[1], msg_ip_addr[2], msg_ip_addr[3]);
		break;
		
		case M2M_WIFI_RESP_GET_SYS_TIME:{
			tstrSystemTime *strSysTime_now = (tstrSystemTime *)msg_data;
			utc = getTimestamp(strSysTime_now->u16Year, strSysTime_now->u8Month, strSysTime_now->u8Day, strSysTime_now->u8Hour, strSysTime_now->u8Minute, strSysTime_now->u8Second );
			break;
		}
		case M2M_WIFI_RESP_PROVISION_INFO:
		{
			tstrM2MProvisionInfo *pstrProvInfo = (tstrM2MProvisionInfo *)msg_data;
			printf("wifi_cb: M2M_WIFI_RESP_PROVISION_INFO.\r\n");

			if (pstrProvInfo->u8Status == M2M_SUCCESS) {
				m2m_wifi_connect((char *)pstrProvInfo->au8SSID, strlen((char *)pstrProvInfo->au8SSID), pstrProvInfo->u8SecType,
				pstrProvInfo->au8Password, M2M_WIFI_CH_ALL);
				
				SSID = pstrProvInfo->au8SSID;
				Password = pstrProvInfo->au8Password;
				printf("SSID %s\r\n", SSID);
				
				//Writing the SSID and Password of the network into the serial flash memory
				at25dfx_chip_set_sector_protect(&at25dfx_chip, 0x10000, false);
				at25dfx_chip_set_sector_protect(&at25dfx_chip, 0x20000, false);
				
				at25dfx_chip_erase_block(&at25dfx_chip, 0x10000, AT25DFX_BLOCK_SIZE_4KB);
				at25dfx_chip_erase_block(&at25dfx_chip, 0x20000, AT25DFX_BLOCK_SIZE_4KB);
				
				at25dfx_chip_write_buffer(&at25dfx_chip, 0x10000, SSID, AT25DFX_BUFFER_SIZE);
				at25dfx_chip_write_buffer(&at25dfx_chip, 0x20000, Password, AT25DFX_BUFFER_SIZE);
				
				at25dfx_chip_set_global_sector_protect(&at25dfx_chip, true);
				
				at25dfx_chip_read_buffer(&at25dfx_chip, 0x10000, SSID_Read, AT25DFX_BUFFER_SIZE);
				printf("SSID read from flash:  %s\r\n", SSID_Read);
				
				at25dfx_chip_read_buffer(&at25dfx_chip, 0x20000, Password, AT25DFX_BUFFER_SIZE);
				printf("Password read from flash:  %s\r\n", Password);
				
				m2m_wifi_request_dhcp_client();
				
				wifi_connected = 1;
				} else {
				printf("wifi_cb: Provision failed.\r\n");
			}
		}
		break;

		default:
		break;
	}
}

//Callback function for the server socket only
static void server_socket_cb(SOCKET sock, uint8_t u8Msg, void *pvMsg)
{
	switch (u8Msg) {
		/* Socket bind */
		case SOCKET_MSG_BIND:
		{
			tstrSocketBindMsg *pstrBind = (tstrSocketBindMsg *)pvMsg;
			if (pstrBind && pstrBind->status == 0) {
				printf("socket_cb: bind success!\r\n");
				listen(tcp_server_socket, 0);
				} else {
				printf("socket_cb: bind error!\r\n");
				close(tcp_server_socket);
				tcp_server_socket = -1;
			}
		}
		break;

		/* Socket listen */
		case SOCKET_MSG_LISTEN:
		{
			tstrSocketListenMsg *pstrListen = (tstrSocketListenMsg *)pvMsg;
			if (pstrListen && pstrListen->status == 0) {
				printf("socket_cb: listen success!\r\n");
				accept(tcp_server_socket, NULL, NULL);
				} else {
				printf("socket_cb: listen error!\r\n");
				close(tcp_server_socket);
				tcp_server_socket = -1;
			}
		}
		break;

		/* Connect accept */
		case SOCKET_MSG_ACCEPT:
		{
			tstrSocketAcceptMsg *pstrAccept = (tstrSocketAcceptMsg *)pvMsg;
			if (pstrAccept) {
				printf("socket_cb: accept success!\r\n");
				accept(tcp_server_socket, NULL, NULL);
				tcp_client_socket = pstrAccept->sock;
				tcp_connected = 1;
				recv(tcp_client_socket, gau8SocketTestBuffer, sizeof(gau8SocketTestBuffer), 0);
				} else {
				printf("socket_cb: accept error!\r\n");
				close(tcp_server_socket);
				tcp_server_socket = -1;
				tcp_connected = 0;
			}
		}
		break;
		case SOCKET_MSG_CONNECT:
		{
			tstrSocketConnectMsg *pstrConnect = (tstrSocketConnectMsg *)pvMsg;
			if (pstrConnect && pstrConnect->s8Error >= 0) {
				printf("socket_cb: connect success.\r\n");
				tcp_connected = 1;
				recv(tcp_client_socket, gau8SocketBuffer, sizeof(gau8SocketBuffer), 0);
				
				} else {
				printf("socket_cb: connect error!\r\n");
				tcp_connected = 0;
				
			}
		}
		break;

		/* Message send */
		case SOCKET_MSG_SEND:
		{

			recv(tcp_client_socket, gau8SocketBuffer, sizeof(gau8SocketBuffer), 0);
			
		}
		break;

		/* Message receive */
		case SOCKET_MSG_RECV:
		{
			tstrSocketRecvMsg *pstrRecv = (tstrSocketRecvMsg *)pvMsg;
			if (pstrRecv && pstrRecv->s16BufferSize > 0) {
				printf("socket_cb: recv success!\r\n");
				//Handle the command and execute the appropriate function if there is for the command.
				handle_tcp_command(pstrRecv->pu8Buffer);
				//printf("The message received is %s\r\n", pstrRecv->pu8Buffer);
				
				send(tcp_client_socket, &msg_wifi_product, sizeof(t_msg_wifi_product), 0);
				
				}else {
				printf("socket_cb: recv error!\r\n");
				close(tcp_server_socket);
				tcp_server_socket = -1;
				break;
			}
			recv(tcp_client_socket, gau8SocketBuffer, sizeof(gau8SocketBuffer), 0);
		}

		break;

		default:
		break;
	}
}

//Callback function for the client socket only
void client_socket_cb(SOCKET sock, uint8_t u8Msg, void *pvMsg){
	switch (u8Msg) {
		/* Socket connected */
		case SOCKET_MSG_CONNECT:
		{
			tstrSocketConnectMsg *pstrConnect = (tstrSocketConnectMsg *)pvMsg;
			if (pstrConnect && pstrConnect->s8Error >= 0) {
				printf("socket_cb: connect success!\r\n");
				send(tcp_client_socket_external, &msg_wifi_product, sizeof(t_msg_wifi_product), 0);
				} else {
				printf("socket_cb: connect error!\r\n");
				close(tcp_client_socket_external);
				tcp_client_socket_external = -1;
			}
		}
		break;

		/* Message send */
		case SOCKET_MSG_SEND:
		{
			printf("socket_cb: send success!\r\n");
			recv(tcp_client_socket_external, gau8SocketTestBuffer, sizeof(gau8SocketTestBuffer), 0);
		}
		break;

		/* Message receive */
		case SOCKET_MSG_RECV:
		{
			tstrSocketRecvMsg *pstrRecv = (tstrSocketRecvMsg *)pvMsg;
			if (pstrRecv && pstrRecv->s16BufferSize > 0) {
				printf("socket_cb: recv success!\r\n");
				} else {
				printf("socket_cb: recv error!\r\n");
				close(tcp_client_socket_external);
				tcp_client_socket_external = -1;
			}
		}
		break;

		default:
		break;
	}
}


// The socket callback handler, this will pass the sockets to their correct callback.
// NB the MQTT socket is a secure socket
static void socket_event_handler(SOCKET sock, uint8_t msg_type, void *msg_data)
{
	if (sock == tcp_server_socket || sock == tcp_client_socket){
		server_socket_cb(sock, msg_type, msg_data);
	}
	else if (sock == tcp_client_socket_external){
		client_socket_cb(sock, msg_type, msg_data);
	}
	else{
		mqtt_socket_event_handler(sock, msg_type, msg_data);
	}
}

//Intialising the ATECC108A
static void at25dfx_init(void)
{
	struct at25dfx_chip_config at25dfx_chip_config;
	struct spi_config at25dfx_spi_config;

	at25dfx_spi_get_config_defaults(&at25dfx_spi_config);
	at25dfx_spi_config.mode_specific.master.baudrate = AT25DFX_CLOCK_SPEED;
	at25dfx_spi_config.mux_setting = AT25DFX_SPI_PINMUX_SETTING;
	at25dfx_spi_config.pinmux_pad0 = AT25DFX_SPI_PINMUX_PAD0;
	at25dfx_spi_config.pinmux_pad1 = AT25DFX_SPI_PINMUX_PAD1;
	at25dfx_spi_config.pinmux_pad2 = AT25DFX_SPI_PINMUX_PAD2;
	at25dfx_spi_config.pinmux_pad3 = AT25DFX_SPI_PINMUX_PAD3;

	spi_init(&at25dfx_spi, AT25DFX_SPI, &at25dfx_spi_config);
	spi_enable(&at25dfx_spi);
	
	at25dfx_chip_config.type = AT25DFX_MEM_TYPE;
	at25dfx_chip_config.cs_pin = AT25DFX_CS;

	at25dfx_chip_init(&at25dfx_chip, &at25dfx_spi, &at25dfx_chip_config);
}

//Erase the SSID and Password from the serial flash for reprogramming
static void eraseFlash(void){
	at25dfx_chip_set_sector_protect(&at25dfx_chip, 0x10000, false);
	at25dfx_chip_set_sector_protect(&at25dfx_chip, 0x20000, false);
	
	at25dfx_chip_erase_block(&at25dfx_chip, 0x10000, AT25DFX_BLOCK_SIZE_4KB);
	at25dfx_chip_erase_block(&at25dfx_chip, 0x20000, AT25DFX_BLOCK_SIZE_4KB);
	
	at25dfx_chip_set_global_sector_protect(&at25dfx_chip, true);
}


static void socket_resolve_handler(uint8_t *doamin_name, uint32_t server_ip)
{
	mqtt_socket_resolve_handler(doamin_name, server_ip);
}

//Function to handle to the TCP commands sent through to the server.
void handle_tcp_command(char* inputMessage){
	
	char* pch;
	
	int count = 0;
	int arrCount = 0;
	//printf("Splitting string into individual elements\r\n");
	pch = strtok(inputMessage, "-");
	
	
	
	while (pch != NULL){
		//printf("%s\n", pch);
		CommandArray[count++] = pch;
		pch = strtok(NULL, "-");
		
	}

	size_t commandSize = 3;
	size_t connectSize = 7;
	size_t sendSize = 4;
	
	//Default ACK message for the TCP bridge
	char ack_message[128] = "Command received";
	
	//Server can only take one command, however this functionality can be extended within this function.
	if(!strncmp("GCP", CommandArray[0], commandSize)){
		printf("Google Cloud Command\r\n");
		
		if(!strncmp("CONNECT", CommandArray[1], strlen("CONNECT"))){
			connectToGCP();
			publishToGCP();
			canPublish = true;
			snprintf(ack_message, 128, "Connecting to Google Cloud");
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		if(!strncmp("STOP", CommandArray[1], strlen("STOP"))){
			disconnectGCP();
			snprintf(ack_message, 128, "Disconnected from Google Cloud");
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		if(!strncmp("CLIENTID", CommandArray[1], strlen("CLIENTID"))){
			printf("Changing Client ID \r\n");
			strcpy(clientID, CommandArray[2]);
			printf("The new Client ID is %s\r\n", clientID);
			
			snprintf(ack_message, 128, "ClientID updated to %s",clientID);
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		if(!strncmp("USERNAME", CommandArray[1], strlen("USERNAME"))){
			printf("Changing the Username \r\n");
			strcpy(mqtt_user, CommandArray[2]);
			printf("The new username is %s\r\n", mqtt_user);
			
			snprintf(ack_message, 128, "Username updated to %s",mqtt_user);
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		if(!strncmp("PROJECTID", CommandArray[1], strlen("PROJECTID"))){
			printf("Changing the PROJECTID \r\n");
			strcpy(PROJECT_ID, CommandArray[2]);
			printf("The new projectID is %s\r\n", PROJECT_ID);
			
			snprintf(ack_message, 128, "Project ID updated to %s",PROJECT_ID);
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		if(!strncmp("REGIONID", CommandArray[1], strlen("REGIONID"))){
			printf("Changing the REGIONID \r\n");
			strcpy(REGION_ID, CommandArray[2]);
			printf("The new regionID is %s\r\n", REGION_ID);
			
			snprintf(ack_message, 128, "RegionID updated to %s",REGION_ID);
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}

		if(!strncmp("REGISTRYID", CommandArray[1], strlen("REGISTRYID"))){
			printf("Changing the REGISTRYID \r\n");
			strcpy(REGISTRY_ID, CommandArray[2]);
			printf("The new registryID is %s\r\n", REGISTRY_ID);
			
			snprintf(ack_message, 128, "Registry ID updated to %s",REGISTRY_ID);
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}

		if(!strncmp("DEVICEID", CommandArray[1], strlen("DEVICEID"))){
			printf("Changing the DEVICEID \r\n");
			strcpy(DEVICE_ID, CommandArray[2]);
			printf("The new deviceID is %s\r\n", DEVICE_ID);
			
			snprintf(ack_message, 128, "Device ID updated to %s",DEVICE_ID);
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		if(!strncmp("UPDATECLIENTID", CommandArray[1], strlen("UPDATE-CLIENTID"))){
			printf("Updating the ClientID \r\n");
			updateClientID();
			printf("The new clientID is %s\r\n", clientID);
			
			snprintf(ack_message, 128, "ClientID updated using new details");
			send(tcp_client_socket, ack_message, strlen(ack_message), 0);
		}
		
		/*
		if(!strncmp("PUBLISH", CommandArray[1], connectSize)){
		canPublish = true;
		publishToGCP();
		}
		*/
	}
	
	
	
}


static void mqtt_callback(struct mqtt_module *module_inst, int type, union mqtt_data *data)
{
	switch (type) {
		case MQTT_CALLBACK_SOCK_CONNECTED:
		{
			//Once the MQTT bridge is connected between the device and the broker, can now attempt to connect to device project
			if (data->sock_connected.result >= 0) {
				printf("\r\nConnecting to Broker...");
				size_t size1 = 512;
				memset(mqtt_password, 0, sizeof mqtt_password);
				//Creation of mqtt password which is a JWT
				config_mqtt_password(mqtt_password, size1);
				delay_ms(100);
				mqtt_connect_broker(module_inst, 1, mqtt_user, mqtt_password, clientID , NULL, NULL, 0, 0, 0);
				} else {
				printf("Connect fail to server(%s)! retry it automatically.\r\n", main_mqtt_broker);
				mqtt_connect(module_inst, main_mqtt_broker); /* Retry that. */
				
			}
		}
		break;

		case MQTT_CALLBACK_CONNECTED:
		if (data->connected.result == MQTT_CONN_RESULT_ACCEPT) {
			usart_enable_callback(&cdc_uart_module, USART_CALLBACK_BUFFER_RECEIVED);
			printf("Preparation of the chat has been completed.\r\n");
			mqtt_connected = 1;
			gcpConnected = true;
			} else {
			/* Cannot connect for some reason. */
			printf("MQTT broker decline your access! error code %d\r\n", data->connected.result);
		}

		break;

		case MQTT_CALLBACK_DISCONNECTED:
		/* Stop timer and USART callback. */
		printf("MQTT disconnected\r\n");
		usart_disable_callback(&cdc_uart_module, USART_CALLBACK_BUFFER_RECEIVED);
		break;
	}
}

//Configure the UART console
static void configure_console(void)
{
	struct usart_config usart_conf;

	usart_get_config_defaults(&usart_conf);
	usart_conf.mux_setting = EDBG_CDC_SERCOM_MUX_SETTING;
	usart_conf.pinmux_pad0 = EDBG_CDC_SERCOM_PINMUX_PAD0;
	usart_conf.pinmux_pad1 = EDBG_CDC_SERCOM_PINMUX_PAD1;
	usart_conf.pinmux_pad2 = EDBG_CDC_SERCOM_PINMUX_PAD2;
	usart_conf.pinmux_pad3 = EDBG_CDC_SERCOM_PINMUX_PAD3;
	usart_conf.baudrate    = 115200;

	stdio_serial_init(&cdc_uart_module, EDBG_CDC_MODULE, &usart_conf);
	/* Register USART callback for receiving user input. */
	usart_register_callback(&cdc_uart_module, (usart_callback_t)uart_callback, USART_CALLBACK_BUFFER_RECEIVED);
	usart_enable_callback(&cdc_uart_module, USART_CALLBACK_BUFFER_RECEIVED);
	usart_enable(&cdc_uart_module);
}

//Configure MQTT
static void configure_mqtt(void)
{
	struct mqtt_config mqtt_conf;
	int result;

	mqtt_get_config_defaults(&mqtt_conf);
	/* To use the MQTT service, it is necessary to always set the buffer and the timer. */
	mqtt_conf.keep_alive = 36000;
	mqtt_conf.read_buffer = mqtt_read_buffer;
	mqtt_conf.read_buffer_size = MAIN_MQTT_BUFFER_SIZE;
	mqtt_conf.send_buffer = mqtt_send_buffer;
	mqtt_conf.send_buffer_size = MAIN_MQTT_BUFFER_SIZE;
	//port 8883 is used for the SSL connections
	mqtt_conf.port = 8883;
	mqtt_conf.tls = SOCKET_FLAGS_SSL;
	
	result = mqtt_init(&mqtt_inst, &mqtt_conf);
	if (result < 0) {
		printf("MQTT initialization failed. Error code is (%d)\r\n", result);
		while (1) {
		}
	}

	result = mqtt_register_callback(&mqtt_inst, mqtt_callback);
	if (result < 0) {
		printf("MQTT register callback failed. Error code is (%d)\r\n", result);
		while (1) {
		}
	}
}

//Configuration of the security device
bool config_device(ATCADeviceType dev, uint8_t addr) {
	static ATCAIfaceCfg cfg;
	cfg.iface_type = ATCA_I2C_IFACE,
	cfg.devtype = dev;
	cfg.atcai2c.slave_address = addr << 1;
	cfg.atcai2c.baud = 115200;
	cfg.atcai2c.bus = 0;
	cfg.wake_delay = (dev == ATECC108A) ? 800 : 2560;
	cfg.rx_retries = 25;
	if (atcab_init(&cfg) != ATCA_SUCCESS)
	return false;
	atcab_wakeup(); // may return error if device is already awake
	return true;
};

//Generation of the MQTT password
int config_mqtt_password(char* buf, size_t buflen){
	int rv = -1;

	if(buf && buflen)
	{
		atca_jwt_t jwt;
		
		m2m_wifi_get_system_time();
		
		uint32_t ts = rtc_count_get_count(&rtc_instance);

		/* Build the JWT */
		
		rv = atca_jwt_init(&jwt, buf, buflen);
		if(ATCA_SUCCESS != rv)
		{
			return rv;
		}

		if(ATCA_SUCCESS != (rv = atca_jwt_add_claim_numeric(&jwt, "iat", utc)))
		{
			return rv;
		}

		if(ATCA_SUCCESS != (rv = atca_jwt_add_claim_numeric(&jwt, "exp", utc + 86400)))
		{
			return rv;
		}

		if(ATCA_SUCCESS != (rv = atca_jwt_add_claim_string(&jwt, "aud", PROJECT_ID)))
		{
			return rv;
		}

		rv = atca_jwt_finalize(&jwt, 0);
		if(rv != ATCA_SUCCESS){
			
		}
		//printf("The JWT token is: %s\r\n", jwt);

		atcab_release();
	}
	return rv;
}


//Only call the below function if the public key is needed. This assumes that the
//ATECC108A has already been provisioned and has a private key stored within it.
const uint8_t public_key_x509_header[]= { 0x30, 0x59, 0x30, 0x13, 0x06, 0x07, 0x2A, 0x86,
	0x48, 0xCE, 0x3D, 0x02, 0x01, 0x06, 0x08, 0x2A,
	0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07, 0x03,
0x42, 0x00, 0x04 };

//const uint8_t public_Key[64];
//This generates a public from the private key in the listed slot, the public key is then printed
//onto the console
int config_print_public_key(void)
{
	uint8_t buf[128];
	uint8_t * tmp;
	size_t buf_len = sizeof(buf);
	int i;
	ATCA_STATUS rv;
	
	// Calculate where the raw data will fit into the buffer
	tmp = buf + sizeof(buf) - ATCA_PUB_KEY_SIZE - sizeof(public_key_x509_header);

	// Copy the header
	memcpy(tmp, public_key_x509_header, sizeof(public_key_x509_header));

	// Get public key without private key generation
	//This illustrates that the private key is storred within space 0;
	rv = atcab_get_pubkey(0, tmp + sizeof(public_key_x509_header));

	atcab_release();

	if (ATCA_SUCCESS != rv ) {
		return rv;
	}

	// Convert to base 64
	rv = atcab_base64encode(tmp, ATCA_PUB_KEY_SIZE + sizeof(public_key_x509_header), buf, &buf_len);

	if(ATCA_SUCCESS != rv)
	{
		return rv;
	}

	// Add a null terminator
	buf[buf_len] = 0;

	// Print out the key out in PEM format
	printf("-----BEGIN PUBLIC KEY-----\r\n%s\r\n-----END PUBLIC KEY-----\r\n", buf);

}

//This function takes in a string IP address and yeilds a single number that the WINC can connect to
long long int calculateIP(char *IPstring){
	
	char* p;
	int count = 0;
	int i =0;
	
	char* IPArray[4];
	int IPNum[4];
	long long int IPnumber = 0;
	int testNumber = 0;
	p = strtok(IPstring, ".");
	while (p != NULL){
		//printf("%s\n", p);
		
		IPArray[count++] = p;
		
		p = strtok(NULL, ".");
	}
	for(i = 0; i < 4; i++){
		//printf("%d\r\n", i);
		IPNum[i] = atoi(IPArray[i]);
	}
	IPnumber = (IPNum[3]*1) + (IPNum[2]*256) + (IPNum[1]*65536) + 3221225472;
	return IPnumber;
}

//Handles any input message from the UART, the application can open up a TCP connection to an external server,
//furthermore the application can intiate a google cloud connection through the UART
void handle_input_message(void)
{
	int i, msg_len;

	if (uart_buffer_written == 0) {
		return;
		} else if (uart_buffer_written >= MAIN_CHAT_BUFFER_SIZE) {
		
		send(tcp_client_socket, uart_buffer, MAIN_CHAT_BUFFER_SIZE, 0);
		uart_buffer_written = 0;
		} else {
		for (i = 0; i < uart_buffer_written; i++) {
			/* Find newline character ('\n' or '\r\n') and publish the previous string . */
			if (uart_buffer[i] == '\n') {
				/* Remove LF and CR from uart buffer.*/
				if (uart_buffer[i - 1] == '\r') {
					msg_len = i - 1;
					} else {
					msg_len = i;
				}

				uart_buffer[msg_len] = 0;
				printf("The input message is %s\r\n",uart_buffer);
				
				char* pch;
				
				int count = 0;
				int arrCount = 0;
				printf("Splitting string into individual elements\r\n");
				pch = strtok(uart_buffer, " ");
				
				
				
				while (pch != NULL){
					printf("%s\n", pch);
					
					CommandArray[count++] = pch;
					
					pch = strtok(NULL, " ");
					
				}
				
				
				for (arrCount = 0; arrCount < 3 ; arrCount++){
					//	printf("arr count is %d\r\n", arrCount);
					printf("Value %d in the array is %s\r\n",arrCount, CommandArray[arrCount] );
				}
				
				size_t commandSize = 3;
				size_t connectSize = 7;
				size_t sendSize = 4;
				char *message = "Hello World";
				
				printf("Done Splitting\r\n");
				printf("The command is %s\r\n", CommandArray[0]);
				
				
				if(!strncmp("TCP",CommandArray[0],commandSize)){
					printf("The strings are equal");
					printf("The IP address %s\r\n", CommandArray[2]);
					if(!strncmp("CONNECT", CommandArray[1],connectSize)){
						commandTCP(CommandArray[2]);
					}
					if (!strncmp("SEND", CommandArray[1], sendSize))
					{
						sendTCP(CommandArray[2]);
						printf("Sent %s to the server\r\n", CommandArray[2]);
					}
					
					
				}
				
				if(!strncmp("GCP", CommandArray[0], commandSize)){
					printf("Google Cloud Command");
					if(!strncmp("CONNECT", CommandArray[1], connectSize)){
						connectToGCP();
						publishToGCP();
						canPublish = true;
					}
					/*
					if(!strncmp("PUBLISH", CommandArray[1], connectSize)){
					publishToGCP();
					}
					*/
				}
				
				if (uart_buffer_written > i + 1) {
					memmove(uart_buffer, uart_buffer + i + 1, uart_buffer_written - i - 1);
					uart_buffer_written = uart_buffer_written - i - 1;
					} else {
					uart_buffer_written = 0;
				}
				break;
			}
		}
	}
}

//Intiate the connection to Google cloud
void connectToGCP(void){
	mqtt_connect(&mqtt_inst, main_mqtt_broker);
}

void disconnectGCP(void){
	mqtt_disconnect(&mqtt_inst, 1);
	gcpConnected = false;
	canPublish = false;
	
}

//Publish a timestamp to google cloud
void publishToGCP(void){
	//publishes the utc timestamp in json format
	m2m_wifi_get_system_time();
	delay_ms(100);
	char publishMessage [50];
	sprintf(publishMessage, "{timestamp: %u}", utc) ;
	mqtt_publish(&mqtt_inst, MAIN_CHAT_TOPIC, publishMessage, strlen(publishMessage), 0, 0);
	printf("Message -- %s-- has been published\r\n", publishMessage);
	//Need to make this bool false, as allows the interrupt system to work.
	publishInterruptBool = false;
}

//Opens a TCP client connection to the specified IP address, intitated through the UART
int commandTCP(char* IPAddress){
	
	IPnumber = calculateIP(IPAddress);
	
	struct sockaddr_in addr2;
	int8_t ret;

	// Initialize socket address structure.
	addr2.sin_family = AF_INET;
	addr2.sin_port = _htons(MAIN_WIFI_M2M_EXTERNAL_SERVER_PORT);
	addr2.sin_addr.s_addr = _htonl(IPnumber);
	
	if ((tcp_client_socket_external < 0) && (wifi_connected ==1)){
		if ((tcp_client_socket_external = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
			printf("main: failed to create TCP client socket4 error!\r\n");
			return;
		}
		//Connect to tcp client
		ret = connect(tcp_client_socket_external, (struct sockaddr *)&addr2, sizeof(struct sockaddr_in));
		printf("connected to the external TCP server \r\n");
		if (ret < 0) {
			close(tcp_client_socket_external);
			tcp_client_socket_external = -1;
		}
	}
}

//Sends a message over the TCP Client connection, will only work if there is a connection to a tcp client
void sendTCP(char* message){
	printf("message to the server is: %s\r\n", message);
	send(tcp_client_socket_external, message, strlen(message), 0);
}



//Functions to intialise the interrupt service
static void configure_event_channel(struct events_resource *resource)
{
	struct events_config config;

	events_get_config_defaults(&config);

	config.generator      = CONF_EVENT_GENERATOR;
	config.edge_detect    = EVENTS_EDGE_DETECT_RISING;
	config.path           = EVENTS_PATH_SYNCHRONOUS;
	config.clock_source   = GCLK_GENERATOR_0;

	events_allocate(resource, &config);
}

static void configure_event_user(struct events_resource *resource)
{
	events_attach_user(resource, CONF_EVENT_USER);
}

static void configure_tc(struct tc_module *tc_instance)
{
	struct tc_config config_tc;
	struct tc_events config_events;

	tc_get_config_defaults(&config_tc);

	config_tc.counter_size    = TC_COUNTER_SIZE_16BIT;
	config_tc.wave_generation = TC_WAVE_GENERATION_NORMAL_FREQ;
	config_tc.clock_source    = GCLK_GENERATOR_1;
	config_tc.clock_prescaler = TC_CLOCK_PRESCALER_DIV8;

	tc_init(tc_instance, CONF_TC_MODULE, &config_tc);

	config_events.generate_event_on_overflow = true;
	tc_enable_events(tc_instance, &config_events);

	tc_enable(tc_instance);

}

static void configure_event_interrupt(struct events_resource *resource,
struct events_hook *hook)
{
	events_create_hook(hook, event_counter);

	events_add_hook(resource, hook);
	events_enable_interrupt_source(resource, EVENTS_INTERRUPT_DETECT);
}


void event_counter(struct events_resource *resource)
{
	if(events_is_interrupt_set(resource, EVENTS_INTERRUPT_DETECT)) {
		//printf("Event fired!\r\n");
		//Only when the two bools are tru will the "publishInterruptBool" become true.
		//When it is true, the if statment in main will pass and the application wil publish to GCP
		//The interrupt service cannot publish to the cloud as the interrupt service freezes alot of the functionality
		//which may include the wifi module
		if(canPublish == true && gcpConnected == true){
			publishInterruptBool = true;
		}
		
		event_count++;
		
		events_ack_interrupt(resource, EVENTS_INTERRUPT_DETECT);

	}
}

/**
* \brief Main application function.
*
* Application entry point.
*
* \return program return value.
*/
int main(void)
{
	tstrWifiInitParam param;
	int8_t ret;
	uint8_t mac_addr[6];
	uint8_t u8IsMacAddrValid;
	
	struct tc_module       tc_instance;
	struct events_resource example_event;
	struct events_hook     hook;
	
	// Initialize the board.
	system_init();

	// Initialize the UART console.
	configure_console();

	// Output example information
	printf(STRING_HEADER);

	// Initialize the MQTT service.
	configure_mqtt();

	// Initialize the BSP.
	nm_bsp_init();
	
	//Enable interrupts
	system_interrupt_enable_global();

	configure_event_channel(&example_event);
	configure_event_user(&example_event);
	configure_event_interrupt(&example_event, &hook);
	configure_tc(&tc_instance);
	
	at25dfx_init();
	
	// Initialise the ATECC108A
	if(config_device(ATECC108A, ECC108_I2C_ADDR)){
		printf("ATECC108A Configured\r\n");
	}
	
	//Wake up the ATECC108A chip
	at25dfx_chip_wake(&at25dfx_chip);
	
	//Checks if the chip is responsive
	if (at25dfx_chip_check_presence(&at25dfx_chip) != STATUS_OK) {
		// Handle missing or non-responsive device
		printf("Chip is unresponsive\r\n");
	}
	
	//at25dfx_chip_wake(&at25dfx_chip);

	// Initialize Wi-Fi parameters structure.
	memset((uint8_t *)&param, 0, sizeof(tstrWifiInitParam));
	
	// Initialize Wi-Fi driver with data and status callbacks.
	param.pfAppWifiCb = wifi_callback; // Set Wi-Fi event callback.
	ret = m2m_wifi_init(&param);
	if (M2M_SUCCESS != ret) {
		printf("main: m2m_wifi_init call error!(%d)\r\n", ret);
		while (1) { // Loop forever.
		}
	}
	
	// Configure the SNTP with the NTP server
	ret = m2m_wifi_configure_sntp((uint8_t *)MAIN_WORLDWIDE_NTP_POOL_HOSTNAME, strlen(MAIN_WORLDWIDE_NTP_POOL_HOSTNAME), SNTP_ENABLE_DHCP);
	if(M2M_SUCCESS != ret) {
		printf("main: SNTP %s configuration Failure\r\n",MAIN_WORLDWIDE_NTP_POOL_HOSTNAME);
	}
	
	m2m_wifi_get_otp_mac_address(mac_addr, &u8IsMacAddrValid);
	if (!u8IsMacAddrValid) {
		m2m_wifi_set_mac_address(gau8MacAddr);
	}

	m2m_wifi_get_mac_address(gau8MacAddr);

	set_dev_name_to_mac((uint8_t *)gacDeviceName, gau8MacAddr);
	set_dev_name_to_mac((uint8_t *)gstrM2MAPConfig.au8SSID, gau8MacAddr);
	m2m_wifi_set_device_name((uint8_t *)gacDeviceName, (uint8_t)m2m_strlen((uint8_t *)gacDeviceName));
	gstrM2MAPConfig.au8DHCPServerIP[0] = 0xC0; /* 192 */
	gstrM2MAPConfig.au8DHCPServerIP[1] = 0xA8; /* 168 */
	gstrM2MAPConfig.au8DHCPServerIP[2] = 0x01; /* 1 */
	gstrM2MAPConfig.au8DHCPServerIP[3] = 0x01; /* 1 */
	
	
	at25dfx_chip_read_buffer(&at25dfx_chip, 0x10000, SSID_read, AT25DFX_BUFFER_SIZE);
	//printf("SSID READ %s\r\n", SSID_read);
	
	char c;
	c = SSID_read[0];
	
	if(( c>='a' && c<='z') || (c>='A' && c<='Z')){
		printf("SSID is valid, will continue to connect\r\n");
		at25dfx_chip_read_buffer(&at25dfx_chip, 0x10000, SSID_read, AT25DFX_BUFFER_SIZE);
		at25dfx_chip_read_buffer(&at25dfx_chip, 0x20000, Password_read, AT25DFX_BUFFER_SIZE);
		
		printf("SSID  read is %s\r\n", SSID_read);
		printf("Password read is: %s\r\n", Password_read);
		
		m2m_wifi_connect((char *)SSID_read, strlen(SSID_read), M2M_WIFI_SEC_WPA_PSK, (char *)Password_read, M2M_WIFI_CH_ALL);
		wifi_connected = 1;
		
	}

	else{
		//printf("First letter of SSID is: %c\r\n", SSID_read[0]);
		printf("SSID is not valid, wifi provisioning will start");
		
		m2m_wifi_start_provision_mode((tstrM2MAPConfig *)&gstrM2MAPConfig, (char *)gacHttpProvDomainName, 1);
		printf("Provision Mode started.\r\nConnect to [%s] via AP[%s] and fill up the page.\r\n", MAIN_HTTP_PROV_SERVER_DOMAIN_NAME, gstrM2MAPConfig.au8SSID);
	}
	
	
	//Initialize the socket address structure
	struct sockaddr_in server_socket_address;
	server_socket_address.sin_family = AF_INET;
	server_socket_address.sin_port = _htons(MAIN_WIFI_M2M__INTERNAL_SERVER_PORT);
	server_socket_address.sin_addr.s_addr = 0;
	
	
	
	// Initialize socket interface.
	socketInit();
	registerSocketCallback(socket_event_handler, socket_resolve_handler);


	if (SysTick_Config(system_cpu_clock_get_hz() / 1000))
	{
		puts("ERR>> Systick configuration error\r\n");
		while (1);
	}
	
	m2m_wifi_get_system_time();
	
	
	while (events_is_busy(&example_event)) {
	};
	
	//start the counter which the interrupt system uses
	tc_start_counter(&tc_instance);

	
	while (1) {
		//Handle events from the network.
		m2m_wifi_handle_events(NULL);
		
		//Read any messages inserted from the uart
		usart_read_job(&cdc_uart_module, &uart_ch_buffer);
		
		//Handle commands from the uart
		handle_input_message();
		
		//Spins up a TCP server on port 2323
		if ((tcp_server_socket < 0) && (wifi_connected == 1)) {
			// Open TCP server socket
			if ((tcp_server_socket = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
				printf("main: failed to create TCP server socket error!\r\n");
				continue;
			}
			// Bind service
			bind(tcp_server_socket, (struct sockaddr *)&server_socket_address, sizeof(struct sockaddr_in));
		}
		
		//When the interrupt occurs it sets this bool to true, thus it will publish to the cloud.
		if (publishInterruptBool == true){
			publishToGCP();
		}
		
		//When the button is pressed, it erases the flash memory and restarts the system so that it goes into provisioning mode
		if(port_pin_get_input_level(BUTTON_0_PIN) != BUTTON_0_INACTIVE){
			eraseFlash();
			NVIC_SystemReset();
			delay_s(1);
		}

	}
}
