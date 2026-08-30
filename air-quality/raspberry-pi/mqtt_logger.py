import csv
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path


BASE_DIR = Path.home() / "air_quality"
DATA_DIR = BASE_DIR / "data"
RAW_DIR = DATA_DIR / "raw"
TEST_DIR = DATA_DIR / "test"

CHIRPSTACK_DIR = Path.home() / "chirpstack-docker"

MQTT_TOPIC = "application/+/device/+/event/up"


CSV_FIELDS = [
    "run_type",
    "session_id",
    "server_time",
    "node",
    "boot_id",
    "seq",
    "sensor_ts_ms",
    "gas",
    "pm1",
    "pm25",
    "pm10",
    "fcnt",
    "rssi",
    "snr",
    "frequency",
    "sf",
    "bandwidth",
    "gateway_id",
    "event",
    "is_duplicate",
    "missing_before",
]


def get_next_experiment_number():
    RAW_DIR.mkdir(parents=True, exist_ok=True)

    numbers = []

    for file in RAW_DIR.glob("EXP*_*.csv"):
        try:
            prefix = file.stem.split("_")[0]
            number = int(prefix.replace("EXP", ""))
            numbers.append(number)
        except ValueError:
            continue

    return max(numbers, default=0) + 1


def select_run_type():
    while True:
        print()
        print("Select run type:")
        print("1 - TEST")
        print("2 - EXPERIMENT")

        choice = input("Choice: ").strip()

        if choice == "1":
            return "TEST"

        if choice == "2":
            return "EXPERIMENT"

        print("[ERROR] Please enter 1 or 2.")


def create_session(run_type):
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")

    if run_type == "TEST":
        TEST_DIR.mkdir(parents=True, exist_ok=True)

        session_id = f"TEST_{timestamp}"
        csv_file = TEST_DIR / f"{session_id}.csv"

    else:
        RAW_DIR.mkdir(parents=True, exist_ok=True)

        experiment_number = get_next_experiment_number()

        session_id = (
            f"EXP{experiment_number:03d}_{timestamp}"
        )

        csv_file = RAW_DIR / f"{session_id}.csv"

    return session_id, csv_file


def create_epoch():
    return {
        "first_seq": None,
        "last_seq": None,
        "seen_seq": set(),
    }


def create_node_stats():
    return {
        "total_uplinks": 0,
        "unique_readings": 0,
        "duplicates": 0,
        "reboots": 0,
        "out_of_order": 0,

        "boot_id": 1,

        "last_seq": None,
        "last_ts": None,

        "seen_keys": set(),

        "epochs": {
            1: create_epoch()
        },

        "rssi_sum": 0.0,
        "rssi_count": 0,

        "snr_sum": 0.0,
        "snr_count": 0,
    }


RUN_TYPE = None
SESSION_ID = None
CSV_FILE = None
START_TIME = None

stats = {
    "S1": create_node_stats(),
    "S2": create_node_stats(),
}

seen_fcnt = set()
duplicate_fcnt = 0
total_fcnt_events = 0
last_fcnt = None
fcnt_resets = 0


def initialize_csv():
    CSV_FILE.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    with CSV_FILE.open(
        "w",
        newline=""
    ) as file:

        writer = csv.DictWriter(
            file,
            fieldnames=CSV_FIELDS
        )

        writer.writeheader()


def start_mqtt_listener():
    command = [
        "docker",
        "compose",
        "exec",
        "-T",
        "mosquitto",
        "mosquitto_sub",
        "-h",
        "localhost",
        "-t",
        MQTT_TOPIC,
    ]

    return subprocess.Popen(
        command,
        cwd=CHIRPSTACK_DIR,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        bufsize=1,
    )


def update_fcnt(fcnt):
    global duplicate_fcnt
    global total_fcnt_events
    global last_fcnt
    global fcnt_resets

    if not isinstance(fcnt, int):
        return

    total_fcnt_events += 1

    if fcnt in seen_fcnt:
        duplicate_fcnt += 1
    else:
        if (
            last_fcnt is not None
            and fcnt < last_fcnt
        ):
            fcnt_resets += 1

        seen_fcnt.add(fcnt)

    last_fcnt = fcnt


