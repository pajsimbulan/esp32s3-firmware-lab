#!/usr/bin/env python3
"""Lab 23 factory test station. Drives one DUT, records the result, exits with a code."""
import sys, csv, time, argparse, pathlib, datetime
import serial                                   # pip install pyserial

PROMPT   = b"TESTMODE\n"
REQUIRED_TESTS = {"FLASH", "PSRAM", "IMU_PRESENT", "IMU_SANE", "PROVISION"}
TIMEOUT  = 30.0
LOG      = pathlib.Path("factory_log.csv")


def run_dut(port, baud=115200):
    """Returns (verdict, serial_number, [(name, result), ...])."""
    tests, serial_no, verdict = [], "", "FAIL"
    with serial.Serial(port, baud, timeout=1.0) as s:
        s.reset_input_buffer()
        s.dtr = False; s.rts = True; time.sleep(0.1)   # pulse reset
        s.rts = False

        # Wait for the DUT to announce it is listening, rather than guessing a
        # delay. The old version slept 0.3 s and hoped; if NVS or I2C bring-up
        # ran long, the token was sent into the void and the test "timed out"
        # on a perfectly good board.
        ready_by = time.time() + 10.0
        while time.time() < ready_by:
            if s.readline().decode("utf-8", "replace").strip() == "FACTORY_READY":
                break
        else:
            return "NO_READY", "", []

        s.write(PROMPT); s.flush()

        deadline = time.time() + TIMEOUT
        while time.time() < deadline:
            line = s.readline().decode("utf-8", "replace").strip()
            if not line:
                continue
            if line.startswith("TEST "):
                parts = line.split()
                if len(parts) >= 3:
                    tests.append((parts[1], parts[2]))
            elif line.startswith("SERIAL="):
                serial_no = line.split("=", 1)[1]
            elif line.startswith("RESULT "):
                parts = line.split()
                if len(parts) < 2:
                    continue                     # malformed line, keep listening
                verdict = parts[1]
                # A verdict is only trustworthy if the checks we require were
                # actually reported. A truncated or partially-flashed DUT can
                # print RESULT PASS having run almost nothing.
                ran = {n for n, _ in tests}
                if verdict == "PASS" and not REQUIRED_TESTS.issubset(ran):
                    missing = ",".join(sorted(REQUIRED_TESTS - ran))
                    print(f"  !! PASS claimed but these never ran: {missing}")
                    verdict = "INCOMPLETE"
                return verdict, serial_no, tests
    return "TIMEOUT", serial_no, tests


def record(verdict, serial_no, tests):
    new = not LOG.exists()
    with LOG.open("a", newline="") as f:
        w = csv.writer(f)
        if new:
            w.writerow(["timestamp", "serial", "verdict", "failed_tests"])
        failed = ";".join(n for n, r in tests if r != "PASS")
        w.writerow([datetime.datetime.now().isoformat(timespec="seconds"),
                    serial_no, verdict, failed])


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("port")
    a = ap.parse_args()
    try:
        verdict, serial_no, tests = run_dut(a.port)
    except Exception as e:
        print(f"FIXTURE ERROR: {e}", file=sys.stderr)
        sys.exit(2)                              # 2 = fixture fault, not a bad board

    for name, res in tests:
        mark = "ok  " if res == "PASS" else "FAIL"
        print(f"  [{mark}] {name}")
    print(f"\n  serial : {serial_no or '(none)'}")
    print(f"  RESULT : {verdict}")
    record(verdict, serial_no, tests)

    # Exit codes are read by the line controller, so the distinction matters.
    #   0 = the board passed          -> ship it
    #   1 = the board FAILED A CHECK  -> route to rework
    #   2 = the FIXTURE is suspect    -> stop the line, do not scrap boards
    #
    # TIMEOUT and NO_READY both mean "the DUT never told us anything". That is
    # far more often a cable, a port or a power problem than a bad board, and
    # scoring it as a board failure is how a broken fixture quietly scraps a
    # batch of good units.
    if verdict == "PASS":                    sys.exit(0)
    if verdict in ("NO_READY", "TIMEOUT"):   sys.exit(2)
    if verdict == "INCOMPLETE":              sys.exit(1)   # treat as a failed board
    sys.exit(1)