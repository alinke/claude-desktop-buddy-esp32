// ============================================================
// ble_bridge_hosted.cpp — Nordic UART Service for ESP32-P4 boards.
//
// The P4 has no radio. Its BLE runs on the board's ESP32-C6 co-processor,
// reached over SDIO through Espressif's ESP-Hosted; the Arduino core ships
// the matching NimBLE host for the P4 and wraps it in its own BLE library,
// which brings the hosted link up in BLEDevice::init(). (NimBLE-Arduino,
// used on every other board, would add a second NimBLE host, so P4 envs
// leave it out.) Same UUIDs, framing and behaviour as ble_bridge.cpp — the
// desktop can't tell the two apart.
// ============================================================
#include "ble_bridge.h"
#include <sdkconfig.h>
#if CONFIG_IDF_TARGET_ESP32P4

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>

#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

static const size_t RX_CAP = 2048;
static uint8_t  rxBuf[RX_CAP];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static BLEServer*         server = nullptr;
static BLECharacteristic* txChar = nullptr;
static volatile bool      connected = false;
static volatile uint16_t  mtu = 23;
static bool               started = false;

static void rxPush(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    size_t next = (rxHead + 1) % RX_CAP;
    if (next == rxTail) return;
    rxBuf[rxHead] = p[i];
    rxHead = next;
  }
}

class RxCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic* c) override {
    String v = c->getValue();
    if (v.length()) rxPush((const uint8_t*)v.c_str(), v.length());
  }
};

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer*) override {
    connected = true;
    Serial.println("[ble] connected");
  }
  void onDisconnect(BLEServer*) override {
    connected = false;
    mtu = 23;
    Serial.println("[ble] disconnected");
    BLEDevice::startAdvertising();
  }
  void onMtuChanged(BLEServer*, ble_gap_conn_desc*, uint16_t newMtu) override {
    mtu = newMtu;
    Serial.printf("[ble] mtu=%u\n", mtu);
  }
};

void bleInit(const char* deviceName) {
  // Brings up the SDIO link to the C6 (hostedInitBLE) and the NimBLE host.
  // A C6 running old ESP-Hosted firmware, or wrong SDIO pins, fail here.
  if (!BLEDevice::init(deviceName)) {
    Serial.println("[ble] init FAILED — check the C6 co-processor / ESP-Hosted link");
    return;
  }
  BLEDevice::setMTU(517);

  server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());
  BLEService* svc = server->createService(NUS_SERVICE_UUID);
  // Unencrypted, like ble_bridge.cpp: the protocol allows open devices.
  txChar = svc->createCharacteristic(
      NUS_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ);
  BLECharacteristic* rx = svc->createCharacteristic(
      NUS_RX_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());
  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  adv->setScanResponse(true);   // name goes in the scan response
  adv->setMinPreferred(0x06);
  adv->setMaxPreferred(0x12);
  BLEDevice::startAdvertising();
  started = true;
  Serial.printf("[ble] advertising as '%s' (via ESP-Hosted)\n", deviceName);
}

bool bleReady()       { return started; }
bool bleConnected()   { return connected; }
bool bleSecure()      { return false; }
uint32_t blePasskey() { return 0; }
void bleClearBonds()  {}

size_t bleAvailable() { return (rxHead + RX_CAP - rxTail) % RX_CAP; }

int bleRead() {
  if (rxHead == rxTail) return -1;
  int b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_CAP;
  return b;
}

size_t bleWrite(const uint8_t* data, size_t len) {
  if (!started || !connected || !txChar) return 0;
  size_t chunk = mtu > 3 ? mtu - 3 : 20;
  if (chunk > 180) chunk = 180;
  size_t sent = 0;
  while (sent < len) {
    size_t n = len - sent;
    if (n > chunk) n = chunk;
    txChar->setValue((uint8_t*)(data + sent), n);
    txChar->notify();
    sent += n;
    delay(4);
  }
  return sent;
}

#endif  // CONFIG_IDF_TARGET_ESP32P4
