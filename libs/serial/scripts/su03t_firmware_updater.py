#!/usr/bin/env python3

from __future__ import annotations

import argparse
import os
import string
import subprocess
import sys
import time
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

import serial


SOH = 0x01
STX = 0x02
EOT = 0x04
ACK = 0x06
NAK = 0x15
CAN = 0x18
CRC_REQ = ord("C")
PAD = 0x1A

DEFAULT_FIRMWARE = (
    Path(__file__).resolve().parent.parent
    / "firmware"
    / "jx_firm"
    / "jx_su_03t_release_update.bin"
)


class UpgradeError(RuntimeError):
    pass


@dataclass
class HandshakeResult:
    byte: int
    crc_mode: bool


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="SU-03T 串口固件升级工具，支持探测握手并通过 XMODEM/YMODEM 发送升级包。"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--device", default="/dev/ttyUSB0", help="串口设备路径")
    common.add_argument("--baud", type=int, default=115200, help="串口波特率")
    common.add_argument(
        "--systemd-service",
        default="doly-serial",
        help="可能占用串口的 systemd 服务名，用于提示或自动停启",
    )
    common.add_argument(
        "--stop-service",
        action="store_true",
        help="升级前尝试停止 systemd 服务，结束后自动恢复",
    )
    common.add_argument(
        "--boot-command",
        help="进入升级模式前先发送一段十六进制命令，例如 AA 55 10 55 AA",
    )
    common.add_argument(
        "--boot-command-repeat",
        type=int,
        default=1,
        help="进入升级命令的重复发送次数",
    )
    common.add_argument(
        "--boot-command-interval",
        type=float,
        default=0.2,
        help="重复发送进入升级命令时的间隔秒数",
    )
    common.add_argument(
        "--boot-wait",
        type=float,
        default=8.0,
        help="发送进入升级命令后，等待 BootLoader 握手的超时秒数",
    )
    common.add_argument(
        "--read-timeout",
        type=float,
        default=0.2,
        help="串口单次读超时秒数",
    )
    common.add_argument(
        "--pulse-dtr",
        action="store_true",
        help="打开串口后拉低再拉高 DTR，可用于某些接线方式下的复位",
    )
    common.add_argument(
        "--verbose",
        action="store_true",
        help="输出更详细的串口接收日志",
    )

    probe = subparsers.add_parser(
        "probe",
        parents=[common],
        help="只探测模块握手和输出，不发送固件",
    )
    probe.add_argument(
        "--listen-seconds",
        type=float,
        default=10.0,
        help="探测模式下的监听时长",
    )
    probe.add_argument(
        "--loop",
        action="store_true",
        help="持续循环探测，直到抓到握手或手动中断",
    )
    probe.add_argument(
        "--cycles",
        type=int,
        default=1,
        help="循环探测的轮数，0 表示无限循环",
    )
    probe.add_argument(
        "--cycle-gap",
        type=float,
        default=0.5,
        help="每轮探测之间的间隔秒数",
    )

    flash = subparsers.add_parser(
        "flash",
        parents=[common],
        help="发送固件",
    )
    flash.add_argument(
        "--firmware",
        type=Path,
        default=DEFAULT_FIRMWARE,
        help="升级固件路径",
    )
    flash.add_argument(
        "--protocol",
        choices=["xmodem", "ymodem"],
        required=True,
        help="BootLoader 使用的传输协议。仓库内没有 SU-03T 的公开升级协议，需按实测选择",
    )
    flash.add_argument(
        "--packet-size",
        type=int,
        choices=[128, 1024],
        default=1024,
        help="XMODEM 数据块大小，YMODEM 数据块始终使用 1024",
    )
    flash.add_argument(
        "--response-timeout",
        type=float,
        default=10.0,
        help="等待 ACK/NAK/握手响应的超时秒数",
    )
    flash.add_argument(
        "--retries",
        type=int,
        default=12,
        help="单个分包允许的重试次数",
    )
    flash.add_argument(
        "--inter-frame-delay",
        type=float,
        default=0.0,
        help="每个数据包发完后的额外停顿秒数",
    )
    flash.add_argument(
        "--post-eot-wait",
        type=float,
        default=2.0,
        help="结束传输后继续监听模块输出的秒数",
    )

    return parser.parse_args()


