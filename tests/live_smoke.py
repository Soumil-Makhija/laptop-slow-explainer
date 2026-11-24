"""Native macOS integration check; never prints real process names or PIDs."""
import json
from pathlib import Path
import subprocess
import sys
import time

if sys.platform != "darwin":
    raise SystemExit("Live smoke requires macOS")
binary = Path(__file__).resolve().parents[1] / "build" / "slow-explain"
# One CPU-bound worker with a hard lifetime bound, no disk or network operations.
worker = subprocess.Popen([sys.executable, "-u", "-c", """
import time
print('READY', flush=True)
end = time.monotonic() + 12
while time.monotonic() < end:
    pass
"""], text=True, stdout=subprocess.PIPE)
try:
    assert worker.stdout.readline().strip() == "READY"
    run = subprocess.run([str(binary), "--seconds", "2", "--json"],
                         capture_output=True, text=True, timeout=10)
    if run.returncode:
        raise SystemExit("Native collection failed: " + run.stderr.strip())
    report = json.loads(run.stdout)
    assert report["source"] == "macos-native"
    assert 1.8 <= report["seconds"] <= 8
    assert report["cpu_count"] >= 1
    assert report["memory_bytes"] > 0
    assert report["busy_pct"] is not None and 0 <= report["busy_pct"] <= 100
    row = next((p for p in report["processes"] if p["pid"] == worker.pid), None)
    assert row is not None, "CPU worker missing; process access may be restricted"
    assert row["cpu_pct"] is not None and 10 <= row["cpu_pct"] <= 130, "CPU timebase or collection check failed"
    assert row["footprint_bytes"] > 0
    assert report["coverage"]["processes"] > 0
    print("Native macOS smoke passed: CPU worker attributed, system counters and memory verified.")
finally:
    if worker.poll() is None:
        worker.terminate()
    worker.wait(timeout=5)
    worker.stdout.close()
