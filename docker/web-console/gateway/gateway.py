#!/usr/bin/env python3
"""Crosspoint WebRTC Opus audio gateway (P7.3).

Bridges PulseAudio <-> one WebRTC peer (the browser) using GStreamer webrtcbin.

  engine_out.monitor -> opusenc -> rtpopuspay -> webrtcbin  (downlink, stereo)
  webrtcbin -> rtpopusdepay -> opusdec -> pulsesink engine_in (uplink, mono)

Signalling: POST /rtc/offer {"sdp": ..., "type": "offer"} -> {"sdp": ..., "type": "answer"}
Non-trickle: the answer is returned once ICE gathering is complete.
"""
import asyncio
import json
import os
import re
import threading
import time

import gi

gi.require_version("Gst", "1.0")
gi.require_version("GstSdp", "1.0")
gi.require_version("GstWebRTC", "1.0")
from gi.repository import GLib, Gst, GstSdp, GstWebRTC  # noqa: E402
from aiohttp import web  # noqa: E402

Gst.init(None)

ENV = os.environ.get
HTTP_PORT = int(ENV("HTTP_PORT", "8090"))
UDP_MIN = int(ENV("RTC_UDP_MIN", "40000"))
UDP_MAX = int(ENV("RTC_UDP_MAX", "40019"))
PUBLIC_IP = ENV("RTC_PUBLIC_IP", "").strip()
TURN_URL = ENV("RTC_TURN_URL", "").strip()  # e.g. turn:host:3478?transport=udp
TURN_USER = ENV("RTC_TURN_USER", "")
TURN_PASS = ENV("RTC_TURN_PASS", "")
SRC_DEVICE = ENV("RTC_SRC_DEVICE", "engine_out.monitor")
SINK_DEVICE = ENV("RTC_SINK_DEVICE", "engine_in")
FRAME_MS = ENV("OPUS_FRAME_MS", "10")  # 2.5, 5, 10, 20, 40, 60
BITRATE = int(ENV("OPUS_BITRATE", "128000"))
JITTER_MS = int(ENV("RTC_JITTER_MS", "30"))
PA_BUFFER_US = int(ENV("PA_BUFFER_US", "40000"))
PA_LATENCY_US = int(ENV("PA_LATENCY_US", "10000"))
GATHER_TIMEOUT = float(ENV("RTC_GATHER_TIMEOUT", "5"))
OPUS_PT = 111  # matches what Chrome/Firefox/Safari offer
HERE = os.path.dirname(os.path.abspath(__file__))


def log(*a):
    print("[gateway]", *a, flush=True)


# --------------------------------------------------------------------------
# GLib thread: all GStreamer work happens here.
# --------------------------------------------------------------------------
glib_loop = GLib.MainLoop()
threading.Thread(target=glib_loop.run, daemon=True).start()


def in_glib(fn, *args):
    """Run fn in the GLib thread, return an asyncio future for its result."""
    aloop = asyncio.get_running_loop()
    fut = aloop.create_future()

    def done(ok, val):
        if fut.done():
            return
        (fut.set_result if ok else fut.set_exception)(val)

    def run():
        try:
            r = fn(*args)
            # fn may return a callable-registering "async" via Deferred
            if isinstance(r, Deferred):
                r.bind(lambda ok, v: aloop.call_soon_threadsafe(done, ok, v))
            else:
                aloop.call_soon_threadsafe(done, True, r)
        except Exception as e:  # noqa: BLE001
            aloop.call_soon_threadsafe(done, False, e)
        return False

    GLib.idle_add(run)
    return fut


class Deferred:
    def __init__(self):
        self.cb = None
        self.res = None

    def bind(self, cb):
        self.cb = cb
        if self.res:
            cb(*self.res)

    def resolve(self, ok, val):
        self.res = (ok, val)
        if self.cb:
            self.cb(ok, val)


