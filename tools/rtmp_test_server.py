#!/usr/bin/env python3
"""A tiny RTMP ingest server for testing Abyss Stream locally.

It accepts a publisher (Abyss Stream, OBS, ffmpeg), checks the stream key,
and reports what actually arrived: the H.264 resolution and profile from the
AVC sequence header, the AAC sample rate and channels, frame and keyframe
counts, bytes and bitrate. Nothing is sent to the internet, and no third-party
packages are needed.

    python tools/rtmp_test_server.py --port 1935 --key test-key
    # point a destination at rtmp://127.0.0.1:1935/live with key "test-key"

Options:
    --record FILE.flv     also write what was received to an FLV file
    --stats FILE.json     write a JSON summary of every session on exit/disconnect
    --drop-after SECONDS  close the connection once, after this long, to test
                          the publisher's automatic reconnect

This implements just enough of RTMP for a publisher: the simple handshake,
chunk streams, AMF0 commands (connect, createStream, publish) and media
messages. It is a test tool, not a media server.
"""

from __future__ import annotations

import argparse
import asyncio
import json
import os
import struct
import sys
import time
from dataclasses import dataclass, field

HANDSHAKE_SIZE = 1536
OUR_CHUNK_SIZE = 4096
WINDOW_ACK_SIZE = 2_500_000

# Message type ids
SET_CHUNK_SIZE = 1
ABORT = 2
ACK = 3
USER_CONTROL = 4
WINDOW_ACK = 5
PEER_BANDWIDTH = 6
AUDIO = 8
VIDEO = 9
DATA_AMF0 = 18
COMMAND_AMF0 = 20


# --------------------------------------------------------------- AMF0 -----

class AmfReader:
    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def done(self) -> bool:
        return self.pos >= len(self.data)

    def _take(self, n: int) -> bytes:
        chunk = self.data[self.pos:self.pos + n]
        if len(chunk) != n:
            raise ValueError("truncated AMF0 value")
        self.pos += n
        return chunk

    def _string(self, long: bool = False) -> str:
        (length,) = struct.unpack(">I" if long else ">H", self._take(4 if long else 2))
        return self._take(length).decode("utf-8", "replace")

    def _pairs(self) -> dict:
        result = {}
        while True:
            key = self._string()
            if key == "" and self.data[self.pos:self.pos + 1] == b"\x09":
                self.pos += 1
                return result
            result[key] = self.read()

    def read(self):
        marker = self._take(1)[0]
        if marker == 0x00:
            return struct.unpack(">d", self._take(8))[0]
        if marker == 0x01:
            return self._take(1)[0] != 0
        if marker == 0x02:
            return self._string()
        if marker == 0x03:
            return self._pairs()
        if marker in (0x05, 0x06):
            return None
        if marker == 0x08:
            self._take(4)  # ECMA array count hint
            return self._pairs()
        if marker == 0x0A:
            (count,) = struct.unpack(">I", self._take(4))
            return [self.read() for _ in range(count)]
        if marker == 0x0B:
            self._take(10)  # date: double + timezone
            return None
        if marker == 0x0C:
            return self._string(long=True)
        raise ValueError(f"unsupported AMF0 marker 0x{marker:02x}")


def amf_encode(value) -> bytes:
    if value is None:
        return b"\x05"
    if isinstance(value, bool):
        return b"\x01" + (b"\x01" if value else b"\x00")
    if isinstance(value, (int, float)):
        return b"\x00" + struct.pack(">d", float(value))
    if isinstance(value, str):
        raw = value.encode("utf-8")
        return b"\x02" + struct.pack(">H", len(raw)) + raw
    if isinstance(value, dict):
        out = b"\x03"
        for key, item in value.items():
            raw = key.encode("utf-8")
            out += struct.pack(">H", len(raw)) + raw + amf_encode(item)
        return out + b"\x00\x00\x09"
    raise TypeError(f"cannot AMF0-encode {type(value)!r}")


# ------------------------------------------------- H.264 / AAC parsing -----

class BitReader:
    def __init__(self, data: bytes):
        self.data = data
        self.bit = 0

    def u(self, n: int) -> int:
        value = 0
        for _ in range(n):
            byte = self.data[self.bit >> 3]
            value = (value << 1) | ((byte >> (7 - (self.bit & 7))) & 1)
            self.bit += 1
        return value

    def ue(self) -> int:
        zeros = 0
        while self.u(1) == 0:
            zeros += 1
        return (1 << zeros) - 1 + (self.u(zeros) if zeros else 0)

    def se(self) -> int:
        value = self.ue()
        return (value + 1) // 2 if value & 1 else -(value // 2)


