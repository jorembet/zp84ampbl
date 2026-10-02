#!/usr/bin/env python3
"""Private BLE transport for zp84gui. stdin/stdout carry framed IPC, not logs.
Vendor BTService: 80,length,command,payload, CRC16-Modbus (big endian).
No HID envelope is sent over BLE. Only parameter read/write is accepted.
"""
import asyncio
import struct
import sys


def crc16(data):
    crc = 0xFFFF
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0xA001 if crc & 1 else 0)
    return crc


def pack(command, payload):
    if len(payload) > 15:
        raise ValueError("BLE request exceeds 20-byte frame")
    data = bytes((0x80, len(payload) + 3, command)) + payload
    return data + struct.pack(">H", crc16(data))


class Parser:
    def __init__(self):
        self.buffer = bytearray()

    def feed(self, data):
        self.buffer.extend(data)
        frames = []
        while self.buffer:
            if self.buffer[0] != 0x80:
                del self.buffer[0]
                continue
            if len(self.buffer) < 2:
                break
            length = self.buffer[1] + 2
            if length < 5:
                del self.buffer[0]
                continue
            if len(self.buffer) < length:
                break
            frame = bytes(self.buffer[:length])
            if crc16(frame[:-2]) != int.from_bytes(frame[-2:], "big"):
                del self.buffer[0]
                continue
            del self.buffer[:length]
            frames.append((frame[2], frame[3:-2]))
        return frames


def reply(error, command=0, payload=b""):
    sys.stdout.buffer.write(struct.pack(">BBH", error, command, len(payload)) + payload)
    sys.stdout.buffer.flush()


def read_request():
    header = sys.stdin.buffer.read(3)
    if not header:
        return None
    if len(header) != 3:
        raise EOFError("Truncated IPC header")
    command, length = struct.unpack(">BH", header)
    if length > 128:
        raise ValueError("IPC request too large")
    payload = sys.stdin.buffer.read(length)
    if len(payload) != length:
        raise EOFError("Truncated IPC payload")
    return command, payload


async def known_devices():
    """Reuse BlueZ objects: a connected peripheral may no longer advertise."""
    from dbus_fast import BusType, Message, MessageType
    from dbus_fast.aio import MessageBus
    from bleak.backends.device import BLEDevice
    bus = await MessageBus(bus_type=BusType.SYSTEM).connect()
    try:
        result = await bus.call(Message(destination="org.bluez", path="/",
            interface="org.freedesktop.DBus.ObjectManager", member="GetManagedObjects"))
        if result.message_type == MessageType.ERROR:
            raise RuntimeError("BlueZ: " + str(result.body))
        devices = []
        for path, interfaces in result.body[0].items():
            props = {key: val.value for key, val in interfaces.get("org.bluez.Device1", {}).items()}
            name = props.get("Name", "")
            if name in ("Mango3.0", "Mango 3.0") and not props.get("Blocked", False):
                devices.append(BLEDevice(props["Address"], name, {"path": path, "props": props}))
        return devices
    finally:
        bus.disconnect()


RESPONSE_TIMEOUT = 2.0

stage = 51  # Startup diagnostics carried over IPC to the GUI.