# --------------------------------------------------------------------------
# Session
# --------------------------------------------------------------------------
class Session:
    def __init__(self):
        self.pipe = None
        self.webrtc = None
        self.started = time.time()
        self.state = "new"
        self.closed = False

    def build(self):
        fms = FRAME_MS
        p = Gst.Pipeline.new("rtc")
        self.pipe = p
        w = Gst.ElementFactory.make("webrtcbin", "webrtc")
        w.set_property("bundle-policy", GstWebRTC.WebRTCBundlePolicy.MAX_BUNDLE)
        w.set_property("latency", JITTER_MS)
        if TURN_URL:
            m = re.match(r"^(turns?):(.*)$", TURN_URL)
            if m:
                from urllib.parse import quote
                w.set_property(
                    "turn-server",
                    f"{m.group(1)}://{quote(TURN_USER, safe='')}:{quote(TURN_PASS, safe='')}@{m.group(2)}")
        ice = self.ice = w.get_property("ice-agent")  # keep a reference
        for prop, val in (("min-rtp-port", UDP_MIN), ("max-rtp-port", UDP_MAX)):
            try:
                ice.set_property(prop, val)
            except Exception as e:  # noqa: BLE001
                log("cannot set", prop, e)
        self.webrtc = w
        p.add(w)

        # Downlink branch. A sink pad is requested BEFORE the remote offer so
        # that webrtcbin matches it to the offer's audio m-line.
        launch = (
            f'pulsesrc device={SRC_DEVICE} buffer-time={PA_BUFFER_US} latency-time={PA_LATENCY_US} '
            f'provide-clock=false do-timestamp=true ! audio/x-raw,rate=48000,channels=2 ! '
            f'audioconvert ! queue max-size-buffers=3 leaky=downstream ! '
            f'opusenc bitrate={BITRATE} frame-size={fms} audio-type=restricted-lowdelay '
            f'inband-fec=true packet-loss-percentage=5 complexity=5 ! '
            f'rtpopuspay pt={OPUS_PT}')
        down = Gst.parse_bin_from_description(launch, True)
        p.add(down)
        sinkpad = w.request_pad_simple("sink_%u")
        down.get_static_pad("src").link(sinkpad)
        for t in [w.emit("get-transceiver", 0)]:
            t.set_property("direction", GstWebRTC.WebRTCRTPTransceiverDirection.SENDRECV)
            t.set_property("do-nack", True)
            t.set_property("fec-type", GstWebRTC.WebRTCFECType.NONE)

        w.connect("pad-added", self.on_pad_added)
        w.connect("notify::connection-state", self.on_state)
        w.connect("notify::ice-connection-state", self.on_state)
        p.get_bus().add_signal_watch()
        p.get_bus().connect("message::error", lambda b, m: log("pipeline error:", m.parse_error()))
        p.set_state(Gst.State.PLAYING)

    def on_state(self, w, _pspec):
        cs = w.get_property("connection-state").value_nick
        ics = w.get_property("ice-connection-state").value_nick
        self.state = cs
        log(f"connection-state={cs} ice={ics}")

    def on_pad_added(self, w, pad):
        if pad.direction != Gst.PadDirection.SRC:
            return
        caps = pad.get_current_caps() or pad.query_caps(None)
        log("remote pad", pad.get_name(), caps.to_string() if caps else "?")
        b = Gst.parse_bin_from_description(
            f'rtpopusdepay ! opusdec plc=true use-inband-fec=true ! audioconvert ! audioresample ! '
            f'audio/x-raw,rate=48000,channels=2 ! queue max-size-buffers=4 leaky=downstream ! '
            f'pulsesink device={SINK_DEVICE} buffer-time={PA_BUFFER_US} latency-time={PA_LATENCY_US} '
            f'sync=false', True)
        self.pipe.add(b)
        b.sync_state_with_parent()
        pad.link(b.get_static_pad("sink"))

    def negotiate(self, offer_sdp, d: Deferred):
        res, msg = self._parse(offer_sdp)
        if res != GstSdp.SDPResult.OK:
            d.resolve(False, ValueError("bad SDP"))
            return
        offer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.OFFER, msg)
        w = self.webrtc

        def fail(e):
            d.resolve(False, RuntimeError(e))

        def on_local_set(promise, _):
            promise.wait()
            self.wait_gather(d)

        def on_answer(promise, _):
            promise.wait()
            reply = promise.get_reply()
            ans = reply.get_value("answer")
            if ans is None:
                return fail("create-answer failed")
            w.emit("set-local-description", ans, Gst.Promise.new_with_change_func(on_local_set, None))

        def on_remote_set(promise, _):
            promise.wait()
            w.emit("create-answer", None, Gst.Promise.new_with_change_func(on_answer, None))

        w.emit("set-remote-description", offer, Gst.Promise.new_with_change_func(on_remote_set, None))

    @staticmethod
    def _parse(text):
        res, msg = GstSdp.SDPMessage.new()
        r = GstSdp.sdp_message_parse_buffer(text.encode(), msg)
        return r, msg

    def wait_gather(self, d: Deferred):
        w = self.webrtc
        done = {"v": False}

        def finish(*_):
            if done["v"]:
                return False
            done["v"] = True
            sdp = w.get_property("local-description").sdp.as_text()
            d.resolve(True, {"type": "answer", "sdp": munge_answer(sdp)})
            return False

        def on_gather(*_):
            if w.get_property("ice-gathering-state") == GstWebRTC.WebRTCICEGatheringState.COMPLETE:
                GLib.idle_add(finish)

        w.connect("notify::ice-gathering-state", on_gather)
        GLib.timeout_add(int(GATHER_TIMEOUT * 1000), finish)
        on_gather()

    def close(self):
        if self.closed:
            return
        self.closed = True
        if self.pipe:
            self.pipe.set_state(Gst.State.NULL)
            self.pipe = None
            self.webrtc = None

    def stats(self):
        d = Deferred()
        w = self.webrtc
        if not w:
            return {}

        def cb(promise, _):
            promise.wait()
            d.resolve(True, struct_to_dict(promise.get_reply()))

        w.emit("get-stats", None, Gst.Promise.new_with_change_func(cb, None))
        return d


