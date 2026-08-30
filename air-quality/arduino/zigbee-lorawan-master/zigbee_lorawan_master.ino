#include <SoftwareSerial.h>
#include <SPI.h>
#include <lmic.h>
#include <hal/hal.h>


// ======================================================
// Sensor packet data structure
// MUST stay near the top for Arduino preprocessor
// ======================================================

typedef struct SensorPacket {

  uint8_t node;

  uint32_t seq;
  uint32_t timestamp;

  uint16_t gas;
  uint16_t pm1;
  uint16_t pm25;
  uint16_t pm10;

} SensorPacket;


// ======================================================
// AIR QUALITY MASTER
// ZigBee -> Binary LoRaWAN
// ======================================================


// ======================================================
// 1. XBee / ZigBee
// ======================================================

// Arduino RX = D4
// Arduino TX = D5
SoftwareSerial xbeeSerial(4, 5);

#define ZIGBEE_BUFFER_SIZE 96

char zigbeeBuffer[ZIGBEE_BUFFER_SIZE];
uint8_t zigbeeIndex = 0;
bool receivingPacket = false;


// ======================================================
// 2. ChirpStack ABP credentials
// ======================================================

// Replace these values with your ChirpStack ABP credentials.
// Never commit real session keys to a public repository.

// Network Session Key
static const PROGMEM u1_t NWKSKEY[16] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Application Session Key
static const PROGMEM u1_t APPSKEY[16] = {
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Device Address
static const u4_t DEVADDR = 0x00000000;


// ABP does not use OTAA callbacks
void os_getArtEui(u1_t* buf) {}
void os_getDevEui(u1_t* buf) {}
void os_getDevKey(u1_t* buf) {}


// ======================================================
// 3. Dragino LoRa Shield v1.4 / RFM98
// ======================================================

const lmic_pinmap lmic_pins = {
  .nss = 10,
  .rxtx = LMIC_UNUSED_PIN,
  .rst = 9,
  .dio = {2, 6, 7}
};


// ======================================================
// 4. FIFO queue
// ======================================================

#define QUEUE_SIZE 6

SensorPacket packetQueue[QUEUE_SIZE];

uint8_t queueHead = 0;
uint8_t queueTail = 0;
uint8_t queueCount = 0;


// ======================================================
// 5. LoRaWAN state
// ======================================================

bool lorawanBusy = false;


// ======================================================
// 6. Binary payload definition
// ======================================================
//
// Byte 0      = protocol version
// Byte 1      = node
// Bytes 2-5   = seq
// Bytes 6-9   = timestamp
// Bytes 10-11 = gas
// Bytes 12-13 = pm1
// Bytes 14-15 = pm25
// Bytes 16-17 = pm10
//
// Total = 18 bytes
// ======================================================

#define PAYLOAD_VERSION 1
#define LORAWAN_PAYLOAD_SIZE 18

uint8_t txBuffer[LORAWAN_PAYLOAD_SIZE];


// ======================================================
// 7. Diagnostic counters
// ======================================================

unsigned long zigbeeValidCount = 0;
unsigned long zigbeeInvalidCount = 0;

unsigned long queueAddedCount = 0;
unsigned long queueDropCount = 0;

unsigned long lorawanQueuedCount = 0;
unsigned long lorawanCompleteCount = 0;


// ======================================================
// 8. Forward declarations
// ======================================================

bool getUnsignedValue(
  const char* packet,
  const char* key,
  unsigned long& value
);

bool parsePacket(
  const char* input,
  SensorPacket& packet
);

bool enqueuePacket(
  const SensorPacket& packet
);

bool dequeuePacket(
  SensorPacket& packet
);

void encodePayload(
  const SensorPacket& packet,
  uint8_t* payload
);

void printSensorPacket(
  const SensorPacket& packet
);

void tryLoRaWANSend();


// ======================================================
// 9. Extract numeric field
// ======================================================

bool getUnsignedValue(
  const char* packet,
  const char* key,
  unsigned long& value
) {

  const char* ptr = strstr(packet, key);

  if (ptr == NULL) {
    return false;
  }

  ptr += strlen(key);

  char* endPtr;

  value = strtoul(ptr, &endPtr, 10);

  if (endPtr == ptr) {
    return false;
  }

  return true;
}


// ======================================================
// 10. Parse ZigBee ASCII packet
// ======================================================

bool parsePacket(
  const char* input,
  SensorPacket& packet
) {

  // Must start with <DATA
  if (strncmp(input, "<DATA", 5) != 0) {
    return false;
  }


  size_t len = strlen(input);

  if (len < 10) {
    return false;
  }


  // Must end with >
  if (input[len - 1] != '>') {
    return false;
  }


  // ------------------------------------
  // NODE
  // ------------------------------------

  if (strstr(input, "NODE=S1") != NULL) {

    packet.node = 1;

  }
  else if (strstr(input, "NODE=S2") != NULL) {

    packet.node = 2;

  }
  else {

    return false;
  }


  // ------------------------------------
  // Numeric fields
  // ------------------------------------

  unsigned long value;


  // SEQ
  if (!getUnsignedValue(input, "SEQ=", value)) {
    return false;
  }

  packet.seq = (uint32_t)value;


  // Timestamp
  if (!getUnsignedValue(input, "TS=", value)) {
    return false;
  }

  packet.timestamp = (uint32_t)value;


  // GAS
  if (!getUnsignedValue(input, "GAS=", value)) {
    return false;
  }

  if (value > 65535UL) {
    return false;
  }

  packet.gas = (uint16_t)value;


  // PM1
  if (!getUnsignedValue(input, "PM1=", value)) {
    return false;
  }

  if (value > 65535UL) {
    return false;
  }

  packet.pm1 = (uint16_t)value;


  // PM2.5
  if (!getUnsignedValue(input, "PM25=", value)) {
    return false;
  }

  if (value > 65535UL) {
    return false;
  }

  packet.pm25 = (uint16_t)value;


  // PM10
  if (!getUnsignedValue(input, "PM10=", value)) {
    return false;
  }

  if (value > 65535UL) {
    return false;
  }

  packet.pm10 = (uint16_t)value;


  return true;
}


// ======================================================
// 11. FIFO enqueue
// ======================================================

bool enqueuePacket(
  const SensorPacket& packet
) {

  if (queueCount >= QUEUE_SIZE) {

    queueDropCount++;

    Serial.print(F("[QUEUE DROP #"));
    Serial.print(queueDropCount);
    Serial.println(F("] FIFO full"));

    return false;
  }


  packetQueue[queueTail] = packet;


  queueTail++;

  if (queueTail >= QUEUE_SIZE) {
    queueTail = 0;
  }


  queueCount++;

  queueAddedCount++;


  Serial.print(F("[QUEUE ADD #"));
  Serial.print(queueAddedCount);

  Serial.print(F("] size="));
  Serial.println(queueCount);


  return true;
}


// ======================================================
// 12. FIFO dequeue
// ======================================================

bool dequeuePacket(
  SensorPacket& packet
) {

  if (queueCount == 0) {
    return false;
  }


  packet = packetQueue[queueHead];


  queueHead++;

  if (queueHead >= QUEUE_SIZE) {
    queueHead = 0;
  }


  queueCount--;


  return true;
}


// ======================================================
// 13. Binary encoder
// ======================================================

void encodePayload(
  const SensorPacket& packet,
  uint8_t* payload
) {

  // Version
  payload[0] = PAYLOAD_VERSION;

  // Node
  payload[1] = packet.node;


  // ------------------------------------
  // SEQ - uint32 big endian
  // ------------------------------------

  payload[2] =
    (packet.seq >> 24) & 0xFF;

  payload[3] =
    (packet.seq >> 16) & 0xFF;

  payload[4] =
    (packet.seq >> 8) & 0xFF;

  payload[5] =
    packet.seq & 0xFF;


  // ------------------------------------
  // Timestamp - uint32 big endian
  // ------------------------------------

  payload[6] =
    (packet.timestamp >> 24) & 0xFF;

  payload[7] =
    (packet.timestamp >> 16) & 0xFF;

  payload[8] =
    (packet.timestamp >> 8) & 0xFF;

  payload[9] =
    packet.timestamp & 0xFF;


  // ------------------------------------
  // GAS
  // ------------------------------------

  payload[10] =
    (packet.gas >> 8) & 0xFF;

  payload[11] =
    packet.gas & 0xFF;


  // ------------------------------------
  // PM1
  // ------------------------------------

  payload[12] =
    (packet.pm1 >> 8) & 0xFF;

  payload[13] =
    packet.pm1 & 0xFF;


  // ------------------------------------
  // PM2.5
  // ------------------------------------

  payload[14] =
    (packet.pm25 >> 8) & 0xFF;

  payload[15] =
    packet.pm25 & 0xFF;


  // ------------------------------------
  // PM10
  // ------------------------------------

  payload[16] =
    (packet.pm10 >> 8) & 0xFF;

  payload[17] =
    packet.pm10 & 0xFF;
}


// ======================================================
// 14. Print parsed packet
// ======================================================

void printSensorPacket(
  const SensorPacket& packet
) {

  Serial.print(F("NODE=S"));
  Serial.print(packet.node);

  Serial.print(F(" SEQ="));
  Serial.print(packet.seq);

  Serial.print(F(" TS="));
  Serial.print(packet.timestamp);

  Serial.print(F(" GAS="));
  Serial.print(packet.gas);

  Serial.print(F(" PM1="));
  Serial.print(packet.pm1);

  Serial.print(F(" PM25="));
  Serial.print(packet.pm25);

  Serial.print(F(" PM10="));
  Serial.println(packet.pm10);
}


// ======================================================
// 15. Try LoRaWAN transmission
// ======================================================

void tryLoRaWANSend() {

  if (lorawanBusy) {
    return;
  }


  if (queueCount == 0) {
    return;
  }


  SensorPacket packet;


  if (!dequeuePacket(packet)) {
    return;
  }


  encodePayload(
    packet,
    txBuffer
  );


  // Mark busy BEFORE LMIC call
  lorawanBusy = true;


  // FPort = 1
  // confirmed = 0
  LMIC_setTxData2(
    1,
    txBuffer,
    LORAWAN_PAYLOAD_SIZE,
    0
  );


  lorawanQueuedCount++;


  Serial.print(F("[LORAWAN TX #"));
  Serial.print(lorawanQueuedCount);

  Serial.print(F("] queue="));
  Serial.println(queueCount);


  Serial.print(F("[TX DATA] "));

  printSensorPacket(packet);
}


// ======================================================
// 16. LMIC events
// ======================================================

void onEvent(ev_t ev) {

  switch (ev) {


    case EV_TXCOMPLETE:

      lorawanCompleteCount++;

      Serial.print(F("[LMIC TX COMPLETE #"));
      Serial.print(lorawanCompleteCount);
      Serial.println(F("]"));


      // Transaction finished
      lorawanBusy = false;


      if (LMIC.txrxFlags & TXRX_ACK) {

        Serial.println(
          F("[DOWNLINK] ACK received")
        );
      }


      if (LMIC.dataLen) {

        Serial.print(F("[DOWNLINK] "));
        Serial.print(LMIC.dataLen);
        Serial.println(F(" bytes"));
      }


      // Immediately try next queued packet
      tryLoRaWANSend();

      break;


    case EV_SCAN_TIMEOUT:

      Serial.println(
        F("[LMIC] EV_SCAN_TIMEOUT")
      );

      break;


    case EV_BEACON_FOUND:

      Serial.println(
        F("[LMIC] EV_BEACON_FOUND")
      );

      break;


    case EV_BEACON_MISSED:

      Serial.println(
        F("[LMIC] EV_BEACON_MISSED")
      );

      break;


    case EV_BEACON_TRACKED:

      Serial.println(
        F("[LMIC] EV_BEACON_TRACKED")
      );

      break;


    case EV_JOINING:

      Serial.println(
        F("[LMIC] EV_JOINING")
      );

      break;


    case EV_JOINED:

      Serial.println(
        F("[LMIC] EV_JOINED")
      );

      break;


    case EV_JOIN_FAILED:

      Serial.println(
        F("[LMIC] EV_JOIN_FAILED")
      );

      break;


    case EV_REJOIN_FAILED:

      Serial.println(
        F("[LMIC] EV_REJOIN_FAILED")
      );

      break;


    case EV_LOST_TSYNC:

      Serial.println(
        F("[LMIC] EV_LOST_TSYNC")
      );

      break;


    case EV_RESET:

      Serial.println(
        F("[LMIC] EV_RESET")
      );

      break;


    case EV_RXCOMPLETE:

      Serial.println(
        F("[LMIC] EV_RXCOMPLETE")
      );

      break;


    case EV_LINK_DEAD:

      Serial.println(
        F("[LMIC] EV_LINK_DEAD")
      );

      break;


    case EV_LINK_ALIVE:

      Serial.println(
        F("[LMIC] EV_LINK_ALIVE")
      );

      break;


    default:

      Serial.print(F("[LMIC] EVENT "));
      Serial.println((unsigned)ev);

      break;
  }
}


// ======================================================
// 17. SETUP
// ======================================================

void setup() {

  // PC Serial Monitor
  Serial.begin(115200);


  // XBee
  xbeeSerial.begin(9600);


  delay(1000);


  Serial.println();
  Serial.println(F("================================"));

  Serial.println(
    F("AIR QUALITY MASTER V2")
  );

  Serial.println(
    F("ZigBee -> Binary LoRaWAN")
  );

  Serial.println(
    F("Arduino UNO + Dragino RFM98")
  );

  Serial.println(
    F("EU433 Single Channel")
  );

  Serial.println(
    F("433.175 MHz / SF7 / BW125")
  );

  Serial.println(
    F("Binary payload = 18 bytes")
  );

  Serial.println(F("================================"));


  // ====================================================
  // LMIC initialization
  // ====================================================

  os_init();

  LMIC_reset();


  // ====================================================
  // ABP Session
  // ====================================================

#ifdef PROGMEM

  uint8_t appskey[sizeof(APPSKEY)];
  uint8_t nwkskey[sizeof(NWKSKEY)];


  memcpy_P(
    appskey,
    APPSKEY,
    sizeof(APPSKEY)
  );


  memcpy_P(
    nwkskey,
    NWKSKEY,
    sizeof(NWKSKEY)
  );


  LMIC_setSession(
    0x000000,
    DEVADDR,
    nwkskey,
    appskey
  );


#else


  LMIC_setSession(
    0x000000,
    DEVADDR,
    NWKSKEY,
    APPSKEY
  );


#endif


// ====================================================
// SINGLE-CHANNEL GATEWAY FIX
// Keep ONLY EU433 channel 0 enabled.
// Channel 0 = 433.175 MHz
// ====================================================

  LMIC_disableChannel(1);
  LMIC_disableChannel(2);

  LMIC_setAdrMode(0);

  LMIC_setLinkCheckMode(0);

  LMIC_setDrTxpow(
    DR_SF7,
    14
  );


  Serial.print(F("DevAddr: 0x"));
  Serial.println(DEVADDR, HEX);


  Serial.println(
    F("[READY] Waiting for ZigBee packets...")
  );
}


// ======================================================
// 18. LOOP
// ======================================================

void loop() {

  // LMIC must be serviced continuously
  os_runloop_once();


  // If free and queue contains packets,
  // send next packet.
  tryLoRaWANSend();


  // ====================================================
  // Read XBee / ZigBee
  // ====================================================

  while (xbeeSerial.available() > 0) {

    char c = xbeeSerial.read();


    // ------------------------------------
    // Start packet
    // ------------------------------------

    if (c == '<') {

      zigbeeIndex = 0;

      zigbeeBuffer[zigbeeIndex++] = c;

      receivingPacket = true;
    }


    // ------------------------------------
    // End packet
    // ------------------------------------

    else if (
      c == '>' &&
      receivingPacket
    ) {

      if (
        zigbeeIndex <
        ZIGBEE_BUFFER_SIZE - 1
      ) {

        zigbeeBuffer[zigbeeIndex++] = c;

        zigbeeBuffer[zigbeeIndex] = '\0';


        SensorPacket packet;


        if (
          parsePacket(
            zigbeeBuffer,
            packet
          )
        ) {

          zigbeeValidCount++;


          Serial.print(
            F("[ZIGBEE VALID #")
          );

          Serial.print(
            zigbeeValidCount
          );

          Serial.print(
            F("] ")
          );


          printSensorPacket(
            packet
          );


          enqueuePacket(
            packet
          );

        }

        else {

          zigbeeInvalidCount++;


          Serial.print(
            F("[ZIGBEE INVALID #")
          );

          Serial.print(
            zigbeeInvalidCount
          );

          Serial.println(
            F("]")
          );
        }
      }


      zigbeeIndex = 0;

      receivingPacket = false;
    }


    // ------------------------------------
    // Packet body
    // ------------------------------------

    else if (
      receivingPacket
    ) {

      if (
        zigbeeIndex <
        ZIGBEE_BUFFER_SIZE - 1
      ) {

        zigbeeBuffer[
          zigbeeIndex++
        ] = c;

      }

      else {

        zigbeeIndex = 0;

        receivingPacket = false;

        zigbeeInvalidCount++;


        Serial.println(
          F("[ZIGBEE] Packet too long")
        );
      }
    }
  }
}