def parse_sps(sps: bytes) -> dict:
    """Width, height, profile and level from an H.264 sequence parameter set."""
    rbsp = bytearray()
    zeros = 0
    for byte in sps[1:]:  # skip the NAL header
        if zeros >= 2 and byte == 3:
            zeros = 0
            continue  # emulation prevention byte
        rbsp.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    r = BitReader(bytes(rbsp))
    profile = r.u(8)
    r.u(8)  # constraint flags
    level = r.u(8)
    r.ue()  # seq_parameter_set_id
    chroma_format = 1
    if profile in (100, 110, 122, 244, 44, 83, 86, 118, 128, 138, 139, 134, 135):
        chroma_format = r.ue()
        if chroma_format == 3:
            r.u(1)
        r.ue()
        r.ue()
        r.u(1)
        if r.u(1):  # seq_scaling_matrix_present_flag
            for i in range(8 if chroma_format != 3 else 12):
                if r.u(1):
                    last, nxt = 8, 8
                    for _ in range(16 if i < 6 else 64):
                        if nxt:
                            nxt = (last + r.se() + 256) % 256
                        last = nxt or last
    r.ue()  # log2_max_frame_num_minus4
    poc_type = r.ue()
    if poc_type == 0:
        r.ue()
    elif poc_type == 1:
        r.u(1)
        r.se()
        r.se()
        for _ in range(r.ue()):
            r.se()
    r.ue()  # max_num_ref_frames
    r.u(1)
    width_mbs = r.ue() + 1
    height_units = r.ue() + 1
    frame_mbs_only = r.u(1)
    if not frame_mbs_only:
        r.u(1)
    r.u(1)
    crop = [0, 0, 0, 0]
    if r.u(1):
        crop = [r.ue(), r.ue(), r.ue(), r.ue()]
    crop_x = 2 if chroma_format in (1, 2) else 1
    crop_y = (2 if chroma_format == 1 else 1) * (2 - frame_mbs_only)
    width = width_mbs * 16 - crop_x * (crop[0] + crop[1])
    height = (2 - frame_mbs_only) * height_units * 16 - crop_y * (crop[2] + crop[3])
    names = {66: "Baseline", 77: "Main", 100: "High"}
    return {"width": width, "height": height, "profile": names.get(profile, str(profile)), "level": level / 10}


AAC_RATES = [96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350]


def parse_audio_specific_config(config: bytes) -> dict:
    r = BitReader(config)
    object_type = r.u(5)
    rate_index = r.u(4)
    rate = r.u(24) if rate_index == 15 else AAC_RATES[rate_index] if rate_index < len(AAC_RATES) else 0
    channels = r.u(4)
    return {"objectType": object_type, "sampleRate": rate, "channels": channels}


# ------------------------------------------------------------- session -----

@dataclass
class Session:
    peer: str
    started: float = field(default_factory=time.monotonic)
    app: str = ""
    stream_name: str = ""
    key_ok: bool | None = None
    publishing: bool = False
    bytes_in: int = 0
    media_bytes: int = 0
    video_frames: int = 0
    keyframes: int = 0
    audio_frames: int = 0
    first_ts: int | None = None
    last_ts: int = 0
    video: dict = field(default_factory=dict)
    audio: dict = field(default_factory=dict)
    metadata: dict = field(default_factory=dict)
    closed_by: str = ""

    def summary(self) -> dict:
        duration = (self.last_ts - (self.first_ts or 0)) / 1000 if self.first_ts is not None else 0.0
        return {
            "peer": self.peer,
            "app": self.app,
            "keyAccepted": self.key_ok,
            "publishing": self.publishing,
            "video": self.video,
            "audio": self.audio,
            "videoFrames": self.video_frames,
            "keyframes": self.keyframes,
            "audioFrames": self.audio_frames,
            "mediaSeconds": round(duration, 2),
            "kbps": round(self.media_bytes * 8 / 1000 / duration) if duration > 0 else 0,
            "encoder": self.metadata.get("encoder", ""),
            "closedBy": self.closed_by,
        }


