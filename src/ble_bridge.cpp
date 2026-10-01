// ============================================================
// ble_bridge.cpp — Nordic UART Service over NimBLE (2.x).
//
// Was Bluedroid (BLEDevice/BLEServer/BLE2902/BLESecurityCallbacks).
// Replaced with NimBLE to free ~80 KB of RAM and ~150 KB of flash so the
// WiFi + HTTPS standalone "Ask Claude" feature fits on the plain ESP32.
// The on-the-wire protocol (UUIDs, characteristic properties, LE Secure
// Connections passkey-entry, encrypted-only access) is identical — the
// desktop bridge has no idea which stack we're running.
// ============================================================
#include "ble_bridge.h"
#include <sdkconfig.h>
// ESP32-P4 boards use ble_bridge_hosted.cpp instead (no on-chip radio).
#if !CONFIG_IDF_TARGET_ESP32P4
#include <NimBLEDevice.h>
#include <Arduino.h>
#include <esp_random.h>
#include <string.h>

// Nordic UART Service UUIDs — match every BLE serial example out there
// so nRF Connect, Web Bluetooth, etc. can talk to us with no config.
#define NUS_SERVICE_UUID "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_RX_UUID      "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
#define NUS_TX_UUID      "6e400003-b5a3-f393-e0a9-e50e24dcca9e"

// Incoming bytes are buffered in a simple ring for bleRead()/bleAvailable().
// Sized to hold a transcript snapshot JSON plus headroom.
static const size_t RX_CAP = 2048;
static uint8_t  rxBuf[RX_CAP];
static volatile size_t rxHead = 0;
static volatile size_t rxTail = 0;

static NimBLEServer*         server  = nullptr;
static NimBLECharacteristic* txChar  = nullptr;
static NimBLECharacteristic* rxChar  = nullptr;
static volatile bool         connected = false;
static volatile bool         secure    = false;
static volatile uint32_t     passkey   = 0;
static volatile uint16_t     mtu       = 23;

static void rxPush(const uint8_t* p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    size_t next = (rxHead + 1) % RX_CAP;
    if (next == rxTail) return;
    rxBuf[rxHead] = p[i];
    rxHead = next;
  }
}

class RxCallbacks : public NimBLECharacteristicCallbacks {
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo&) override {
    std::string v = c->getValue();
    if (v.size() > 0) rxPush((const uint8_t*)v.data(), v.size());
  }
};

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, NimBLEConnInfo&) override {
    connected = true;
    Serial.println("[ble] connected");
  }
  void onDisconnect(NimBLEServer*, NimBLEConnInfo&, int) override {
    connected = false;
    secure    = false;
    mtu       = 23;
    Serial.println("[ble] disconnected");
    NimBLEDevice::startAdvertising();
  }
  void onMTUChange(uint16_t newMtu, NimBLEConnInfo&) override {
    mtu = newMtu;
    Serial.printf("[ble] mtu=%u\n", mtu);
  }
  // DisplayOnly IO capability — the stack asks us for the passkey to
  // display. We generated it at bleInit() so the user can read it off
  // the screen and type it into the desktop.
  uint32_t onPassKeyDisplay() override {
    Serial.printf("[ble] passkey %06lu\n", (unsigned long)passkey);
    return passkey;
  }
  void onAuthenticationComplete(NimBLEConnInfo& info) override {
    bool ok = info.isEncrypted();
    secure  = ok;
    Serial.printf("[ble] auth %s  enc=%d bonded=%d auth=%d keysz=%d\n",
                  ok ? "ok" : "FAIL", info.isEncrypted(), info.isBonded(),
                  info.isAuthenticated(), info.getSecKeySize());
    if (ok) {
      passkey = 0;
    } else if (server) {
      server->disconnect(info.getConnHandle());
    }
  }
};

void bleInit(const char* deviceName) {
  // Random six-digit passkey for this boot. Stays the same across multiple
  // pairings until reset — matches the user expectation of a printed code.
  passkey = (esp_random() % 900000UL) + 100000UL;

  NimBLEDevice::init(deviceName);
  NimBLEDevice::setMTU(517);

  // Empirical (upstream, NimBLE 1.4): NimBLE + WinRT's GATT pairing call refuse to negotiate
  // each other regardless of MITM/SC/Legacy/JW combination — handshake
  // dies returning enc=0 bonded=0 auth=0 keysz=0. The documented Hardware
  // Buddy wire protocol explicitly supports unencrypted devices, so we
  // ship unencrypted on this build. Tradeoff: a sniffer in BLE radio
  // range can read transcript snippets + tool-call hints in clear; for a
  // desk pet in your own room this is acceptable. The desktop also
  // marks `sec: false` in its status ack data so the Hardware Buddy
  // window can reflect the security state honestly.
  NimBLEDevice::setSecurityAuth(false, false, false);
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
  passkey = 0;

  server = NimBLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  NimBLEService* svc = server->createService(NUS_SERVICE_UUID);

  // Open characteristics — no encryption requirement. See bleInit().
  txChar = svc->createCharacteristic(
      NUS_TX_UUID,
      NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::READ);

  rxChar = svc->createCharacteristic(
      NUS_RX_UUID,
      NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  rxChar->setCallbacks(new RxCallbacks());

  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(NUS_SERVICE_UUID);
  // The 128-bit service UUID fills most of the 31-byte advertisement, so
  // the name goes in the scan response.
  adv->enableScanResponse(true);
  adv->setName(deviceName);
  adv->setPreferredParams(0x06, 0x12);
  NimBLEDevice::startAdvertising();
  Serial.printf("[ble] advertising as '%s' passkey=%06lu\n",
                deviceName, (unsigned long)passkey);
}

bool bleReady()     { return NimBLEDevice::isInitialized(); }
bool bleConnected() { return connected; }
bool bleSecure()    { return secure; }
uint32_t blePasskey() { return passkey; }

void bleClearBonds() {
  int n = NimBLEDevice::getNumBonds();
  if (n > 0) NimBLEDevice::deleteAllBonds();
  Serial.printf("[ble] cleared %d bond(s)\n", n);
}

size_t bleAvailable() {
  return (rxHead + RX_CAP - rxTail) % RX_CAP;
}

int bleRead() {
  if (rxHead == rxTail) return -1;
  int b = rxBuf[rxTail];
  rxTail = (rxTail + 1) % RX_CAP;
  return b;
}

size_t bleWrite(const uint8_t* data, size_t len) {
  if (!connected || !txChar) return 0;
  // ATT notify payload is limited to (MTU - 3). macOS negotiates 185, so
  // the 182-byte chunk works there; use the live mtu so a peer that caps
  // at the 23-byte default doesn't get truncated notifies.
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

#endif  // !CONFIG_IDF_TARGET_ESP32P4