async def run():
    from bleak import BleakClient, BleakScanner
    global stage
    matches = await known_devices()
    if not matches:
        devices = await BleakScanner.discover(timeout=6)
        matches = [d for d in devices if d.name in ("Mango 3.0", "Mango3.0")]
    if len(matches) != 1:
        raise RuntimeError("Mango 3.0 absent or ambiguous; found %d" % len(matches))
    queue = asyncio.Queue()
    parser = Parser()
    write_uuid = "0000ae01-0000-1000-8000-00805f9b34fb"
    notify_uuid = "0000ae02-0000-1000-8000-00805f9b34fb"
    stage = 52
    async with BleakClient(matches[0], timeout=12) as client:
        stage = 53
        write = client.services.get_characteristic(write_uuid)
        notify = client.services.get_characteristic(notify_uuid)
        if not write or "write-without-response" not in write.properties or not notify or "notify" not in notify.properties:
            raise RuntimeError("Vendor GATT characteristics missing")

        def received(_characteristic, data):
            for item in parser.feed(data):
                queue.put_nowait(item)

        async def reconnect():
            nonlocal write, notify
            try:
                if client.is_connected:
                    await client.disconnect()
                await asyncio.sleep(0.2)
                await asyncio.wait_for(client.connect(), timeout=8)
                write = client.services.get_characteristic(write_uuid)
                notify = client.services.get_characteristic(notify_uuid)
                if not write or "write-without-response" not in write.properties or not notify or "notify" not in notify.properties:
                    raise RuntimeError("Vendor GATT characteristics missing after reconnect")
                await client.start_notify(notify, received)
                parser.buffer.clear()
                while not queue.empty():
                    queue.get_nowait()
                return True
            except Exception as error:
                print("Bluetooth: GATT reconnect failed: " + str(error), file=sys.stderr)
                return False

        stage = 54
        await client.start_notify(notify, received)

        last_send = 0.0

        async def exchange(command, payload):
            nonlocal last_send
            attempts = 3 if command == 6 else 1  # Reads are safe to retry; never replay writes.
            reconnected = False
            ids = [payload[i:i+2] for i in range(0, len(payload), 2)]
            for attempt in range(attempts):
                # A stale partial notification must not poison the next request.
                parser.buffer.clear()
                while not queue.empty():
                    queue.get_nowait()
                await asyncio.sleep(max(0, 0.03 - (asyncio.get_running_loop().time() - last_send)))
                last_send = asyncio.get_running_loop().time()
                async def response():
                    records = {}
                    while True:
                        cmd, data = await queue.get()
                        if cmd != command:
                            continue
                        if command == 3:
                            if data == payload:
                                return data
                        elif data and len(data) % 4 == 0:
                            # Devices can return a group as several CRC-valid frames.
                            for i in range(0, len(data), 4):
                                key = data[i:i+2]
                                if key in ids:
                                    records[key] = data[i:i+4]
                            if all(key in records for key in ids):
                                return b"".join(records[key] for key in ids)
                try:
                    await client.write_gatt_char(write, pack(command, payload), response=False)
                    return await asyncio.wait_for(response(), RESPONSE_TIMEOUT)
                except TimeoutError:
                    print("Bluetooth: query timeout command=%02x attempt=%d" % (command, attempt+1), file=sys.stderr)
                except Exception as error:
                    if command != 6:
                        raise
                    print("Bluetooth: query failed command=%02x attempt=%d: %s" % (command, attempt+1, error), file=sys.stderr)

                if command != 6:
                    raise TimeoutError
                if not client.is_connected:
                    if reconnected or not await reconnect():
                        raise ConnectionError("BLE link disconnected during DSP read")
                    reconnected = True
                if attempt + 1 < attempts:
                    await asyncio.sleep(0.15)
            if not client.is_connected and not reconnected and not await reconnect():
                raise ConnectionError("BLE link disconnected during DSP read")
            raise TimeoutError

        stage = 55
        await exchange(6, b"\x00\x00")  # Verify DSP before reporting connected.
        reply(0)
        stage = 5

        while True:
            request = await asyncio.to_thread(read_request)
            if request is None:
                return
            command, payload = request
            try:
                if command == 6 and payload and len(payload) % 2 == 0:
                    data = bytearray()
                    # Three IDs: reply is 3*4+5=17 bytes, below the default
                    # BLE notification payload limit of 20. Four IDs need 21.
                    for offset in range(0, len(payload), 6):
                        data.extend(await exchange(command, payload[offset:offset+6]))
                    reply(0, command, data)
                elif command == 3 and len(payload) == 4:
                    reply(0, command, await exchange(command, payload))
                else:
                    reply(6)
            except TimeoutError:
                if not client.is_connected:
                    raise ConnectionError("BLE link disconnected")
                # A missed DSP reply is not a GATT disconnect. Keep the helper
                # and link alive; caller invalidates the incomplete snapshot.
                reply(2)



if __name__ == "__main__":
    try:
        asyncio.run(run())
    except Exception as error:
        print("Bluetooth: " + str(error), file=sys.stderr)
        try:
            reply(stage)
        except (BrokenPipeError, OSError):
            pass
        sys.exit(1)