class FlvWriter:
    def __init__(self, path: str):
        self.file = open(path, "wb")
        self.file.write(b"FLV\x01\x05\x00\x00\x00\x09" + b"\x00\x00\x00\x00")

    def write(self, tag_type: int, timestamp: int, payload: bytes) -> None:
        ts = timestamp & 0xFFFFFFFF
        header = struct.pack(">B", tag_type) + len(payload).to_bytes(3, "big")
        header += (ts & 0xFFFFFF).to_bytes(3, "big") + bytes([(ts >> 24) & 0xFF]) + b"\x00\x00\x00"
        self.file.write(header + payload + struct.pack(">I", len(header) + len(payload)))

    def close(self) -> None:
        self.file.close()


class RtmpConnection:
    def __init__(self, server: "TestServer", reader, writer):
        self.server = server
        self.reader = reader
        self.writer = writer
        self.session = Session(peer="%s:%d" % writer.get_extra_info("peername")[:2])
        self.in_chunk_size = 128
        self.out_chunk_size = 128
        self.headers: dict[int, dict] = {}   # last header per chunk stream
        self.partial: dict[int, bytearray] = {}
        self.ack_window = 0
        self.acked = 0

    def log(self, text: str) -> None:
        print(f"[{self.server.port}] {self.session.peer}: {text}", flush=True)

    # -- sending
    def send(self, csid: int, msg_type: int, payload: bytes, stream_id: int = 0, timestamp: int = 0) -> None:
        out = bytearray()
        header = bytes([csid & 0x3F]) + timestamp.to_bytes(3, "big") + len(payload).to_bytes(3, "big")
        header += bytes([msg_type]) + struct.pack("<I", stream_id)
        out += header
        for offset in range(0, len(payload), self.out_chunk_size):
            if offset:
                out.append(0xC0 | (csid & 0x3F))  # type 3 continuation chunk
            out += payload[offset:offset + self.out_chunk_size]
        self.writer.write(bytes(out))

    def send_command(self, *values, stream_id: int = 0) -> None:
        self.send(3, COMMAND_AMF0, b"".join(amf_encode(v) for v in values), stream_id)

    # -- receiving
    async def read_exact(self, n: int) -> bytes:
        data = await self.reader.readexactly(n)
        self.session.bytes_in += n
        if self.ack_window and self.session.bytes_in - self.acked >= self.ack_window:
            self.acked = self.session.bytes_in
            self.send(2, ACK, struct.pack(">I", self.acked & 0xFFFFFFFF))
        return data

    async def handshake(self) -> None:
        c0 = await self.read_exact(1)
        if c0[0] != 3:
            raise ConnectionError(f"unsupported RTMP version {c0[0]}")
        c1 = await self.read_exact(HANDSHAKE_SIZE)
        # Zero version bytes in S1 tell librtmp to use the simple handshake.
        s1 = struct.pack(">I", 0) + b"\x00" * 4 + os.urandom(HANDSHAKE_SIZE - 8)
        self.writer.write(b"\x03" + s1 + c1)  # S0 + S1 + S2 (echo of C1)
        await self.writer.drain()
        await self.read_exact(HANDSHAKE_SIZE)  # C2

    async def read_message(self):
        first = (await self.read_exact(1))[0]
        fmt = first >> 6
        csid = first & 0x3F
        if csid == 0:
            csid = 64 + (await self.read_exact(1))[0]
        elif csid == 1:
            b = await self.read_exact(2)
            csid = 64 + b[0] + b[1] * 256

        header = dict(self.headers.get(csid, {"timestamp": 0, "delta": 0, "length": 0, "type": 0, "stream": 0, "extended": False}))
        if fmt <= 2:
            raw_ts = int.from_bytes(await self.read_exact(3), "big")
            if fmt <= 1:
                header["length"] = int.from_bytes(await self.read_exact(3), "big")
                header["type"] = (await self.read_exact(1))[0]
            if fmt == 0:
                header["stream"] = struct.unpack("<I", await self.read_exact(4))[0]
            header["extended"] = raw_ts == 0xFFFFFF
            if header["extended"]:
                raw_ts = struct.unpack(">I", await self.read_exact(4))[0]
            if fmt == 0:
                header["timestamp"] = raw_ts
                header["delta"] = 0
            else:
                header["delta"] = raw_ts
        elif header["extended"]:
            await self.read_exact(4)  # repeated extended timestamp on type-3 chunks

        buffer = self.partial.setdefault(csid, bytearray())
        if not buffer and fmt != 0:
            header["timestamp"] += header["delta"]
        self.headers[csid] = header

        need = header["length"] - len(buffer)
        buffer += await self.read_exact(min(need, self.in_chunk_size))
        if len(buffer) < header["length"]:
            return None
        payload = bytes(buffer)
        self.partial[csid] = bytearray()
        return header["type"], header["stream"], header["timestamp"], payload

    # -- message handling
    def on_command(self, payload: bytes, stream_id: int) -> bool:
        amf = AmfReader(payload)
        name = amf.read()
        txn = amf.read() if not amf.done() else 0
        args = []
        while not amf.done():
            args.append(amf.read())

        if name == "connect":
            obj = args[0] if args and isinstance(args[0], dict) else {}
            self.session.app = str(obj.get("app", ""))
            self.send(2, WINDOW_ACK, struct.pack(">I", WINDOW_ACK_SIZE))
            self.send(2, PEER_BANDWIDTH, struct.pack(">IB", WINDOW_ACK_SIZE, 2))
            self.send(2, SET_CHUNK_SIZE, struct.pack(">I", OUR_CHUNK_SIZE))
            self.out_chunk_size = OUR_CHUNK_SIZE
            self.send_command("_result", txn, {"fmsVer": "FMS/3,0,1,123", "capabilities": 31},
                              {"level": "status", "code": "NetConnection.Connect.Success",
                               "description": "Connection succeeded.", "objectEncoding": 0})
            self.log(f"connect app='{self.session.app}'")
        elif name in ("releaseStream", "FCPublish"):
            self.send_command("_result", txn, None, None)
        elif name == "createStream":
            self.send_command("_result", txn, None, 1)
        elif name == "publish":
            stream_name = str(args[1]) if len(args) > 1 else ""
            self.session.stream_name = stream_name
            expected = self.server.key
            self.session.key_ok = expected is None or stream_name == expected
            if not self.session.key_ok:
                self.log("publish REJECTED: wrong stream key")
                self.send_command("onStatus", 0, None,
                                  {"level": "error", "code": "NetStream.Publish.BadName",
                                   "description": "Stream key rejected by test server."}, stream_id=stream_id)
                self.session.closed_by = "server (bad key)"
                return False
            self.session.publishing = True
            self.send(2, USER_CONTROL, struct.pack(">HI", 0, 1))  # StreamBegin
            self.send_command("onStatus", 0, None,
                              {"level": "status", "code": "NetStream.Publish.Start",
                               "description": "Publishing."}, stream_id=stream_id)
            self.log("publish accepted")
        elif name in ("FCUnpublish", "deleteStream", "closeStream"):
            self.log(f"{name}")
            self.session.closed_by = "client"
        return True

    def on_media(self, msg_type: int, timestamp: int, payload: bytes) -> None:
        s = self.session
        if s.first_ts is None:
            s.first_ts = timestamp
        s.last_ts = max(s.last_ts, timestamp)
        s.media_bytes += len(payload)
        if self.server.flv:
            self.server.flv.write(msg_type, timestamp, payload)

        if msg_type == VIDEO and payload:
            codec = payload[0] & 0x0F
            if codec == 7 and len(payload) > 5 and payload[1] == 0:  # AVC sequence header
                record = payload[5:]
                sps_count = record[5] & 0x1F
                if sps_count:
                    sps_length = struct.unpack(">H", record[6:8])[0]
                    s.video = parse_sps(record[8:8 + sps_length])
                    s.video["codec"] = "H.264"
                    self.log(f"video {s.video['width']}x{s.video['height']} {s.video['profile']} level {s.video['level']}")
            elif codec == 7:
                s.video_frames += 1
                if payload[0] >> 4 == 1:
                    s.keyframes += 1
        elif msg_type == AUDIO and payload:
            if payload[0] >> 4 == 10 and len(payload) > 2 and payload[1] == 0:  # AAC sequence header
                s.audio = parse_audio_specific_config(payload[2:])
                s.audio["codec"] = "AAC"
                self.log(f"audio AAC {s.audio['sampleRate']} Hz, {s.audio['channels']} channel(s)")
            else:
                s.audio_frames += 1
        elif msg_type == DATA_AMF0:
            amf = AmfReader(payload)
            values = []
            try:
                while not amf.done():
                    values.append(amf.read())
            except ValueError:
                pass
            for value in values:
                if isinstance(value, dict):
                    s.metadata.update(value)

    async def run(self) -> None:
        self.log("connected")
        drop_at = None
        if self.server.drop_after is not None and not self.server.dropped:
            drop_at = time.monotonic() + self.server.drop_after
        try:
            await self.handshake()
            while True:
                if drop_at and self.session.publishing and time.monotonic() >= drop_at:
                    self.server.dropped = True
                    self.session.closed_by = "server (simulated drop)"
                    self.log("simulating a dropped connection")
                    break
                message = await self.read_message()
                await self.writer.drain()
                if message is None:
                    continue
                msg_type, stream_id, timestamp, payload = message
                if msg_type == SET_CHUNK_SIZE:
                    self.in_chunk_size = struct.unpack(">I", payload[:4])[0] & 0x7FFFFFFF
                elif msg_type == WINDOW_ACK:
                    self.ack_window = struct.unpack(">I", payload[:4])[0]
                elif msg_type == COMMAND_AMF0:
                    if not self.on_command(payload, stream_id):
                        await self.writer.drain()
                        break
                elif msg_type in (AUDIO, VIDEO, DATA_AMF0):
                    self.on_media(msg_type, timestamp, payload)
        except (asyncio.IncompleteReadError, ConnectionError) as error:
            if not self.session.closed_by:
                if isinstance(error, asyncio.IncompleteReadError):
                    self.session.closed_by = "client"
                elif isinstance(error, ConnectionResetError):
                    self.session.closed_by = "client (connection reset)"
                else:
                    self.session.closed_by = str(error)
        finally:
            self.writer.close()
            summary = self.session.summary()
            self.server.sessions.append(summary)
            self.server.write_stats()
            self.log("closed: " + json.dumps(summary))


