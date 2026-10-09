#!/usr/bin/env python3
"""WebSocket audio bridge for the containerised SonoBus.

Browsers cannot reach the AOO transport (raw UDP) and noVNC is video-only, so
this process shuttles PCM between PulseAudio and the browser:

    downlink   sonobus.monitor --parec--> /ws --> AudioWorklet --> speakers
    uplink     microphone --> AudioWorklet --> /ws --pacat--> sonobusmic

Both directions use raw interleaved signed 16-bit little-endian stereo at
48 kHz, which is what the engine runs at internally.

Downlink is captured once and fanned out to every connected client. Uplink is
one `pacat` per client, each writing into the same null sink; PulseAudio mixes
them, so several browsers can talk at once.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
import os
import signal
import sys
from pathlib import Path

from aiohttp import WSMsgType, web

SAMPLE_RATE = 48_000
CHANNELS = 2
BYTES_PER_SAMPLE = 2

# 20 ms of audio per WebSocket frame: the usual sweet spot between header
# overhead and per-frame latency.
FRAME_SAMPLES = SAMPLE_RATE // 50
FRAME_BYTES = FRAME_SAMPLES * CHANNELS * BYTES_PER_SAMPLE

# The speaker output of the engine.
SINK_NAME = os.environ.get("SONOBUS_SINK", "sonobus")

# The virtual microphone the engine records from.
MIC_NAME = os.environ.get("SONOBUS_MIC", "sonobusmic")

HERE = Path(__file__).resolve().parent
WEB_DIR = HERE.parent / "web"

# noVNC's own assets, served from the same origin so one port is enough.
NOVNC_DIR = Path(os.environ.get("NOVNC_DIR", "/usr/share/novnc"))

# Where x11vnc listens; the browser's VNC websocket is relayed here.
VNC_HOST = os.environ.get("VNC_HOST", "127.0.0.1")
VNC_PORT = int(os.environ.get("VNC_PORT", "5900"))

log = logging.getLogger("bridge")

# Fan-out targets. Each is a bounded queue so a slow browser cannot make the
# capture loop lag or grow memory without bound.
subscribers: set[asyncio.Queue[bytes]] = set()


def _fmt_args() -> list[str]:
    return [
        f"--format=s16le",
        f"--rate={SAMPLE_RATE}",
        f"--channels={CHANNELS}",
    ]


def parec_command() -> list[str]:
    return [
        "parec",
        f"--device={SINK_NAME}.monitor",
        *_fmt_args(),
        # Keep PulseAudio's own buffer small so capture stays close to realtime.
        "--latency-msec=20",
        "--raw",
    ]


def pacat_command() -> list[str]:
    return [
        "pacat",
        f"--device={MIC_NAME}",
        *_fmt_args(),
        "--latency-msec=20",
        "--raw",
    ]


def broadcast(chunk: bytes) -> None:
    """Push a chunk to every client, dropping the oldest data if one is behind."""
    for queue in list(subscribers):
        if queue.full():
            with contextlib.suppress(asyncio.QueueEmpty):
                queue.get_nowait()
        with contextlib.suppress(asyncio.QueueFull):
            queue.put_nowait(chunk)


async def _terminate(proc: asyncio.subprocess.Process) -> None:
    if proc.returncode is not None:
        return
    proc.terminate()
    try:
        await asyncio.wait_for(proc.wait(), timeout=2)
    except asyncio.TimeoutError:
        proc.kill()
        with contextlib.suppress(Exception):
            await proc.wait()


async def capture_loop() -> None:
    """Read the engine's monitor source and fan it out, restarting if it dies."""
    while True:
        if not subscribers:
            await asyncio.sleep(0.2)
            continue

        log.info("starting capture: %s", " ".join(parec_command()))
        proc = await asyncio.create_subprocess_exec(
            *parec_command(),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.DEVNULL,
        )
        try:
            while subscribers and proc.returncode is None:
                # A timeout here means the source went quiet; loop and re-check
                # whether we still have clients.
                try:
                    chunk = await asyncio.wait_for(
                        proc.stdout.readexactly(FRAME_BYTES), timeout=5
                    )
                except asyncio.TimeoutError:
                    continue
                broadcast(chunk)
        except (asyncio.IncompleteReadError, ConnectionResetError):
            log.warning("capture stream ended")
        except asyncio.CancelledError:
            await _terminate(proc)
            raise
        except Exception:
            log.exception("capture loop error")
        finally:
            await _terminate(proc)

        await asyncio.sleep(0.5)