def parse_hex_bytes(value: str | None) -> bytes:
    if not value:
        return b""
    cleaned = (
        value.replace("0x", " ")
        .replace("0X", " ")
        .replace(",", " ")
        .replace(":", " ")
        .replace("-", " ")
    )
    parts = [part for part in cleaned.split() if part]
    if not parts:
        return b""
    try:
        return bytes(int(part, 16) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError(f"无法解析十六进制字节串: {value}") from exc


def crc16_xmodem(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc


def checksum(data: bytes) -> int:
    return sum(data) & 0xFF


def printable_preview(data: bytes) -> str:
    chars = []
    for byte in data:
        if chr(byte) in string.printable and byte not in (0x0B, 0x0C):
            chars.append(chr(byte))
        else:
            chars.append(".")
    return "".join(chars)


def describe_control_byte(byte: int) -> str:
    control_names = {
        ACK: "ACK",
        NAK: "NAK",
        CAN: "CAN",
        CRC_REQ: "CRC_REQ",
        SOH: "SOH",
        STX: "STX",
        EOT: "EOT",
        0x00: "NUL",
    }
    if byte in control_names:
        return control_names[byte]
    if 32 <= byte < 127:
        return repr(chr(byte))
    return "CTRL"


def log_received_bytes(prefix: str, chunk: bytes, elapsed: float, verbose: bool) -> None:
    if verbose:
        for byte in chunk:
            print(
                f"{prefix} +{elapsed:.3f}s RX=0x{byte:02X} ({describe_control_byte(byte)})",
                flush=True,
            )
        return
    print(
        f"{prefix} HEX={chunk.hex(' ')} TEXT={printable_preview(chunk)}",
        flush=True,
    )


class ServiceGuard:
    def __init__(self, service_name: str, stop_service: bool):
        self.service_name = service_name
        self.stop_service = stop_service
        self.was_active = False

    def __enter__(self) -> "ServiceGuard":
        self.was_active = self._is_active()
        if self.was_active:
            if not self.stop_service:
                raise UpgradeError(
                    f"检测到 systemd 服务 {self.service_name} 正在运行，可能占用串口。"
                    "请先停止服务，或追加 --stop-service 让工具代为处理。"
                )
            self._run_systemctl("stop")
            deadline = time.monotonic() + 5.0
            while time.monotonic() < deadline:
                if not self._is_active():
                    print(f"[INFO] 已停止服务 {self.service_name}")
                    return self
                time.sleep(0.1)
            raise UpgradeError(f"未能在超时前停止服务 {self.service_name}")
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.stop_service and self.was_active:
            try:
                self._run_systemctl("start")
                print(f"[INFO] 已恢复服务 {self.service_name}")
            except UpgradeError as restart_error:
                print(f"[WARN] 恢复服务失败: {restart_error}", file=sys.stderr)

    def _is_active(self) -> bool:
        result = subprocess.run(
            ["systemctl", "is-active", "--quiet", self.service_name],
            check=False,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        return result.returncode == 0

    def _run_systemctl(self, action: str) -> None:
        cmd = ["systemctl", action, self.service_name]
        if os.geteuid() != 0:
            cmd.insert(0, "sudo")
        result = subprocess.run(cmd, check=False)
        if result.returncode != 0:
            raise UpgradeError(f"执行 {' '.join(cmd)} 失败，退出码 {result.returncode}")


class SerialPort:
    def __init__(self, device: str, baud: int, read_timeout: float, pulse_dtr: bool):
        self.device = device
        self.baud = baud
        self.read_timeout = read_timeout
        self.pulse_dtr = pulse_dtr
        self.serial: serial.Serial | None = None

    def __enter__(self) -> "SerialPort":
        kwargs = dict(
            port=self.device,
            baudrate=self.baud,
            bytesize=serial.EIGHTBITS,
            parity=serial.PARITY_NONE,
            stopbits=serial.STOPBITS_ONE,
            timeout=self.read_timeout,
            write_timeout=2.0,
            xonxoff=False,
            rtscts=False,
            dsrdtr=False,
        )
        try:
            self.serial = serial.Serial(exclusive=True, **kwargs)
        except TypeError:
            self.serial = serial.Serial(**kwargs)
        except serial.SerialException as exc:
            raise UpgradeError(f"打开串口失败: {exc}") from exc

        self.serial.reset_input_buffer()
        self.serial.reset_output_buffer()
        if self.pulse_dtr:
            self.serial.dtr = False
            time.sleep(0.1)
            self.serial.dtr = True
            time.sleep(0.2)
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self.serial is not None:
            self.serial.close()
            self.serial = None

    def write(self, data: bytes) -> None:
        assert self.serial is not None
        self.serial.write(data)
        self.serial.flush()

    def read(self, size: int = 1) -> bytes:
        assert self.serial is not None
        return self.serial.read(size)

    def reset_input(self) -> None:
        assert self.serial is not None
        self.serial.reset_input_buffer()


def wait_for_handshake(port: SerialPort, timeout: float, verbose: bool = False) -> HandshakeResult:
    deadline = time.monotonic() + timeout
    transcript = bytearray()
    start = time.monotonic()
    while time.monotonic() < deadline:
        chunk = port.read(1)
        if not chunk:
            continue
        byte = chunk[0]
        transcript.extend(chunk)
        if verbose:
            elapsed = time.monotonic() - start
            print(
                f"[HS] +{elapsed:.3f}s RX=0x{byte:02X} ({describe_control_byte(byte)})",
                flush=True,
            )
        if byte == CRC_REQ:
            return HandshakeResult(byte=byte, crc_mode=True)
        if byte == NAK:
            return HandshakeResult(byte=byte, crc_mode=False)
        if byte == CAN:
            raise UpgradeError("接收端返回 CAN，已取消升级")
    if transcript:
        preview = printable_preview(bytes(transcript[-64:]))
        hex_preview = bytes(transcript[-32:]).hex(" ")
        raise UpgradeError(
            "等待握手超时。收到的尾部数据为 "
            f"HEX=[{hex_preview}] TEXT=[{preview}]"
        )
    raise UpgradeError("等待握手超时，模块没有返回 C/NAK")


def wait_for_control_byte(port: SerialPort, timeout: float) -> int:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        chunk = port.read(1)
        if not chunk:
            continue
        byte = chunk[0]
        if byte in (ACK, NAK, CAN, CRC_REQ):
            return byte
    raise UpgradeError("等待 ACK/NAK 超时")


def make_packet(block_number: int, payload: bytes, packet_size: int, crc_mode: bool) -> bytes:
    header = STX if packet_size == 1024 else SOH
    body = payload.ljust(packet_size, bytes([PAD]))
    packet = bytearray([header, block_number & 0xFF, 0xFF - (block_number & 0xFF)])
    packet.extend(body)
    if crc_mode:
        crc = crc16_xmodem(body)
        packet.extend([(crc >> 8) & 0xFF, crc & 0xFF])
    else:
        packet.append(checksum(body))
    return bytes(packet)


def send_packet(
    port: SerialPort,
    block_number: int,
    payload: bytes,
    packet_size: int,
    crc_mode: bool,
    retries: int,
    response_timeout: float,
    inter_frame_delay: float,
) -> None:
    packet = make_packet(block_number, payload, packet_size, crc_mode)
    for attempt in range(1, retries + 1):
        port.write(packet)
        if inter_frame_delay > 0:
            time.sleep(inter_frame_delay)
        response = wait_for_control_byte(port, response_timeout)
        if response == ACK:
            return
        if response == NAK:
            print(f"[WARN] 分包 {block_number} 被 NAK，重试 {attempt}/{retries}")
            continue
        if response == CAN:
            raise UpgradeError("接收端返回 CAN，升级已被取消")
        if response == CRC_REQ:
            continue
    raise UpgradeError(f"分包 {block_number} 超过最大重试次数")


def send_eot(port: SerialPort, response_timeout: float) -> None:
    for _ in range(2):
        port.write(bytes([EOT]))
        response = wait_for_control_byte(port, response_timeout)
        if response == ACK:
            return
        if response == CAN:
            raise UpgradeError("接收端在结束阶段返回 CAN")
        if response == NAK:
            continue
    raise UpgradeError("发送 EOT 后没有收到 ACK")


def send_xmodem(
    port: SerialPort,
    firmware: bytes,
    packet_size: int,
    retries: int,
    response_timeout: float,
    inter_frame_delay: float,
    crc_mode: bool,
) -> None:
    total_packets = (len(firmware) + packet_size - 1) // packet_size
    for index in range(total_packets):
        start = index * packet_size
        end = start + packet_size
        payload = firmware[start:end]
        block_number = index + 1
        send_packet(
            port,
            block_number,
            payload,
            packet_size,
            crc_mode,
            retries,
            response_timeout,
            inter_frame_delay,
        )
        print_progress(block_number, total_packets, min(end, len(firmware)), len(firmware))
    send_eot(port, response_timeout)


def send_ymodem(
    port: SerialPort,
    firmware_path: Path,
    firmware: bytes,
    retries: int,
    response_timeout: float,
    inter_frame_delay: float,
    crc_mode: bool,
) -> None:
    metadata = f"{firmware_path.name}\0{len(firmware)}\0".encode("ascii")
    send_packet(
        port,
        0,
        metadata,
        128,
        crc_mode,
        retries,
        response_timeout,
        inter_frame_delay,
    )
    response = wait_for_control_byte(port, response_timeout)
    if response != ACK:
        raise UpgradeError("YMODEM 文件头发送后没有收到 ACK")
    response = wait_for_control_byte(port, response_timeout)
    if response not in (CRC_REQ, NAK):
        raise UpgradeError("YMODEM 文件头 ACK 后没有收到后续握手")

    packet_size = 1024
    total_packets = (len(firmware) + packet_size - 1) // packet_size
    for index in range(total_packets):
        start = index * packet_size
        end = start + packet_size
        payload = firmware[start:end]
        block_number = index + 1
        send_packet(
            port,
            block_number,
            payload,
            packet_size,
            crc_mode,
            retries,
            response_timeout,
            inter_frame_delay,
        )
        print_progress(block_number, total_packets, min(end, len(firmware)), len(firmware))

    send_eot(port, response_timeout)
    response = wait_for_control_byte(port, response_timeout)
    if response not in (CRC_REQ, NAK):
        raise UpgradeError("YMODEM 结束后没有收到尾块请求")
    send_packet(
        port,
        0,
        b"",
        128,
        crc_mode,
        retries,
        response_timeout,
        inter_frame_delay,
    )
    response = wait_for_control_byte(port, response_timeout)
    if response != ACK:
        raise UpgradeError("YMODEM 尾块发送后没有收到 ACK")


def print_progress(block_number: int, total_packets: int, sent_bytes: int, total_bytes: int) -> None:
    percent = (sent_bytes / total_bytes) * 100 if total_bytes else 100.0
    print(
        f"[INFO] 分包 {block_number}/{total_packets} 已确认，"
        f"进度 {sent_bytes}/{total_bytes} ({percent:.1f}%)"
    )


def send_boot_command(port: SerialPort, args: argparse.Namespace) -> None:
    command = parse_hex_bytes(args.boot_command)
    if not command:
        return
    for index in range(args.boot_command_repeat):
        port.write(command)
        print(f"[INFO] 已发送进入升级命令，第 {index + 1}/{args.boot_command_repeat} 次")
        if index + 1 < args.boot_command_repeat:
            time.sleep(args.boot_command_interval)


def run_probe_cycle(port: SerialPort, args: argparse.Namespace, cycle_index: int, cycle_total: int | None) -> int | None:
    cycle_label = f"{cycle_index}" if cycle_total is None else f"{cycle_index}/{cycle_total}"
    print(
        f"[INFO] === 探测轮次 {cycle_label} ===\n"
        f"[INFO] 若模块需要手动复位或重新上电，请在本轮监听期间操作；"
        f"监听时长 {args.listen_seconds:.1f} 秒。"
    )
    send_boot_command(port, args)
    deadline = time.monotonic() + args.listen_seconds
    captured = bytearray()
    started = time.monotonic()
    while time.monotonic() < deadline:
        chunk = port.read(64)
        if not chunk:
            continue
        captured.extend(chunk)
        elapsed = time.monotonic() - started
        log_received_bytes("[RX]", chunk, elapsed, args.verbose)
        if any(byte in (CRC_REQ, NAK, CAN) for byte in chunk):
            for byte in chunk:
                if byte == CRC_REQ:
                    print("[INFO] 检测到字符 C，接收端大概率在请求 CRC 模式传输")
                    return 0
                if byte == NAK:
                    print("[INFO] 检测到 NAK，接收端大概率在请求 checksum 模式传输")
                    return 0
                if byte == CAN:
                    print("[INFO] 检测到 CAN，接收端主动取消了会话")
                    return 2
    if captured:
        counts = Counter(captured)
        summary = ", ".join(
            f"0x{byte:02X}x{count}" for byte, count in counts.most_common(8)
        )
        print(f"[INFO] 本轮接收统计: {summary}")
        if set(captured) == {0x00}:
            print(
                "[WARN] 只收到了 0x00，这通常是普通唤醒/空闲字节，"
                "不是 BootLoader 的升级握手。"
            )
            print(
                "[HINT] 请尝试让模块真正进入升级模式，例如断电重上电、"
                "按厂商要求拉脚位，或通过 --boot-command 发送已知触发命令。"
            )
            return 1
        print("[WARN] 没有发现标准握手字节，但串口有输出。请根据上面的日志判断模块是否进入升级模式。")
        return 1
    print("[WARN] 探测期间串口无输出。模块可能未进入升级模式，或升级协议不是标准 XMODEM/YMODEM 握手。")
    return 1


def run_probe(port: SerialPort, args: argparse.Namespace) -> int:
    if args.loop or args.cycles == 0:
        cycle_total: int | None = None
        cycle_index = 0
        try:
            while True:
                cycle_index += 1
                result = run_probe_cycle(port, args, cycle_index, cycle_total)
                if result in (0, 2):
                    return result
                if args.cycle_gap > 0:
                    time.sleep(args.cycle_gap)
                port.reset_input()
        except KeyboardInterrupt:
            print("[WARN] 用户中断循环探测", file=sys.stderr)
            return 130

    cycle_total = max(args.cycles, 1)
    result = 1
    for cycle_index in range(1, cycle_total + 1):
        result = run_probe_cycle(port, args, cycle_index, cycle_total)
        if result in (0, 2):
            return result
        if cycle_index < cycle_total and args.cycle_gap > 0:
            time.sleep(args.cycle_gap)
            port.reset_input()
    return result


def run_flash(port: SerialPort, args: argparse.Namespace) -> int:
    firmware_path = args.firmware.resolve()
    if not firmware_path.is_file():
        raise UpgradeError(f"固件文件不存在: {firmware_path}")
    firmware = firmware_path.read_bytes()
    if not firmware:
        raise UpgradeError("固件文件为空")

    print(f"[INFO] 固件: {firmware_path}")
    print(f"[INFO] 大小: {len(firmware)} 字节")
    send_boot_command(port, args)
    print(
        "[INFO] 等待 BootLoader 握手。若模块需要手动复位或重新上电，请现在操作。"
    )
    handshake = wait_for_handshake(port, args.boot_wait)
    mode = "CRC" if handshake.crc_mode else "checksum"
    print(f"[INFO] 已收到握手字节 0x{handshake.byte:02X}，传输校验模式: {mode}")

    if args.protocol == "xmodem":
        send_xmodem(
            port,
            firmware,
            args.packet_size,
            args.retries,
            args.response_timeout,
            args.inter_frame_delay,
            handshake.crc_mode,
        )
    else:
        send_ymodem(
            port,
            firmware_path,
            firmware,
            args.retries,
            args.response_timeout,
            args.inter_frame_delay,
            handshake.crc_mode,
        )

    if args.post_eot_wait > 0:
        deadline = time.monotonic() + args.post_eot_wait
        trailer = bytearray()
        while time.monotonic() < deadline:
            chunk = port.read(64)
            if chunk:
                trailer.extend(chunk)
        if trailer:
            print(
                f"[INFO] 结束后串口输出: HEX={bytes(trailer).hex(' ')} "
                f"TEXT={printable_preview(bytes(trailer))}"
            )

    print("[INFO] 传输流程完成。建议随后断电重启模块，并验证离线命令是否恢复。")
    return 0


def main() -> int:
    args = parse_args()
    try:
        with ServiceGuard(args.systemd_service, args.stop_service):
            with SerialPort(args.device, args.baud, args.read_timeout, args.pulse_dtr) as port:
                if args.command == "probe":
                    return run_probe(port, args)
                return run_flash(port, args)
    except KeyboardInterrupt:
        print("[WARN] 用户中断升级流程", file=sys.stderr)
        return 130
    except UpgradeError as exc:
        print(f"[ERROR] {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())