class TestServer:
    def __init__(self, port: int, key: str | None, record: str | None, stats: str | None, drop_after: float | None):
        self.port = port
        self.key = key
        self.flv = FlvWriter(record) if record else None
        self.stats = stats
        self.drop_after = drop_after
        self.dropped = False
        self.sessions: list[dict] = []

    def write_stats(self) -> None:
        if self.stats:
            with open(self.stats, "w", encoding="utf-8") as f:
                json.dump({"port": self.port, "sessions": self.sessions}, f, indent=2)

    async def serve(self) -> None:
        server = await asyncio.start_server(
            lambda r, w: RtmpConnection(self, r, w).run(), "127.0.0.1", self.port)
        print(f"RTMP test server listening on rtmp://127.0.0.1:{self.port}/live"
              + (f" (key: {'*' * len(self.key)})" if self.key else " (any key)"), flush=True)
        self.write_stats()
        async with server:
            await server.serve_forever()


# ----------------------------------------------------------- self-test -----

class BitWriter:
    def __init__(self):
        self.bits: list[int] = []

    def u(self, n: int, value: int) -> None:
        self.bits += [(value >> (n - 1 - i)) & 1 for i in range(n)]

    def ue(self, value: int) -> None:
        code = value + 1
        length = code.bit_length()
        self.u(length - 1, 0)
        self.u(length, code)

    def rbsp(self) -> bytes:
        self.bits.append(1)  # rbsp_stop_one_bit
        while len(self.bits) % 8:
            self.bits.append(0)
        return bytes(int("".join(map(str, self.bits[i:i + 8])), 2) for i in range(0, len(self.bits), 8))


