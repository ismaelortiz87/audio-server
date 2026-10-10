#!/usr/bin/env python3
# L1 latency probe. Runs INSIDE a test container (python3 + pulseaudio-utils or
# pipewire-pulse), stdlib only. See tests/latency/run.sh.
#
#   probe.py listen <pulse-source> <seconds> [threshold] [stop-after-n-bursts]
#       Records <pulse-source> with parec at 5 ms latency and prints one JSON
#       line per tone burst: {"t": <epoch onset>, "len_ms": .., "gaps": ..,
#       "peak": ..}. A "gap" is a silent run of >= 1 ms inside a burst (a
#       dropout: a sine at the test level is never that quiet). The onset time
#       is the wall clock of the parec read minus the samples still after the
#       onset in that read, so two listeners in containers on one Docker VM
#       (one kernel clock) can be compared directly.
#   probe.py burst <out.raw> <count> <gap_s> <burst_ms> <hz> [tail_s]
#       Writes s16le mono 48 kHz: <count> x (gap of silence, burst of sine at
#       -6 dBFS), then <tail_s> s of continuous sine (dropout check), then 1 s
#       of silence.
import json
import math
import struct
import subprocess
import sys
import time

RATE = 48000


def burst(path, count, gap_s, burst_ms, hz, tail_s=0.0):
    amp = 0.5 * 32767
    out = bytearray()
    phase = 0
    def silence(sec):
        out.extend(b"\0\0" * int(sec * RATE))
    def tone(sec):
        nonlocal phase
        n = int(sec * RATE)
        out.extend(struct.pack("<%dh" % n, *(int(amp * math.sin(2 * math.pi * hz * (phase + i) / RATE)) for i in range(n))))
        phase += n
    for _ in range(count):
        silence(gap_s)
        tone(burst_ms / 1000.0)
    if tail_s > 0:
        silence(gap_s)
        tone(tail_s)
    silence(1.0)
    with open(path, "wb") as f:
        f.write(out)


def listen(dev, seconds, thr, want=0):
    thr_i = int(thr * 32768)
    seen = 0
    chunk = 240                     # 5 ms
    p = subprocess.Popen(["parec", "-d", dev, "--raw", "--format=s16le", "--rate=%d" % RATE,
                          "--channels=1", "--latency-msec=5"], stdout=subprocess.PIPE)
    end = time.time() + seconds
    quiet_run = 10 ** 9             # samples since the last loud one
    in_burst = False
    b = None
    end_quiet = int(0.05 * RATE)    # 50 ms of silence ends a burst
    gap_min = int(0.001 * RATE)
    try:
        while time.time() < end:
            data = p.stdout.read(chunk * 2)
            if not data:
                break
            now = time.time()
            n = len(data) // 2
            samples = struct.unpack("<%dh" % n, data[:n * 2])
            for i, s in enumerate(samples):
                loud = s > thr_i or s < -thr_i
                if loud:
                    if not in_burst:
                        in_burst = True
                        b = {"t": now - (n - i) / RATE, "n": 0, "gaps": 0, "peak": 0}
                    elif quiet_run >= gap_min:
                        b["gaps"] += 1
                    quiet_run = 0
                    a = abs(s)
                    if a > b["peak"]:
                        b["peak"] = a
                else:
                    quiet_run += 1
                    if in_burst and quiet_run >= end_quiet:
                        in_burst = False
                        b["len_ms"] = round((b["n"] - quiet_run) * 1000.0 / RATE, 1)
                        print(json.dumps({"t": round(b["t"], 4), "len_ms": b["len_ms"], "gaps": b["gaps"],
                                          "peak": round(b["peak"] / 32768.0, 3)}), flush=True)
                        seen += 1
                if in_burst:
                    b["n"] += 1
            if want and seen >= want:
                break
    finally:
        p.kill()
        p.wait()


if __name__ == "__main__":
    cmd = sys.argv[1] if len(sys.argv) > 1 else ""
    if cmd == "listen":
        listen(sys.argv[2], float(sys.argv[3]), float(sys.argv[4]) if len(sys.argv) > 4 else 0.03,
               int(sys.argv[5]) if len(sys.argv) > 5 else 0)
    elif cmd == "burst":
        burst(sys.argv[2], int(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5]), float(sys.argv[6]),
              float(sys.argv[7]) if len(sys.argv) > 7 else 0.0)
    else:
        sys.exit(__doc__ or "usage: probe.py listen|burst ...")
