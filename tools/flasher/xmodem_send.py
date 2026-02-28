#!/usr/bin/env python3
"""
xmodem_send.py — XMODEM-CRC sender for bootloader firmware updates.

Sends a signed firmware image (.sfw) to the bootloader over a serial port
using the XMODEM-CRC protocol (128-byte blocks, CRC-16/XMODEM).

Usage:
    python xmodem_send.py --port COM3 --file firmware.sfw [--baud 115200]

Protocol:
    1. Open serial port
    2. Optionally send bootloader trigger command (Ninebot protocol)
    3. Wait for receiver to send 'C' (CRC mode request)
    4. Send XMODEM blocks (SOH + seq + ~seq + 128 data + CRC16)
    5. Send EOT when done
    6. Wait for ACK

The bootloader triggers on:
    - STM32: Magic value in backup register (set via Ninebot protocol command)
    - nRF51822: GPREGRET register (set by BLE STM32 before reset)
"""

import argparse
import os
import struct
import sys
import time

import serial


# ── XMODEM constants ───────────────────────────────────────────────────────
SOH = 0x01  # Start of 128-byte block
EOT = 0x04  # End of transmission
ACK = 0x06  # Acknowledge
NAK = 0x15  # Negative acknowledge
CAN = 0x18  # Cancel transfer
CRCC = 0x43  # 'C' — CRC mode request

BLOCK_SIZE = 128
MAX_RETRIES = 10
TIMEOUT_INITIAL = 60  # seconds to wait for 'C'
TIMEOUT_ACK = 10      # seconds to wait for ACK after block


def crc16_xmodem(data: bytes) -> int:
    """Calculate CRC-16/XMODEM (polynomial 0x1021)."""
    crc = 0x0000
    for byte in data:
        crc ^= byte << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = (crc << 1) ^ 0x1021
            else:
                crc <<= 1
            crc &= 0xFFFF
    return crc


def wait_for_crc_request(ser: serial.Serial, timeout: float = TIMEOUT_INITIAL) -> bool:
    """Wait for the receiver to send 'C' indicating CRC mode."""
    print("Waiting for bootloader 'C' request...", end="", flush=True)
    deadline = time.time() + timeout

    while time.time() < deadline:
        if ser.in_waiting > 0:
            ch = ser.read(1)
            if ch == bytes([CRCC]):
                print(" received!")
                return True
            # Drain any other bytes (bootloader banner, etc.)
            continue
        time.sleep(0.1)
        print(".", end="", flush=True)

    print(" TIMEOUT!")
    return False


def send_block(ser: serial.Serial, seq: int, data: bytes) -> bool:
    """
    Send one XMODEM block and wait for ACK.

    Args:
        ser: Serial port
        seq: Block sequence number (1-255, wraps)
        data: Exactly 128 bytes (pad with 0x1A if needed)

    Returns:
        True if ACK received, False on failure.
    """
    assert len(data) == BLOCK_SIZE

    crc = crc16_xmodem(data)

    packet = bytes([SOH, seq & 0xFF, (~seq) & 0xFF]) + data + struct.pack(">H", crc)

    for retry in range(MAX_RETRIES):
        ser.write(packet)
        ser.flush()

        # Wait for response
        response = ser.read(1)
        if len(response) == 0:
            print(f"  Block {seq}: timeout (retry {retry + 1}/{MAX_RETRIES})")
            continue

        if response[0] == ACK:
            return True
        elif response[0] == NAK:
            print(f"  Block {seq}: NAK (retry {retry + 1}/{MAX_RETRIES})")
            continue
        elif response[0] == CAN:
            # Check for double CAN
            response2 = ser.read(1)
            if len(response2) > 0 and response2[0] == CAN:
                print(f"  Block {seq}: receiver cancelled transfer")
                return False
            print(f"  Block {seq}: single CAN, retrying")
            continue
        else:
            print(f"  Block {seq}: unexpected response 0x{response[0]:02X}")
            continue

    return False


def send_eot(ser: serial.Serial) -> bool:
    """Send EOT and wait for ACK."""
    for retry in range(MAX_RETRIES):
        ser.write(bytes([EOT]))
        ser.flush()

        response = ser.read(1)
        if len(response) > 0 and response[0] == ACK:
            return True
        print(f"  EOT: no ACK (retry {retry + 1}/{MAX_RETRIES})")

    return False


def cancel_transfer(ser: serial.Serial):
    """Send CAN CAN to abort transfer."""
    ser.write(bytes([CAN, CAN, CAN]))
    ser.flush()


