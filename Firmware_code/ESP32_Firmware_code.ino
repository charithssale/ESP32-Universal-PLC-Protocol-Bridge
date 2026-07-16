#include "arduino_secrets.h"

/*  GradBridge.ino â Heltec WiFi LoRa 32 V3 (ESP32-S3)
 *  - Modbus RTU master (Serial1) â MQTT telemetry
 *  - OLED status (WiFi/MQTT/TX/RX, value)
 *  - Latency metrics (last, p95)
 *  - Scoped command topic + allow-list for safe FC5/FC6 writes
 */

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ModbusMaster.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

/************** USER CONFIG â EDIT THESE **************/
static const char* WIFI_SSID      = "pukesh family";
static const char* WIFI_PASSWORD  = "Fuckyou2";

// MQTT (no TLS). Use your PC's IPv4 for a local Mosquitto, e.g. 192.168.1.162
static const char* MQTT_HOST      = "192.168.1.162";
static const uint16_t MQTT_PORT   = 1883;
static const char* MQTT_CLIENT_ID = "heltec-v3-modbus-gw-01";
// Optional auth (leave empty for anonymous broker)
static const char* MQTT_USER      = "";
static const char* MQTT_PASS      = "";

// Modbus RTU (must match CLICK PLC)
static const uint8_t  MODBUS_SLAVE_ID = 1;
static const uint32_t MODBUS_BAUD     = 9600;
static const uint8_t  MODBUS_CFG      = SERIAL_8N1;

// Heltec â RS-485 TTL module (from your schematic)
static const int RX_PIN = 48;  // Heltec receives here (module TXD)
static const int TX_PIN = 47;  // Heltec transmits here (module RXD)

// Modbus addresses to read/write (zero-based)
static const uint16_t HR_ADDR = 0;  // CLICK 40001 â 0 (zero-based)

// Safe write allow-list (zero-based addresses)
static const uint16_t ALLOW_COILS[] = { 0 /* 00001 */ };
static const size_t   ALLOW_COILS_N = sizeof(ALLOW_COILS)/sizeof(ALLOW_COILS[0]);
static const uint16_t ALLOW_REGS[]  = { 0 /* 40001 */ };
static const size_t   ALLOW_REGS_N  = sizeof(ALLOW_REGS)/sizeof(ALLOW_REGS[0]);

// Topics
static const char* MQTT_TOPIC_PUB = "plc/holding/40001";
static const char* MQTT_TOPIC_MET = "plc/metrics/latency";
static const char* MQTT_TOPIC_CMD = "plc/cmd/write/#"; // plc/cmd/write/<coil|reg>/<addr>

/************** OLED (Heltec V3 typical I2C) *********/
static const int OLED_SDA   = 17;
static const int OLED_SCL   = 18;
static const uint8_t OLED_ADDR = 0x3C;
static const int OLED_W = 128, OLED_H = 64;
/*****************************************************/

Adafruit_SSD1306 oled(OLED_W, OLED_H, &Wire, -1);
HardwareSerial& RS485 = Serial1;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
ModbusMaster node;

/************* OLED helper *************/
static void oledStatus(const char* l1, const char* l2 = "", const char* l3 = "") {
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setTextColor(SSD1306_WHITE);
  oled.setCursor(0,0);  oled.println(l1);
  oled.setCursor(0,16); oled.println(l2);
  oled.setCursor(0,32); oled.println(l3);
  oled.display();
}

/************* Latency metrics **********/
static const size_t LAT_BUF_SZ = 100;
static uint32_t lat_ms[LAT_BUF_SZ];
static size_t lat_idx = 0, lat_used = 0;

static void recordLatency(uint32_t ms){
  lat_ms[lat_idx % LAT_BUF_SZ] = ms;
  lat_idx++;
  if (lat_used < LAT_BUF_SZ) lat_used++;
}
static uint32_t p95(){
  if (!lat_used) return 0;
  uint32_t tmp[LAT_BUF_SZ];
  for (size_t i=0;i<lat_used;i++) tmp[i]=lat_ms[i];
  // insertion sort (small N)
  for (size_t i=1;i<lat_used;i++){uint32_t v=tmp[i]; size_t j=i; while(j>0 && tmp[j-1]>v){tmp[j]=tmp[j-1]; j--; } tmp[j]=v;}
  size_t idx = (size_t)((95.0/100.0)*(lat_used-1));
  return tmp[idx];
}
static void publishLatencySummary(){
  char msg[96];
  snprintf(msg, sizeof(msg), "{\"samples\":%u,\"last_ms\":%u,\"p95_ms\":%u}",
           (unsigned)lat_used,
           (unsigned)(lat_used? lat_ms[(lat_idx-1)%LAT_BUF_SZ]:0),
           (unsigned)p95());
  mqtt.publish(MQTT_TOPIC_MET, msg, true);
  Serial.print("[MET] "); Serial.println(msg);
}

/************* Wi-Fi + MQTT *************/
static void waitForWiFi(){
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("[WiFi] Connecting to "); Serial.println(WIFI_SSID);
  oledStatus("WiFiâ¦", WIFI_SSID);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print('.');
    if (millis()-t0 > 20000) {
      Serial.println("\n[WiFi] Timeout, retryingâ¦");
      WiFi.disconnect(true);
      delay(200);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      t0 = millis();
    }
  }
  String ip = WiFi.localIP().toString();
  oledStatus("WiFi OK", ip.c_str());
  Serial.print("[WiFi] Connected, IP: "); Serial.println(ip);
}