def get_missing_for_epoch(epoch):
    first_seq = epoch["first_seq"]
    last_seq = epoch["last_seq"]

    if first_seq is None or last_seq is None:
        return 0, 0, 0.0

    expected = last_seq - first_seq + 1

    if expected <= 0:
        return 0, 0, 0.0

    received = len(epoch["seen_seq"])
    missing = max(expected - received, 0)

    pdr = (
        received / expected
    ) * 100.0

    return expected, missing, pdr


def process_message(line):
    try:
        message = json.loads(line)

    except json.JSONDecodeError:
        print("[ERROR] Invalid MQTT JSON payload")
        return

    data = message.get("object", {})

    if not isinstance(data, dict) or not data:
        print("[SKIP] No decoded sensor object")
        return

    node = str(
        data.get(
            "node",
            "UNKNOWN"
        )
    )

    if node not in stats:
        print(
            f"[SKIP] Unknown node: {node}"
        )
        return

    try:
        seq = int(data["seq"])

    except (
        KeyError,
        TypeError,
        ValueError
    ):
        print(
            f"[SKIP] Invalid SEQ from {node}"
        )
        return

    try:
        sensor_ts = int(
            data.get("ts_ms", 0)
        )

    except (
        TypeError,
        ValueError
    ):
        sensor_ts = 0

    rx_info = message.get(
        "rxInfo",
        []
    )

    rx = (
        rx_info[0]
        if rx_info
        else {}
    )

    tx_info = message.get(
        "txInfo",
        {}
    )

    modulation = tx_info.get(
        "modulation",
        {}
    )

    lora = modulation.get(
        "lora",
        {}
    )

    rssi = rx.get(
        "rssi",
        ""
    )

    snr = rx.get(
        "snr",
        ""
    )

    fcnt = message.get(
        "fCnt",
        ""
    )

    # FCnt is processed before application duplicate detection.
    update_fcnt(fcnt)

    node_stats = stats[node]
    node_stats["total_uplinks"] += 1

    boot_id = node_stats["boot_id"]

    current_key = (
        boot_id,
        seq,
        sensor_ts
    )

    event = "NORMAL"
    is_duplicate = 0
    missing_before = 0

    # Exact same reading within the same boot period.
    if current_key in node_stats["seen_keys"]:
        is_duplicate = 1
        event = "DUPLICATE"

        node_stats["duplicates"] += 1

    else:
        last_seq = node_stats["last_seq"]
        last_ts = node_stats["last_ts"]

        # SEQ and millis() both moving backwards indicate node reboot.
        reboot_detected = (
            last_seq is not None
            and last_ts is not None
            and seq < last_seq
            and sensor_ts < last_ts
        )

        if reboot_detected:
            node_stats["reboots"] += 1
            node_stats["boot_id"] += 1

            boot_id = node_stats["boot_id"]

            node_stats["epochs"][
                boot_id
            ] = create_epoch()

            event = "REBOOT"

            current_key = (
                boot_id,
                seq,
                sensor_ts
            )

            node_stats["last_seq"] = None
            node_stats["last_ts"] = None

        epoch = node_stats["epochs"][
            boot_id
        ]

        previous_seq = epoch["last_seq"]

        if (
            previous_seq is not None
            and seq > previous_seq + 1
        ):
            missing_before = (
                seq - previous_seq - 1
            )

            if event == "NORMAL":
                event = "SEQ_GAP"

        elif (
            previous_seq is not None
            and seq < previous_seq
            and event != "REBOOT"
        ):
            node_stats["out_of_order"] += 1
            event = "OUT_OF_ORDER"

        if seq in epoch["seen_seq"]:
            if event == "NORMAL":
                event = "SEQ_REUSE"

        node_stats["seen_keys"].add(
            current_key
        )

        node_stats["unique_readings"] += 1

        epoch["seen_seq"].add(seq)

        if epoch["first_seq"] is None:
            epoch["first_seq"] = seq

        if (
            epoch["last_seq"] is None
            or seq > epoch["last_seq"]
        ):
            epoch["last_seq"] = seq

        node_stats["last_seq"] = seq
        node_stats["last_ts"] = sensor_ts

    if isinstance(
        rssi,
        (int, float)
    ):
        node_stats["rssi_sum"] += float(rssi)
        node_stats["rssi_count"] += 1

    if isinstance(
        snr,
        (int, float)
    ):
        node_stats["snr_sum"] += float(snr)
        node_stats["snr_count"] += 1

    server_time = (
        message.get("time")
        or message.get("nsTime")
        or datetime.now(
            timezone.utc
        ).isoformat()
    )

    row = {
        "run_type": RUN_TYPE,
        "session_id": SESSION_ID,
        "server_time": server_time,

        "node": node,
        "boot_id": boot_id,

        "seq": seq,
        "sensor_ts_ms": sensor_ts,

        "gas": data.get(
            "gas",
            ""
        ),

        "pm1": data.get(
            "pm1",
            ""
        ),

        "pm25": data.get(
            "pm25",
            ""
        ),

        "pm10": data.get(
            "pm10",
            ""
        ),

        "fcnt": fcnt,

        "rssi": rssi,
        "snr": snr,

        "frequency": tx_info.get(
            "frequency",
            ""
        ),

        "sf": lora.get(
            "spreadingFactor",
            ""
        ),

        "bandwidth": lora.get(
            "bandwidth",
            ""
        ),

        "gateway_id": rx.get(
            "gatewayId",
            ""
        ),

        "event": event,
        "is_duplicate": is_duplicate,
        "missing_before": missing_before,
    }

    with CSV_FILE.open(
        "a",
        newline=""
    ) as file:

        writer = csv.DictWriter(
            file,
            fieldnames=CSV_FIELDS
        )

        writer.writerow(row)

    print(
        f"[RX] "
        f"{node} "
        f"BOOT={boot_id} "
        f"SEQ={seq} "
        f"TS={sensor_ts} "
        f"GAS={row['gas']} "
        f"PM1={row['pm1']} "
        f"PM25={row['pm25']} "
        f"PM10={row['pm10']} "
        f"RSSI={rssi} "
        f"SNR={snr} "
        f"FCnt={fcnt} "
        f"EVENT={event}"
    )