def send_trigger_command(ser: serial.Serial, target: str):
    """
    Send a Ninebot protocol command to trigger the bootloader.

    This sends a "enter firmware update" command via the Ninebot protocol
    to the target board. The target's application firmware should handle
    this by setting the update flag and resetting.

    Protocol: 5A A5 LEN DST SRC CMD ARG DATA... CHECKSUM_LO CHECKSUM_HI

    Register write to the "update trigger" register:
        CMD = 0x02 (write), ARG = 0xF0 (update register)
        DATA = 0x01 (enter update mode)
    """
    TARGET_ADDRS = {
        "ble-stm32": 0x21,
        "bms-stm32": 0x22,
        "nrf51822": 0x21,  # Goes through BLE STM32 which forwards to nRF51
    }

    dst = TARGET_ADDRS.get(target, 0x21)
    src = 0x3F  # PC

    # Write command: set update flag
    cmd = 0x02  # Write register
    arg = 0xF0  # Update trigger register (custom)
    data = bytes([0x01])  # Enter update mode

    length = 2 + 1 + len(data)  # dst+src + cmd+arg + data
    payload = bytes([length, dst, src, cmd, arg]) + data

    # Checksum: XOR 0xFFFF of sum of all payload bytes
    checksum = sum(payload) ^ 0xFFFF
    checksum_lo = checksum & 0xFF
    checksum_hi = (checksum >> 8) & 0xFF

    packet = bytes([0x5A, 0xA5]) + payload + bytes([checksum_lo, checksum_hi])

    print(f"Sending bootloader trigger to 0x{dst:02X}...")
    print(f"  Packet: {packet.hex()}")
    ser.write(packet)
    ser.flush()

    # Wait a moment for the target to process and reboot
    time.sleep(2.0)

    # Flush any response
    ser.reset_input_buffer()


def xmodem_send(
    port: str,
    filepath: str,
    baudrate: int = 115200,
    target: str | None = None,
    trigger: bool = False,
):
    """
    Send a file via XMODEM-CRC over serial.

    Args:
        port: Serial port name (e.g., COM3 or /dev/ttyUSB0)
        filepath: Path to the .sfw file to send
        baudrate: Serial baud rate (default: 115200)
        target: Target board name (for trigger command)
        trigger: Whether to send a bootloader trigger command first
    """
    # Read file
    with open(filepath, "rb") as f:
        filedata = f.read()

    file_size = len(filedata)
    total_blocks = (file_size + BLOCK_SIZE - 1) // BLOCK_SIZE

    print(f"File: {filepath}")
    print(f"Size: {file_size} bytes ({total_blocks} XMODEM blocks)")
    print(f"Port: {port} @ {baudrate} baud")
    print()

    # Open serial port
    ser = serial.Serial(
        port=port,
        baudrate=baudrate,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=TIMEOUT_ACK,
    )

    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Optionally send trigger command
        if trigger and target:
            send_trigger_command(ser, target)

        # Wait for 'C' from bootloader
        if not wait_for_crc_request(ser):
            print("ERROR: Bootloader did not respond. Is the device in update mode?")
            cancel_transfer(ser)
            return False

        # Send blocks
        seq = 1
        offset = 0
        start_time = time.time()

        for block_num in range(total_blocks):
            # Extract block data, pad last block with 0x1A (SUB)
            block_data = filedata[offset : offset + BLOCK_SIZE]
            if len(block_data) < BLOCK_SIZE:
                block_data += bytes([0x1A] * (BLOCK_SIZE - len(block_data)))

            if not send_block(ser, seq, block_data):
                print(f"\nERROR: Failed to send block {block_num + 1}")
                cancel_transfer(ser)
                return False

            offset += BLOCK_SIZE
            seq = (seq + 1) & 0xFF
            if seq == 0:
                seq = 1  # XMODEM sequence never uses 0

            # Progress
            progress = (block_num + 1) / total_blocks * 100
            elapsed = time.time() - start_time
            speed = offset / elapsed if elapsed > 0 else 0
            sys.stdout.write(
                f"\r  [{block_num + 1}/{total_blocks}] "
                f"{progress:5.1f}% "
                f"({speed:.0f} B/s)"
            )
            sys.stdout.flush()

        print()  # Newline after progress

        # Send EOT
        if not send_eot(ser):
            print("ERROR: EOT not acknowledged")
            return False

        elapsed = time.time() - start_time
        print()
        print(f"Transfer complete!")
        print(f"  {file_size} bytes in {elapsed:.1f}s ({file_size / elapsed:.0f} B/s)")
        print()
        print("The bootloader will now verify the firmware signature.")
        print("If verification passes, the new firmware will be booted.")

        return True

    finally:
        ser.close()


def main():
    parser = argparse.ArgumentParser(
        description="Send signed firmware (.sfw) to bootloader via XMODEM-CRC."
    )
    parser.add_argument(
        "--port", "-p", required=True, help="Serial port (e.g., COM3 or /dev/ttyUSB0)"
    )
    parser.add_argument(
        "--file", "-f", required=True, help="Signed firmware file (.sfw)"
    )
    parser.add_argument(
        "--baud", "-b", type=int, default=115200, help="Baud rate (default: 115200)"
    )
    parser.add_argument(
        "--target",
        "-t",
        choices=["ble-stm32", "bms-stm32", "nrf51822"],
        help="Target board (for trigger command)",
    )
    parser.add_argument(
        "--trigger",
        action="store_true",
        help="Send Ninebot protocol trigger command before XMODEM",
    )

    args = parser.parse_args()

    if not os.path.isfile(args.file):
        print(f"ERROR: File not found: {args.file}")
        sys.exit(1)

    success = xmodem_send(
        port=args.port,
        filepath=args.file,
        baudrate=args.baud,
        target=args.target,
        trigger=args.trigger,
    )

    sys.exit(0 if success else 1)


if __name__ == "__main__":
    main()
