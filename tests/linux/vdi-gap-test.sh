#!/bin/bash
# Audio-continuity test, run ON a VDI (as the desktop user). Isolated: its own four
# null sinks, a throwaway group on the public server, API ports 7081/7082; the real
# agent and the user's devices are not touched. A test agent captures a continuous
# 440 Hz tone, a test Console on the same host receives it, and the recording is
# checked in 5 ms windows for gaps.
#   tests/linux/vdi-gap-test.sh <crosspoint binary> <label> [buffer]
# First VDI (graph locked at quantum 1024): buffer 256 -> 30-36% silent, 570-631
# gaps in 23 s (before 369ea89c); 512/1024 -> ~0.1-0.2%, 1-6 gaps.
set -u; BIN=$1; L=$2; BUF=${3:-256}; W=/tmp/xp-gap/$L; rm -rf $W; mkdir -p $W/a $W/p; chmod 700 $W
G=xp-gaptest-$RANDOM$RANDOM
mods=""; for n in gt_src gt_agent_out gt_probe_in gt_probe_out; do mods="$mods $(pactl load-module module-null-sink sink_name=$n sink_properties=device.description=$n)"; done
printf "server: aoo.sonobus.net:10998\ngroup: $G\nusername: gt-agent\nrole: vdi\naudio:\n  input_device: \"gt_src.monitor\"\n  output_device: \"gt_agent_out\"\n  sample_rate: 48000\n  buffer: $BUF\n" > $W/a/vdi.yaml
printf "server: aoo.sonobus.net:10998\ngroup: $G\nusername: gt-console\nrole: console\naudio:\n  input_device: \"gt_probe_in.monitor\"\n  output_device: \"gt_probe_out\"\n" > $W/p/c.yaml
python3 -c "
import math,struct,sys
sr=48000; f=440.0; n=sr*60
sys.stdout.buffer.write(b\"\".join(struct.pack(\"<hh\",int(8000*math.sin(2*math.pi*f*i/sr)),int(8000*math.sin(2*math.pi*f*i/sr))) for i in range(n)))" > $W/tone.raw
pacat --playback -d gt_src --format=s16le --rate=48000 --channels=2 $W/tone.raw & TONE=$!
HOME=$W/a PIPEWIRE_ALSA="{ stream.capture.sink = true }" $BIN --headless --config $W/a/vdi.yaml --api-port 7081 > $W/a/log 2>&1 & A=$!
HOME=$W/p PIPEWIRE_ALSA="{ stream.capture.sink = true }" $BIN --headless --config $W/p/c.yaml --api-port 7082 --dump-peers $W/p/peers.json > $W/p/log 2>&1 & P=$!
ok=0; for i in $(seq 40); do sleep 1; python3 -c "
import json,sys;d=json.load(open(\"$W/p/peers.json\"))
sys.exit(0 if any(p[\"name\"]==\"gt-agent\" and p.get(\"recvActive\") and p[\"preFaderPeakDb\"]>-40 for p in d[\"peers\"]) else 1)" 2>/dev/null && { ok=1; break; }; done
echo "peered=$ok after ${i}s"
sleep 3
timeout 25 parecord -d gt_probe_out.monitor --format=s16le --rate=48000 --channels=1 --raw $W/rec.raw
kill $TONE $A $P 2>/dev/null; wait 2>/dev/null
for m in $mods; do pactl unload-module $m; done
python3 - $W/rec.raw <<"PY"
import struct,sys,math
d=open(sys.argv[1],"rb").read(); n=len(d)//2; a=struct.unpack("<%dh"%n,d[:2*n])
w=240  # 5 ms windows
r=[math.sqrt(sum(x*x for x in a[i:i+w])/w) for i in range(0,n-w,w)]
if not r: print("no audio"); sys.exit()
med=sorted(r)[len(r)//2]
low=[i for i,v in enumerate(r) if v<0.3*med]
gaps=0; prev=-5
for i in low:
    if i!=prev+1: gaps+=1
    prev=i
print("seconds=%.1f median_rms=%.0f low_windows=%d (%.2f%%) gaps=%d" % (n/48000, med, len(low), 100.0*len(low)/len(r), gaps))
PY
