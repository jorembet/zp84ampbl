"""BLE protocol/IPC regression tests, no radio or DSP required."""
import asyncio
import importlib.util
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import patch, AsyncMock

spec = importlib.util.spec_from_file_location('zpble', Path(__file__).parents[1]/'src/ble/zp84-ble.py')
ble = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ble)


class BleTests(unittest.TestCase):
    def test_crc_and_fragmentation(self):
        self.assertEqual(ble.crc16(b'123456789'), 0x4B37)
        packet = ble.pack(6, b'\x00\x8b')
        parser = ble.Parser()
        self.assertEqual(parser.feed(b'noise'+packet[:3]), [])
        self.assertEqual(parser.feed(packet[3:]), [(6,b'\x00\x8b')])
        bad = bytearray(packet);bad[-1] ^= 1
        self.assertEqual(parser.feed(bad+packet), [(6,b'\x00\x8b')])
        self.assertEqual(parser.feed(packet+packet), [(6,b'\x00\x8b')]*2)
        with self.assertRaises(ValueError):ble.pack(6, bytes(16))

    def test_transport(self):
        self.run_transport(False)

    def test_connected_device_skips_scan(self):
        self.run_transport(True)

    def test_lost_read_retried(self):
        self.run_transport(True, 'drop_read')

    def test_write_timeout_not_replayed_or_disconnected(self):
        self.run_transport(True, 'drop_write')

    def test_multi_frame_replies(self):
        self.run_transport(True, 'split')

    def run_transport(self, cached, fault=None):
        writes=[]; replies=[];state={i:100+i for i in range(8)}
        class Scanner:
            @staticmethod
            async def discover(timeout):
                assert not cached, 'Connected/cached device must not require advertising'
                return [types.SimpleNamespace(name='Mango 3.0')]
        class Client:
            def __init__(self,*args,**kwargs):
                self.services=self;self.is_connected=True;self.dropped=False
            def get_characteristic(self,uuid):return types.SimpleNamespace(properties=['write-without-response','notify'])
            async def __aenter__(self):return self
            async def __aexit__(self,*args):pass
            async def start_notify(self,char,callback):self.callback=callback
            async def write_gatt_char(self,char,data,response):
                self.assertions(data,response)
                cmd,payload=ble.Parser().feed(data)[0];writes.append((cmd,payload))
                if cmd==3:
                    state[int.from_bytes(payload[:2],'big')]=int.from_bytes(payload[2:],'big');out=payload
                else:
                    out=b''.join(payload[i:i+2]+state[int.from_bytes(payload[i:i+2],'big')].to_bytes(2,'big') for i in range(0,len(payload),2))
                if not self.dropped and ((fault=='drop_read' and cmd==6 and len(payload)>2) or (fault=='drop_write' and cmd==3)):
                    self.dropped=True;return
                blocks=[out[i:i+4] for i in range(0,len(out),4)][::-1] if fault=='split' else [out]
                for block in blocks:
                    raw=bytes([128,len(block)+3,cmd])+block;raw+=ble.crc16(raw).to_bytes(2,'big')
                    assert len(raw)<=20
                    self.callback(None,raw[:4]);self.callback(None,raw[4:])
            @staticmethod
            def assertions(data,response):
                assert len(data)<=20 and response is False
        ids=b''.join(i.to_bytes(2,'big') for i in range(8))
        requests=iter([(6,ids),(3,b'\0\1\x02\x58'),(6,b'\0\1'),(255,b''),None])
        with patch.object(ble,'RESPONSE_TIMEOUT',0.02),patch.object(ble,'known_devices',AsyncMock(return_value=[types.SimpleNamespace(name='Mango3.0')] if cached else [])), patch.dict(sys.modules,{'bleak':types.SimpleNamespace(BleakClient=Client,BleakScanner=Scanner)}),patch.object(ble,'read_request',lambda:next(requests)),patch.object(ble,'reply',lambda *args:replies.append(args)):
            asyncio.run(ble.run())
        self.assertEqual(replies[0],(0,))
        self.assertEqual(len(writes),7 if fault=='drop_read' else 6) # handshake, three chunks, write, readback; no arbitrary command
        self.assertEqual(replies[1][2],b''.join(i.to_bytes(2,'big')+(100+i).to_bytes(2,'big') for i in range(8)))
        self.assertEqual(sum(cmd==3 for cmd,_ in writes),1)
        if fault=='drop_write':self.assertEqual(replies[2],(2,))
        self.assertEqual(replies[3][2],b'\0\1\x02\x58')
        self.assertEqual(replies[4],(6,))

    def test_ambiguous_device_rejected(self):
        class Scanner:
            @staticmethod
            async def discover(timeout):return [types.SimpleNamespace(name='Mango3.0')]*2
        with patch.object(ble,'known_devices',AsyncMock(return_value=[])), patch.dict(sys.modules,{'bleak':types.SimpleNamespace(BleakClient=None,BleakScanner=Scanner)}):
            with self.assertRaisesRegex(RuntimeError,'ambiguous'):asyncio.run(ble.run())

if __name__=='__main__':unittest.main()
