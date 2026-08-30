#include <SoftwareSerial.h>

SoftwareSerial xbee(2, 3);      // RX, TX - XBee
SoftwareSerial pmSerial(4, 5);  // RX, TX - PMS7003

const char NODE_ID[] = "S2";

const unsigned long START_OFFSET_MS  = 15000;
const unsigned long SEND_INTERVAL_MS = 30000;
const unsigned long PMS_TIMEOUT_MS   = 2500;

unsigned long seq = 0;


// ------------------------------------------------------
// Read one valid PMS7003 frame
// ------------------------------------------------------

bool readPMS7003(uint16_t &pm1, uint16_t &pm25, uint16_t &pm10) {

  uint8_t frame[32];

  pm1 = 0;
  pm25 = 0;
  pm10 = 0;

  pmSerial.listen();

  unsigned long startTime = millis();

  while (millis() - startTime < PMS_TIMEOUT_MS) {

    if (pmSerial.available()) {

      uint8_t firstByte = pmSerial.read();

      if (firstByte != 0x42) {
        continue;
      }

      unsigned long headerTimeout = millis();

      while (!pmSerial.available()) {
        if (millis() - headerTimeout > 200) {
          break;
        }
      }

      if (!pmSerial.available()) {
        continue;
      }

      uint8_t secondByte = pmSerial.read();

      if (secondByte != 0x4D) {
        continue;
      }

      frame[0] = 0x42;
      frame[1] = 0x4D;

      size_t bytesRead = pmSerial.readBytes(
        frame + 2,
        30
      );

      if (bytesRead != 30) {
        continue;
      }

      uint16_t receivedChecksum =
        ((uint16_t)frame[30] << 8) |
        frame[31];

      uint16_t calculatedChecksum = 0;

      for (uint8_t i = 0; i < 30; i++) {
        calculatedChecksum += frame[i];
      }

      if (calculatedChecksum != receivedChecksum) {
        continue;
      }

      pm1 =
        ((uint16_t)frame[10] << 8) |
        frame[11];

      pm25 =
        ((uint16_t)frame[12] << 8) |
        frame[13];

      pm10 =
        ((uint16_t)frame[14] << 8) |
        frame[15];

      return true;
    }
  }

  return false;
}


// ------------------------------------------------------
// Setup
// ------------------------------------------------------

void setup() {

  Serial.begin(9600);

  xbee.begin(9600);
  pmSerial.begin(9600);

  pmSerial.setTimeout(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("AIR QUALITY SENSOR NODE");
  Serial.println("Node: S2");
  Serial.println("Sensors: MQ-135 + PMS7003");
  Serial.println("Network: ZigBee");
  Serial.println("Interval: 30 s");
  Serial.println("Start offset: 15 s");
  Serial.println("================================");
  Serial.println("[WAIT] Initial offset");

  delay(START_OFFSET_MS);

  Serial.println("[READY]");
}


// ------------------------------------------------------
// Main loop
// ------------------------------------------------------

void loop() {

  unsigned long cycleStart = millis();

  uint16_t gasValue = analogRead(A0);

  uint16_t pm1  = 0;
  uint16_t pm25 = 0;
  uint16_t pm10 = 0;

  bool pmsValid = readPMS7003(
    pm1,
    pm25,
    pm10
  );

  seq++;

  unsigned long timestamp = millis();

  char payload[96];

 snprintf(
  payload,
  sizeof(payload),
  "<DATA,NODE=%s,SEQ=%lu,TS=%lu,GAS=%u,PM1=%u,PM25=%u,PM10=%u>",
  NODE_ID,
  seq,
  timestamp,
  gasValue,
  pm1,
  pm25,
  pm10
);

  xbee.listen();
  xbee.println(payload);

  Serial.print("[TX #");
  Serial.print(seq);
  Serial.print("] ");
  Serial.println(payload);

  if (!pmsValid) {
    Serial.println("[WARN] PMS7003 frame not available or invalid");
  }

  unsigned long elapsed = millis() - cycleStart;

  if (elapsed < SEND_INTERVAL_MS) {
    delay(SEND_INTERVAL_MS - elapsed);
  }
}