def synthetic_sps(width: int, height: int) -> bytes:
    """A High-profile SPS for width x height (multiple of 2), as an encoder would write."""
    w = BitWriter()
    w.u(8, 100)  # profile_idc: High
    w.u(8, 0)
    w.u(8, 42)   # level 4.2
    w.ue(0)      # seq_parameter_set_id
    w.ue(1)      # chroma_format_idc 4:2:0
    w.ue(0)
    w.ue(0)
    w.u(1, 0)
    w.u(1, 0)    # no scaling matrix
    w.ue(0)      # log2_max_frame_num_minus4
    w.ue(0)      # pic_order_cnt_type
    w.ue(0)
    w.ue(1)      # max_num_ref_frames
    w.u(1, 0)
    mbs_w, mbs_h = (width + 15) // 16, (height + 15) // 16
    w.ue(mbs_w - 1)
    w.ue(mbs_h - 1)
    w.u(1, 1)    # frame_mbs_only_flag
    w.u(1, 1)    # direct_8x8_inference_flag
    crop_right, crop_bottom = (mbs_w * 16 - width) // 2, (mbs_h * 16 - height) // 2
    w.u(1, 1 if crop_right or crop_bottom else 0)
    if crop_right or crop_bottom:
        w.ue(0)
        w.ue(crop_right)
        w.ue(0)
        w.ue(crop_bottom)
    w.u(1, 0)    # no VUI
    raw = w.rbsp()
    escaped = bytearray()
    zeros = 0
    for byte in raw:  # add emulation prevention bytes
        if zeros >= 2 and byte <= 3:
            escaped.append(3)
            zeros = 0
        escaped.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return b"\x67" + bytes(escaped)


