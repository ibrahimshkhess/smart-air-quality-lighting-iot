# Air-Quality Network Experiments

This directory contains experimental data used to evaluate the reliability and radio performance of the air-quality communication chain.

## Communication Path

```text
Sensor Node (S1/S2)
→ ZigBee / IEEE 802.15.4
→ Arduino Master
→ LoRaWAN
→ LG01-N Gateway
→ ChirpStack
→ MQTT
→ Raspberry Pi Logger
```

## Baseline Test

The baseline test file is:

```text
test/TEST_20260829_154634.csv
```

The test was performed using:

- LoRaWAN region: EU433
- Frequency: 433.175 MHz
- Bandwidth: 125 kHz
- Spreading Factor: SF7
- Two sensor nodes: S1 and S2
- Approximately 30 s sampling interval per node
- S2 transmission offset: 15 s

## Baseline Results

The test contained 40 MQTT uplinks:

- S1: 20 received packets
- S2: 20 received packets
- Missing sequence numbers: 0
- Duplicate application packets: 0
- Out-of-order packets: 0
- Node reboots: 0
- Packet Delivery Ratio (PDR): 100% for both nodes
- LoRaWAN FCnt range: 0–39
- Missing FCnt values: 0
- Duplicate FCnt values: 0

Approximate average link-quality values:

- S1 RSSI: -56 dBm
- S2 RSSI: -55.5 dBm
- S1 SNR: 9.7 dB
- S2 SNR: 9.4 dB

## Logged Metrics

Each uplink contains:

- Sequence number
- Sensor timestamp
- Gas sensor raw value
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
- Event classification

These metrics are used for network performance evaluation and packet-loss analysis.