static bool isAllowed(const uint16_t* arr, size_t n, uint16_t v){
  for(size_t i=0;i<n;i++) if(arr[i]==v) return true;
  return false;
}

static void onMqtt(char* topic, byte* payload, unsigned int len){
  // plc/cmd/write/<coil|reg>/<human-address>
  String t(topic), pay;
  for (unsigned i=0;i<len;i++) pay += (char)payload[i];

  int i1 = t.indexOf("/write/");
  if (i1 < 0) return;
  String tail = t.substring(i1+7);
  int slash = tail.indexOf('/');
  if (slash < 0) return;
  String typ = tail.substring(0, slash);
  String aStr = tail.substring(slash+1);
  uint32_t addrHuman = aStr.toInt();

  if (typ == "coil"){
    if (addrHuman < 1) return;
    uint16_t addr0 = (uint16_t)(addrHuman - 1);
    if (!isAllowed(ALLOW_COILS, ALLOW_COILS_N, addr0)) { Serial.println("[CMD] coil not allowed"); return; }
    uint16_t v = (uint16_t) pay.toInt();
    uint8_t r = node.writeSingleCoil(addr0, v ? 0xFF00 : 0x0000);
    Serial.printf("[CMD] FC5 coil %u â %u â r=%u\n", addrHuman, v, r);
  } else if (typ == "reg"){
    if (addrHuman < 40001) return;
    uint16_t addr0 = (uint16_t)(addrHuman - 40001);
    if (!isAllowed(ALLOW_REGS, ALLOW_REGS_N, addr0)) { Serial.println("[CMD] reg not allowed"); return; }
    uint16_t v = (uint16_t) pay.toInt();
    uint8_t r = node.writeSingleRegister(addr0, v);
    Serial.printf("[CMD] FC6 HR %u â %u â r=%u\n", addrHuman, v, r);
  }
}

static void ensureMqtt() {
  if (mqtt.connected()) return;
  mqtt.setServer(MQTT_HOST, MQTT_PORT);

  IPAddress ip;
  if (ip.fromString(MQTT_HOST) || WiFi.hostByName(MQTT_HOST, ip))
    Serial.printf("[MQTT] Will connect to %s:%u\n", ip.toString().c_str(), MQTT_PORT);
  else
    Serial.println("[MQTT] DNS failed");

  uint8_t tries = 0;
  while (!mqtt.connected() && tries < 20) {
    bool ok;
    if (MQTT_USER && *MQTT_USER) ok = mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS);
    else                          ok = mqtt.connect(MQTT_CLIENT_ID);
    if (ok) {
      Serial.println("[MQTT] connected");
      oledStatus("MQTT OK");
      mqtt.subscribe(MQTT_TOPIC_CMD);
      break;
    }
    Serial.printf("[MQTT] connect failed, state=%d (try %u)\n", mqtt.state(), tries+1);
    delay(500);
    tries++;
  }
  if (!mqtt.connected()) oledStatus("MQTT â", "check IP/port");
}

/************* Modbus helper ************/
static bool modbusReadHR(uint16_t addr, uint16_t qty, uint16_t* outBuf){
  uint8_t res = node.readHoldingRegisters(addr, qty);
  if (res == node.ku8MBSuccess){
    for (uint16_t i=0;i<qty;i++) outBuf[i] = node.getResponseBuffer(i);
    return true;
  }
  Serial.print("[Modbus] Error: "); Serial.println(res);
  return false;
}

/**************** Setup / Loop ****************/
static unsigned long lastPoll = 0;
static const unsigned long POLL_MS = 1000;
static unsigned sampleCount = 0;

void setup(){
  Serial.begin(115200);
  uint32_t t0=millis(); while(!Serial && millis()-t0<1500){} // donât wait forever

  Serial.println("\nBooting Modbus RTU â MQTT bridge");

  Wire.begin(OLED_SDA, OLED_SCL);
  if (!oled.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)){
    Serial.println("[OLED] not found");
  } else {
    oledStatus("Bootingâ¦");
  }

  // UART1 for RS-485 (pins from config)
  RS485.begin(MODBUS_BAUD, MODBUS_CFG, RX_PIN, TX_PIN);
  node.begin(MODBUS_SLAVE_ID, RS485);
  // If your RS-485 module needs DE/RE control, uncomment and set a GPIO:
  // node.enableTXpin(DE_RE_GPIO);

  waitForWiFi();
  mqtt.setCallback(onMqtt);
  ensureMqtt();
}

void loop(){
  mqtt.loop();
  if (!mqtt.connected()) ensureMqtt();

  if (millis() - lastPoll >= POLL_MS){
    lastPoll = millis();

    uint16_t hr[1] = {0};
    oledStatus("TX â¶", "Read HR40001");

    uint32_t t0 = millis();
    bool ok = modbusReadHR(HR_ADDR, 1, hr);
    uint32_t t1 = millis();
    recordLatency(t1 - t0);
    sampleCount++;

    if (ok){
      oledStatus("RX â", "HR40001:", String(hr[0]).c_str());
      char payload[64];
      snprintf(payload, sizeof(payload), "{\"addr\":%u,\"value\":%u}", 40001, hr[0]);
      mqtt.publish(MQTT_TOPIC_PUB, payload, true);
      Serial.print("[PUB] "); Serial.print(MQTT_TOPIC_PUB); Serial.print(" â "); Serial.println(payload);
    } else {
      oledStatus("RX â", "Modbus err");
    }

    if (sampleCount % 30 == 0) publishLatencySummary();
  }
}