async def run_self_test() -> int:
    """Publishes synthetic H.264/AAC to an in-process server and checks the report."""
    key = "self-test-key"
    server = TestServer(0, key, None, None, None)
    listener = await asyncio.start_server(lambda r, w: RtmpConnection(server, r, w).run(), "127.0.0.1", 0)
    port = listener.sockets[0].getsockname()[1]

    async def publish(stream_key: str) -> None:
        reader, writer = await asyncio.open_connection("127.0.0.1", port)
        writer.write(b"\x03" + b"\x00" * HANDSHAKE_SIZE)
        await reader.readexactly(1 + 2 * HANDSHAKE_SIZE)
        writer.write(b"\x00" * HANDSHAKE_SIZE)

        def message(csid: int, msg_type: int, payload: bytes, stream: int = 0, ts: int = 0) -> bytes:
            out = bytes([csid]) + ts.to_bytes(3, "big") + len(payload).to_bytes(3, "big") + bytes([msg_type])
            out += struct.pack("<I", stream)
            for offset in range(0, len(payload), 128):
                out += (b"" if offset == 0 else bytes([0xC0 | csid])) + payload[offset:offset + 128]
            return out

        def command(*values, stream: int = 0) -> bytes:
            return message(3, COMMAND_AMF0, b"".join(amf_encode(v) for v in values), stream)

        writer.write(command("connect", 1, {"app": "live", "type": "nonprivate"}))
        writer.write(command("createStream", 2, None))
        writer.write(command("publish", 3, None, stream_key, "live", stream=1))
        sps = synthetic_sps(1920, 1080)
        avc_config = bytes([1, 100, 0, 42, 0xFF, 0xE1]) + struct.pack(">H", len(sps)) + sps + b"\x01\x00\x04\x68\xEE\x3C\x80"
        writer.write(message(6, VIDEO, b"\x17\x00\x00\x00\x00" + avc_config, 1))
        writer.write(message(4, AUDIO, b"\xAF\x00" + bytes([0x11, 0x90]), 1))  # AAC-LC, 48 kHz, stereo
        for i in range(30):
            frame_type = 0x17 if i % 10 == 0 else 0x27
            writer.write(message(6, VIDEO, bytes([frame_type, 1, 0, 0, 0]) + b"\x00" * 400, 1, ts=i * 33))
            writer.write(message(4, AUDIO, b"\xAF\x01" + b"\x00" * 50, 1, ts=i * 21))
        await writer.drain()
        writer.close()

    await publish(key)
    await publish("wrong-key")
    for _ in range(50):
        if len(server.sessions) >= 2:
            break
        await asyncio.sleep(0.05)
    listener.close()

    good = next((s for s in server.sessions if s["keyAccepted"]), {})
    bad = next((s for s in server.sessions if s["keyAccepted"] is False), {})
    checks = {
        "right key accepted": bool(good) and good["publishing"],
        "video is 1920x1080 High": good.get("video", {}).get("width") == 1920
        and good.get("video", {}).get("height") == 1080 and good["video"].get("profile") == "High",
        "audio is AAC 48 kHz stereo": good.get("audio", {}).get("sampleRate") == 48000
        and good["audio"].get("channels") == 2,
        "30 video frames, 3 keyframes": good.get("videoFrames") == 30 and good.get("keyframes") == 3,
        "wrong key rejected": bool(bad) and not bad["publishing"],
    }
    for name, ok in checks.items():
        print(("[pass] " if ok else "[FAIL] ") + name)
    return 0 if all(checks.values()) else 1


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--port", type=int, default=1935)
    parser.add_argument("--key", help="stream key to accept; any key when omitted")
    parser.add_argument("--record", help="write the received stream to this FLV file")
    parser.add_argument("--stats", help="write a JSON summary of sessions to this file")
    parser.add_argument("--drop-after", type=float, help="drop the first publishing connection after N seconds")
    parser.add_argument("--self-test", action="store_true", help="check this server against a built-in publisher")
    args = parser.parse_args()
    if args.self_test:
        return asyncio.run(run_self_test())
    server = TestServer(args.port, args.key, args.record, args.stats, args.drop_after)
    try:
        asyncio.run(server.serve())
    except KeyboardInterrupt:
        pass
    finally:
        if server.flv:
            server.flv.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
