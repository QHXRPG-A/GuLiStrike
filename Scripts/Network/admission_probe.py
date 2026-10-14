"""Negative admission checks against the real local coordinator wire protocol."""
import base64
import hashlib
import json
import os
import socket
import struct
import uuid
from pathlib import Path


def identity(value):
    h=uuid.UUID(str(value)).hex
    return struct.pack('<4I',*(int(h[n:n+8],16) for n in range(0,32,8)))


def text(value):
    data=value.encode();return struct.pack('<I',len(data))+data


def reject(session, player, team, role, expected):
    manifest=json.loads(Path(session.plan['manifest']).read_text(encoding='utf-8-sig'))
    protocol=manifest['protocol_version']
    body=bytes((0,1 if team=='Red' else 2,('Commander','Ground','Air').index(role)))
    body+=b''.join(text(v) for v in (manifest['config_hash'],manifest['map_hash'],'127.0.0.1:1','',''))
    wire=struct.pack('<IHH',0x474c4e43,protocol,1)+identity(session.plan['room'])+struct.pack('<Q',0)+identity(player)
    wire+=bytes(16+21)+struct.pack('<4Q',0,0,1,0)+struct.pack('<I',len(body))+body
    with socket.create_connection(('127.0.0.1',session.args.coordinator_port),timeout=10) as connection:
        key=base64.b64encode(os.urandom(16)).decode()
        connection.sendall(('GET /v1/match HTTP/1.1\r\nHost: 127.0.0.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Version: 13\r\nSec-WebSocket-Key: '+key+'\r\n\r\n').encode())
        header=bytearray()
        while not header.endswith(b'\r\n\r\n'):
            value=connection.recv(1)
            if not value or len(header)>8192: raise RuntimeError('Admission WebSocket handshake failed')
            header+=value
        accept=base64.b64encode(hashlib.sha1((key+'258EAFA5-E914-47DA-95CA-C5AB0DC85B11').encode()).digest())
        if not header.startswith(b'HTTP/1.1 101 ') or accept not in header: raise RuntimeError('Invalid admission WebSocket handshake')
        mask=os.urandom(4);size=len(wire)
        prefix=bytes((0x82,0x80|size)) if size<126 else bytes((0x82,0xFE))+struct.pack('>H',size)
        connection.sendall(prefix+mask+bytes(v^mask[n%4] for n,v in enumerate(wire)))
        def read(n):
            result=bytearray()
            while len(result)<n:
                value=connection.recv(n-len(result))
                if not value: raise RuntimeError('Admission closed without a result')
                result+=value
            return bytes(result)
        frame=read(2);size=frame[1]&127
        if frame[0]!=0x82 or frame[1]&128: raise RuntimeError('Unexpected admission frame')
        if size==126: size=struct.unpack('>H',read(2))[0]
        elif size==127: size=struct.unpack('>Q',read(8))[0]
        if size>65536: raise RuntimeError('Oversized admission result')
        response=read(size)
    if len(response)<125 or struct.unpack_from('<IHH',response)!=(0x474c4e43,protocol,3):
        raise RuntimeError('Invalid request was admitted instead of rejected')
    length=struct.unpack_from('<I',response,121)[0];reason=response[125:125+length].decode()
    result={'player':player,'team':team,'role':role,'reason':reason,'expected':expected,'response_sha256':hashlib.sha256(response).hexdigest()}
    path=session.stage_output/('admission-'+uuid.uuid4().hex+'.json');session.write_json(path,result)
    if reason!=expected: raise RuntimeError('Wrong admission decision: '+reason)
    return path