def print_summary():
    end_time = datetime.now()

    duration = (
        end_time
        - START_TIME
    )

    print()
    print("========================================")
    print(" SESSION SUMMARY")
    print("========================================")

    print(
        f"Run type   : {RUN_TYPE}"
    )

    print(
        f"Session ID : {SESSION_ID}"
    )

    print(
        f"File       : {CSV_FILE}"
    )

    print(
        f"Duration   : "
        f"{str(duration).split('.')[0]}"
    )

    print()

    for node in ("S1", "S2"):

        s = stats[node]

        avg_rssi = (
            s["rssi_sum"]
            / s["rssi_count"]
            if s["rssi_count"]
            else None
        )

        avg_snr = (
            s["snr_sum"]
            / s["snr_count"]
            if s["snr_count"]
            else None
        )

        print(f"{node}:")

        print(
            f"  Total uplinks    : "
            f"{s['total_uplinks']}"
        )

        print(
            f"  Unique readings  : "
            f"{s['unique_readings']}"
        )

        print(
            f"  Duplicates       : "
            f"{s['duplicates']}"
        )

        print(
            f"  Reboots detected : "
            f"{s['reboots']}"
        )

        print(
            f"  Boot periods     : "
            f"{len(s['epochs'])}"
        )

        print(
            f"  Out-of-order     : "
            f"{s['out_of_order']}"
        )

        if avg_rssi is None:
            print(
                "  Average RSSI     : N/A"
            )
        else:
            print(
                f"  Average RSSI     : "
                f"{avg_rssi:.2f} dBm"
            )

        if avg_snr is None:
            print(
                "  Average SNR      : N/A"
            )
        else:
            print(
                f"  Average SNR      : "
                f"{avg_snr:.2f} dB"
            )

        for boot_id in sorted(
            s["epochs"]
        ):
            epoch = s["epochs"][
                boot_id
            ]

            expected, missing, pdr = (
                get_missing_for_epoch(
                    epoch
                )
            )

            print(
                f"  Boot #{boot_id}:"
            )

            print(
                f"    SEQ range      : "
                f"{epoch['first_seq']} "
                f"-> "
                f"{epoch['last_seq']}"
            )

            print(
                f"    Received       : "
                f"{len(epoch['seen_seq'])}"
            )

            print(
                f"    Expected       : "
                f"{expected}"
            )

            print(
                f"    Missing SEQ    : "
                f"{missing}"
            )

            print(
                f"    PDR            : "
                f"{pdr:.2f}%"
            )

        print()

    print("LoRaWAN FCnt:")

    print(
        f"  MQTT uplinks     : "
        f"{total_fcnt_events}"
    )

    if seen_fcnt:

        min_fcnt = min(
            seen_fcnt
        )

        max_fcnt = max(
            seen_fcnt
        )

        print(
            f"  Unique FCnt      : "
            f"{len(seen_fcnt)}"
        )

        print(
            f"  Range            : "
            f"{min_fcnt} -> {max_fcnt}"
        )

        print(
            f"  Duplicate FCnt   : "
            f"{duplicate_fcnt}"
        )

        print(
            f"  FCnt resets      : "
            f"{fcnt_resets}"
        )

        if fcnt_resets == 0:

            expected_fcnt = set(
                range(
                    min_fcnt,
                    max_fcnt + 1
                )
            )

            missing_fcnt = sorted(
                expected_fcnt
                - seen_fcnt
            )

            print(
                f"  Missing FCnt     : "
                f"{len(missing_fcnt)}"
            )

            if missing_fcnt:

                preview = (
                    missing_fcnt[:20]
                )

                print(
                    f"  Missing values   : "
                    f"{preview}"
                )

                if len(
                    missing_fcnt
                ) > 20:

                    print(
                        "  Missing values   : ..."
                    )

        else:
            print(
                "  Missing FCnt     : "
                "not calculated across FCnt reset"
            )

    else:
        print(
            "  No FCnt received"
        )

    print(
        "========================================"
    )


