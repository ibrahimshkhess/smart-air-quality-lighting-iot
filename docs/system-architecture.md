# System Architecture

## Overview

The project implements a heterogeneous IoT system for air-quality monitoring, air-quality prediction, and energy-aware smart lighting.

The system combines multiple communication technologies:

- ZigBee / IEEE 802.15.4
- LoRaWAN
- Wi-Fi
- MQTT

The architecture is divided into two main subsystems:

1. Air-Quality Monitoring and Prediction
2. Energy-Aware Smart Lighting

---

## 1. Air-Quality Monitoring

Two Arduino-based sensor nodes, S1 and S2, collect environmental measurements.

Each node contains:

- MQ-135 gas sensor
- PMS7003 particulate-matter sensor
- XBee IEEE 802.15.4 module

The collected measurements include:

- MQ-135 raw gas value
- PM1
- PM2.5
- PM10

Each sensor node also attaches:

- Node ID
- Sequence number
- Sensor timestamp

The nodes transmit their measurements to a central Arduino master through ZigBee / IEEE 802.15.4.

### Sensor Network Flow

```text
S1 ──ZigBee──┐
             ├──> Arduino Master
S2 ──ZigBee──┘
```

S1 transmits approximately every 30 seconds.

S2 uses the same interval with an initial 15-second offset to reduce simultaneous transmissions.

---

## 2. ZigBee-to-LoRaWAN Master

The Arduino master acts as a protocol bridge between the local ZigBee sensor network and the LoRaWAN network.

Its main functions are:

- Receive packets from S1 and S2
- Validate and parse incoming sensor packets
- Buffer received packets using a FIFO queue
- Encode measurements into an 18-byte binary payload
- Forward the payload through LoRaWAN

### Binary Payload

The LoRaWAN payload contains:

```text
Version
Node ID
Sequence Number
Sensor Timestamp
Gas Value
PM1
PM2.5
PM10
```

The compact binary representation reduces LoRaWAN payload size compared with transmitting the original ASCII sensor message.

---

## 3. LoRaWAN Communication

The master communicates with the LoRaWAN gateway using:

- Region: EU433
- Frequency: 433.175 MHz
- Bandwidth: 125 kHz
- Spreading Factor: SF7
- Activation: ABP

The experimental setup uses a single-channel LG01-N gateway.

The Arduino master is therefore configured to use the channel corresponding to 433.175 MHz.

```text
Arduino Master
      |
   LoRaWAN
      |
      v
LG01-N Gateway
```

---

## 4. Gateway and ChirpStack

The LG01-N gateway forwards LoRaWAN packets over IP to ChirpStack running on a Raspberry Pi.

The gateway communicates with the ChirpStack Gateway Bridge using UDP port 1700.

The Raspberry Pi runs the main network services using Docker:

- ChirpStack
- ChirpStack Gateway Bridge
- Mosquitto
- PostgreSQL
- Redis

### Network Path

```text
Arduino Master
      |
   LoRaWAN
      |
      v
LG01-N Gateway
      |
  UDP / IP
      |
      v
Gateway Bridge
      |
     MQTT
      |
      v
ChirpStack
```

---

## 5. MQTT and Data Logging

After decoding the 18-byte payload, ChirpStack publishes uplink events through MQTT.

The Raspberry Pi logger subscribes to:

```text
application/+/device/+/event/up
```

The Python logger stores measurements and network-performance information in CSV files.

Recorded network metrics include:

- LoRaWAN frame counter
- RSSI
- SNR
- Frequency
- Spreading factor
- Bandwidth
- Sequence-number gaps
- Duplicate packets
- Out-of-order packets
- Node reboots

These measurements are used to evaluate Packet Delivery Ratio (PDR) and communication reliability.

---

## 6. Air-Quality Prediction

Collected particulate-matter measurements will be used by the machine-learning component for time-series air-quality prediction.

The prediction subsystem is designed to use historical sensor measurements to estimate future air-quality conditions.

---

## 7. Energy-Aware Smart Lighting

The second subsystem uses ESP32 devices connected through Wi-Fi.

The planned sensing and control components include:

- BH1750 light sensor
- ESP32
- LED / smart-light actuator
- Energy-source monitoring and control

The lighting subsystem will implement energy-aware control logic based on ambient light conditions and available energy.

---

## Complete Architecture

```text
                AIR-QUALITY SUBSYSTEM

     MQ-135 + PMS7003          MQ-135 + PMS7003
            |                         |
           S1                        S2
            |                         |
            +------ ZigBee -----------+
                       |
                       v
                Arduino Master
                       |
                    LoRaWAN
                 433.175 MHz
                       |
                       v
                 LG01-N Gateway
                       |
                    UDP/IP
                       |
                       v
                  Raspberry Pi
                       |
        +--------------+--------------+
        |              |              |
   Gateway Bridge  ChirpStack     Mosquitto
                       |
                       v
                 MQTT Data Logger
                       |
                       v
                     CSV
                       |
                       v
               ML / Prediction


               SMART-LIGHTING SUBSYSTEM

             BH1750 / Energy Data
                       |
                       v
                     ESP32
                       |
                     Wi-Fi
                       |
                       v
              Control / Monitoring
                       |
                       v
                Lighting Actuator
```

## Technologies

| Layer | Technology |
|---|---|
| Sensor network | ZigBee / IEEE 802.15.4 |
| Long-range communication | LoRaWAN EU433 |
| Gateway backhaul | IP |
| Application messaging | MQTT |
| Lighting network | Wi-Fi |
| Network server | ChirpStack |
| Edge platform | Raspberry Pi |
| Data storage | CSV |
| Intelligence | Time-series prediction and energy-aware control |
