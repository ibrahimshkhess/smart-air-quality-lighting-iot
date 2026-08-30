# Raspberry Pi Data Logger

This directory contains the Raspberry Pi software used to collect and store air-quality uplinks received through ChirpStack.

## Data Flow

```text
ZigBee sensor nodes → ZigBee/LoRaWAN master → LG01-N gateway → ChirpStack → MQTT → Raspberry Pi logger
```

## Logger

The `mqtt_logger.py` script subscribes to the ChirpStack MQTT uplink topic:

```text
application/+/device/+/event/up
```

The logger reads MQTT messages through the Mosquitto container using Docker Compose.

## Run Types

At startup, the logger allows selecting:

- TEST
- EXPERIMENT

Test sessions are stored in:

```text
~/air_quality/data/test/
```

Experiment sessions are stored in:

```text
~/air_quality/data/raw/
```

Experiment files are automatically numbered using the format:

```text
EXP001_YYYYMMDD_HHMMSS.csv
```

## Recorded Parameters

Each received uplink is stored with:

- Sensor node ID
- Boot ID
- Sequence number
- Sensor timestamp
- MQ-135 raw gas reading
- PM1
- PM2.5
- PM10
- LoRaWAN frame counter
- RSSI
- SNR
- Frequency
- Spreading factor
- Bandwidth
- Gateway ID
- Packet event classification

## Reliability Monitoring

The logger detects:

- Missing sequence numbers
- Duplicate packets
- Out-of-order packets
- Sensor-node reboots
- LoRaWAN frame-counter duplicates
- LoRaWAN frame-counter resets

For each sensor node, it also calculates Packet Delivery Ratio (PDR), average RSSI, and average SNR.

## Requirements

- Raspberry Pi
- Docker
- Docker Compose
- ChirpStack
- Mosquitto MQTT broker
- Python 3

No external Python MQTT library is required because the script uses `mosquitto_sub` from the running Mosquitto Docker container.