def main():
    global RUN_TYPE
    global SESSION_ID
    global CSV_FILE
    global START_TIME

    print(
        "========================================"
    )

    print(
        " AIR QUALITY MQTT LOGGER"
    )

    print(
        " ZigBee -> LoRaWAN -> ChirpStack -> Pi"
    )

    print(
        "========================================"
    )

    RUN_TYPE = select_run_type()

    SESSION_ID, CSV_FILE = (
        create_session(
            RUN_TYPE
        )
    )

    START_TIME = datetime.now()

    initialize_csv()

    print()

    print(
        f"[MODE] {RUN_TYPE}"
    )

    print(
        f"[SESSION] {SESSION_ID}"
    )

    print(
        f"[FILE] {CSV_FILE}"
    )

    print()

    print(
        "[MQTT] Starting Mosquitto subscriber..."
    )

    process = start_mqtt_listener()

    try:
        for line in process.stdout:

            line = line.strip()

            if line:
                process_message(
                    line
                )

    except KeyboardInterrupt:

        print()

        print(
            "[STOP] Logger stopped by user"
        )

    except FileNotFoundError:

        print()

        print(
            "[ERROR] Docker command not found."
        )

    except Exception as error:

        print()

        print(
            f"[ERROR] {error}"
        )

    finally:

        if process.poll() is None:

            process.terminate()

            try:
                process.wait(
                    timeout=3
                )

            except subprocess.TimeoutExpired:

                process.kill()

        if START_TIME is not None:

            print_summary()


if __name__ == "__main__":
    main()