def struct_to_dict(s):
    out = {}
    for i in range(s.n_fields()):
        k = s.nth_field_name(i)
        try:
            v = s.get_value(k)
        except TypeError:
            out[k] = None  # unsupported GValue type (e.g. GstValueList)
            continue
        if isinstance(v, Gst.Structure):
            v = struct_to_dict(v)
        elif isinstance(v, (int, float, str, bool)) or v is None:
            pass
        elif hasattr(v, "value_nick"):
            v = v.value_nick
        else:
            try:
                v = float(v)
            except Exception:  # noqa: BLE001
                v = str(v)
        out[k] = v
    return out


CAND_RE = re.compile(r"^a=candidate:(\S+) (\d+) (\S+) (\d+) (\S+) (\d+) typ host(.*)$")


def munge_answer(sdp):
    """Advertise RTC_PUBLIC_IP as the only host candidate address; ask for
    stereo + FEC in the fmtp."""
    lines = sdp.replace("\r\n", "\n").split("\n")
    out, seen = [], set()
    for ln in lines:
        if PUBLIC_IP:
            m = CAND_RE.match(ln)
            if m and m.group(3).lower() == "udp":
                f, comp, proto, prio, addr, port, rest = m.groups()
                if ":" in addr and ":" not in PUBLIC_IP:
                    continue  # drop IPv6 host candidates
                key = (comp, port)
                if key in seen:
                    continue
                seen.add(key)
                ln = f"a=candidate:{f} {comp} {proto} {prio} {PUBLIC_IP} {port} typ host{rest}"
            elif ln.startswith("c=IN IP4 ") or ln.startswith("c=IN IP6 "):
                ln = f"c=IN {'IP6' if ':' in PUBLIC_IP else 'IP4'} {PUBLIC_IP}"
        if ln.startswith("a=fmtp:") and "useinbandfec" in ln and "stereo" not in ln:
            ln += ";stereo=1;sprop-stereo=1"
        out.append(ln)
    return "\r\n".join(out)


