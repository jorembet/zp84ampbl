r"""
zp84/transport.py - lapisan transport untuk ZP 8.4 AMP.

Ada 3 jalur yang mungkin, semua abstracted di balik kelas yang sama:

  SerialTransport   USB CDC-ACM / USB-serial adapter
                     -> /dev/ttyACM0, /dev/ttyUSB0  (butuh pyserial)
  RfcommTransport   Bluetooth SPP / RFCOMM
                     -> /dev/rfcomm0               (butuh pyserial, pairing dulu)
  BleTransport      Bluetooth LE GATT 0000ae00/ae01/ae02
                     ->iface hci0                  (butuh bleak)

App Windows yang kita analisis menulis 65 byte lewat WriteFile, jadi kelas
SerialTransport adalah yang paling mungkin dipakai. Ganti sesuai hasil
deteksi device.
"""

from __future__ import annotations

import abc
import time

FRAME_TIMEOUT = 0.25


class TransportError(RuntimeError):
    pass


class Transport(abc.ABC):
    """Antarmuka minimum: kirim frame, terima frame."""

    name = "abstract"

    @abc.abstractmethod
    def open(self) -> None: ...

    @abc.abstractmethod
    def close(self) -> None: ...

    @abc.abstractmethod
    def send(self, data: bytes) -> int: ...

    @abc.abstractmethod
    def recv(self, size: int, timeout: float = FRAME_TIMEOUT) -> bytes: ...

    # -- helper ---------------------------------------------------------
    def send_frame(self, frame: bytes) -> int:
        return self.send(frame)

    def read_frame(self, size: int = 65,
                   timeout: float = FRAME_TIMEOUT) -> bytes | None:
        buf = b""
        deadline = time.time() + timeout
        while len(buf) < size and time.time() < deadline:
            chunk = self.recv(size - len(buf),
                              max(0.0, deadline - time.time()))
            if not chunk:
                continue
            buf += chunk
        return buf if len(buf) == size else (buf or None)

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, *exc):
        self.close()


# ------------------------------------------------------------- serial / USB

class SerialTransport(Transport):
    """
    USB CDC-ACM atau USB-serial (CH340/FTDI/CP210x), dan juga RFCOMM
    karena di Linux semuanya device /dev/tty*.

        SerialTransport("/dev/ttyACM0", baud=115200)
        SerialTransport("/dev/rfcomm0", baud=115200)   # Bluetooth SPP
    """

    name = "serial"

    def __init__(self, port: str, baud: int = 115200, timeout: float = 0.2):
        self.port = port
        self.baud = baud
        self.timeout = timeout
        self._ser = None

    def open(self) -> None:
        try:
            import serial
        except ImportError:
            raise TransportError(
                "pyserial belum ada. Jalankan:  pip install pyserial")
        try:
            self._ser = serial.Serial(self.port, self.baud, timeout=self.timeout)
        except Exception as exc:
            raise TransportError(f"gagal buka {self.port}: {exc}") from exc

    def close(self) -> None:
        if self._ser:
            self._ser.close()
            self._ser = None

    def send(self, data: bytes) -> int:
        if not self._ser:
            raise TransportError("transport belum dibuka")
        return self._ser.write(data)

    def recv(self, size: int, timeout: float = FRAME_TIMEOUT) -> bytes:
        if not self._ser:
            raise TransportError("transport belum dibuka")
        self._ser.timeout = timeout
        try:
            return self._ser.read(size)
        except Exception:
            return b""

    def __repr__(self) -> str:
        return f"<SerialTransport {self.port} @{self.baud}>"


# ------------------------------------------------------------- Bluetooth LE

SERVICE_UUID = "0000ae00-0000-1000-8000-00805f9b34fb"
CHAR_WRITE_UUID = "0000ae01-0000-1000-8000-00805f9b34fb"
CHAR_NOTIFY_UUID = "0000ae02-0000-1000-8000-00805f9b34fb"


class BleTransport(Transport):
    """
    BLE GATT lewat bleak. UUID diambil dari analysis classes.dex APK.

        BleTransport("C0:00:00:0B:91:59:DA")   # nama BT "DSP audio8"
    """

    name = "ble"

    def __init__(self, address: str, iface: str = "hci0"):
        self.address = address
        self.iface = iface
        self._client = None

    def open(self) -> None:
        try:
            from bleak import BleakClient
        except ImportError:
            raise TransportError(
                "bleak belum ada. Jalankan:  pip install bleak")
        import asyncio
        self._client = BleakClient(self.address, timeout=10.0)
        asyncio.run(self._client.connect())

    def close(self) -> None:
        if self._client:
            import asyncio
            try:
                asyncio.run(self._client.disconnect())
            finally:
                self._client = None

    def send(self, data: bytes) -> int:
        if not self._client:
            raise TransportError("transport belum dibuka")
        import asyncio
        asyncio.run(self._client.write_gatt_char(CHAR_WRITE_UUID, data,
                                                 response=True))
        return len(data)

    def recv(self, size: int, timeout: float = FRAME_TIMEOUT) -> bytes:
        return b""      # butuh notifikasi BLE, belum diimplementasi

    def __repr__(self) -> str:
        return f"<BleTransport {self.address}>"


# ------------------------------------------------------------------- auto

def open_auto(*candidates: str, baud: int = 115200) -> Transport:
    """Coba beberapa port berurutan, pakai yang pertama yang bisa dibuka."""
    errors = []
    for port in candidates:
        t = SerialTransport(port, baud)
        try:
            t.open()
            return t
        except TransportError as exc:
            errors.append(str(exc))
    raise TransportError("tidak ada port yang bisa dibuka:\n  " +
                         "\n  ".join(errors))