async def websocket_handler(request: web.Request) -> web.WebSocketResponse:
    ws = web.WebSocketResponse(max_msg_size=0)
    await ws.prepare(request)

    queue: asyncio.Queue[bytes] = asyncio.Queue(maxsize=64)
    subscribers.add(queue)
    peer = request.remote
    log.info("client connected: %s (%d total)", peer, len(subscribers))

    pacat: asyncio.subprocess.Process | None = None

    async def ensure_pacat() -> asyncio.subprocess.Process:
        nonlocal pacat
        if pacat is None or pacat.returncode is not None:
            pacat = await asyncio.create_subprocess_exec(
                *pacat_command(),
                stdin=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.DEVNULL,
            )
        return pacat

    async def pump_down() -> None:
        while True:
            chunk = await queue.get()
            await ws.send_bytes(chunk)

    down_task = asyncio.create_task(pump_down())

    try:
        async for msg in ws:
            if msg.type == WSMsgType.BINARY:
                proc = await ensure_pacat()
                if proc.stdin is not None:
                    proc.stdin.write(msg.data)
                    # Drain so the pipe does not stall the event loop.
                    with contextlib.suppress(Exception):
                        await proc.stdin.drain()
            elif msg.type == WSMsgType.ERROR:
                log.warning("websocket error: %s", ws.exception())
    finally:
        down_task.cancel()
        with contextlib.suppress(asyncio.CancelledError):
            await down_task
        subscribers.discard(queue)
        if pacat is not None:
            await _terminate(pacat)
        log.info("client disconnected: %s (%d left)", peer, len(subscribers))

    return ws


async def index_handler(request: web.Request) -> web.StreamResponse:
    return web.FileResponse(WEB_DIR / "index.html")


async def vnc_websocket_handler(request: web.Request) -> web.WebSocketResponse:
    """Relay the browser's VNC websocket to x11vnc.

    noVNC speaks the RFB protocol over a websocket; x11vnc only speaks raw TCP,
    so this pumps bytes between the two. Keeping it here means the whole app is
    reachable on a single port.
    """
    ws = web.WebSocketResponse(max_msg_size=0)
    await ws.prepare(request)

    try:
        reader, writer = await asyncio.open_connection(VNC_HOST, VNC_PORT)
    except OSError as exc:
        log.warning("cannot reach x11vnc at %s:%d: %s", VNC_HOST, VNC_PORT, exc)
        await ws.close(code=1011, message=b"vnc backend unavailable")
        return ws

    async def ws_to_tcp() -> None:
        async for msg in ws:
            if msg.type == WSMsgType.BINARY:
                writer.write(msg.data)
                await writer.drain()
            elif msg.type == WSMsgType.ERROR:
                break

    async def tcp_to_ws() -> None:
        while True:
            data = await reader.read(65536)
            if not data:
                break
            await ws.send_bytes(data)

    tasks = [asyncio.create_task(ws_to_tcp()), asyncio.create_task(tcp_to_ws())]
    try:
        await asyncio.wait(tasks, return_when=asyncio.FIRST_COMPLETED)
    finally:
        for task in tasks:
            task.cancel()
        for task in tasks:
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await task
        writer.close()
        with contextlib.suppress(Exception):
            await writer.wait_closed()
        with contextlib.suppress(Exception):
            await ws.close()

    return ws


async def vnc_page_handler(request: web.Request) -> web.StreamResponse:
    """noVNC's UI, with its default websocket path pointed at our relay."""
    path = request.match_info.get("path", "vnc.html")
    candidate = NOVNC_DIR / path
    if not candidate.is_file():
        candidate = NOVNC_DIR / "vnc.html"
    return web.FileResponse(candidate)


async def health_handler(request: web.Request) -> web.StreamResponse:
    return web.json_response(
        {
            "ok": True,
            "clients": len(subscribers),
            "sampleRate": SAMPLE_RATE,
            "channels": CHANNELS,
        }
    )


def build_app() -> web.Application:
    app = web.Application()
    app.router.add_get("/", index_handler)
    app.router.add_get("/health", health_handler)
    app.router.add_get("/ws", websocket_handler)
    app.router.add_get("/websockify", vnc_websocket_handler)

    # Serves index.html's siblings: the AudioWorklet modules.
    app.router.add_static("/static/", WEB_DIR, show_index=False)

    # noVNC assets. /vnc.html is served with its default path overridden so the
    # page connects to /websockify on this same origin.
    if NOVNC_DIR.is_dir():
        app.router.add_get("/vnc.html", vnc_page_handler)
        app.router.add_get("/vnc_lite.html", vnc_page_handler)
        app.router.add_static("/", NOVNC_DIR, show_index=False)
    else:
        log.warning("noVNC assets not found at %s; GUI view disabled", NOVNC_DIR)

    return app


async def main() -> None:
    logging.basicConfig(
        level=logging.INFO,
        format="[bridge] %(message)s",
        stream=sys.stdout,
    )

    app = build_app()
    capture_task = asyncio.create_task(capture_loop())

    runner = web.AppRunner(app)
    await runner.setup()
    port = int(os.environ.get("BRIDGE_PORT", "6081"))
    site = web.TCPSite(runner, "0.0.0.0", port)
    await site.start()
    log.info("audio bridge listening on :%d", port)

    stop = asyncio.Event()
    loop = asyncio.get_running_loop()
    for sig in (signal.SIGINT, signal.SIGTERM):
        with contextlib.suppress(NotImplementedError):
            loop.add_signal_handler(sig, stop.set)
    await stop.wait()

    log.info("shutting down")
    capture_task.cancel()
    with contextlib.suppress(asyncio.CancelledError):
        await capture_task
    await runner.cleanup()


if __name__ == "__main__":
    with contextlib.suppress(KeyboardInterrupt):
        asyncio.run(main())