# --------------------------------------------------------------------------
# HTTP
# --------------------------------------------------------------------------
current = {"s": None}


def start_session(offer_sdp):
    old = current["s"]
    if old:
        log("replacing previous session")
        old.close()
    s = Session()
    current["s"] = s
    s.build()
    d = Deferred()
    s.negotiate(offer_sdp, d)
    return d


async def offer(request):
    try:
        body = await request.json()
        sdp = body["sdp"]
    except Exception:  # noqa: BLE001
        return web.json_response({"error": "expected JSON {sdp,type:'offer'}"}, status=400)
    try:
        ans = await asyncio.wait_for(in_glib(start_session, sdp), GATHER_TIMEOUT + 10)
    except Exception as e:  # noqa: BLE001
        log("offer failed:", repr(e))
        return web.json_response({"error": str(e)}, status=500)
    return web.json_response(ans)


async def close(request):
    s = current["s"]
    if s:
        await in_glib(s.close)
        current["s"] = None
    return web.json_response({"ok": True})


async def stats(request):
    s = current["s"]
    if not s or not s.webrtc:
        return web.json_response({"active": False})
    try:
        raw = await asyncio.wait_for(in_glib(s.stats), 3)
    except Exception as e:  # noqa: BLE001
        return web.json_response({"active": True, "error": str(e)})
    summary = {"active": True, "state": s.state, "uptime_s": round(time.time() - s.started, 1),
               "inbound": [], "outbound": [], "remote_inbound": [], "remote_outbound": [], "raw": raw}
    for k, v in raw.items():
        if not isinstance(v, dict):
            continue
        t = v.get("type")
        t = getattr(t, "value_nick", t)
        keymap = {"inbound-rtp": "inbound", "outbound-rtp": "outbound",
                  "remote-inbound-rtp": "remote_inbound", "remote-outbound-rtp": "remote_outbound"}
        if t in keymap:
            summary[keymap[t]].append(v)
        elif t == "candidate-pair" and v.get("nominated"):
            summary["rtt_s"] = v.get("current-round-trip-time")
    if summary.get("rtt_s") is None and summary["remote_inbound"]:
        summary["rtt_s"] = summary["remote_inbound"][0].get("round-trip-time")
    return web.json_response(summary)


async def config(request):
    ice = []
    if TURN_URL:
        ice.append({"urls": [TURN_URL], "username": TURN_USER, "credential": TURN_PASS})
    return web.json_response({"iceServers": ice, "udpRange": [UDP_MIN, UDP_MAX], "frameMs": FRAME_MS})


async def test_page(request):
    return web.FileResponse(os.path.join(HERE, "test.html"))


async def health(request):
    return web.json_response({"ok": True})


def main():
    app = web.Application()
    app.add_routes([
        web.post("/rtc/offer", offer), web.post("/rtc/close", close),
        web.get("/rtc/stats", stats), web.get("/rtc/config", config),
        web.get("/rtc/test", test_page), web.get("/rtc/health", health),
    ])
    log(f"http :{HTTP_PORT} udp {UDP_MIN}-{UDP_MAX} public_ip={PUBLIC_IP or 'auto'} "
        f"opus {FRAME_MS}ms {BITRATE}bps jitter {JITTER_MS}ms turn={'yes' if TURN_URL else 'no'}")
    web.run_app(app, host="0.0.0.0", port=HTTP_PORT, print=None)


if __name__ == "__main__":
    main()
