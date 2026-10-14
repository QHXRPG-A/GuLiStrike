"""Two unelevated socket fixtures; only windows_network_trace is elevated."""
from __future__ import annotations
import argparse
import gzip
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import time

from runtime_evidence import read_json_shared
from windows_network_trace import calibrate, publish, qpc

IDENTITY='two-pid-calibration'


def wait_json(path, timeout=30):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        try:
            return read_json_shared(path)
        except FileNotFoundError:
            time.sleep(.025)
    raise RuntimeError(f'Timed out waiting for {path.name}')


def receiver(folder):
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp, socket.socket() as listener:
        udp.bind(('127.0.0.1',0));udp.settimeout(90)
        listener.bind(('127.0.0.1',0));listener.listen(1);listener.settimeout(90)
        publish(folder/'receiver-ready.json',{'pid':os.getpid(),'udp_port':udp.getsockname()[1],'tcp_port':listener.getsockname()[1]})
        wait_json(folder/'go.json',90)
        udp_bytes=0
        for size in (127,499,999):
            data,peer=udp.recvfrom(2048)
            if len(data)!=size:raise RuntimeError('UDP calibration payload changed')
            udp_bytes+=len(data)
        tcp,peer=listener.accept()
        with tcp:
            tcp.settimeout(5);tcp_bytes=0
            for size in (137,509,997):
                remaining=size
                while remaining:
                    data=tcp.recv(remaining)
                    if not data:raise RuntimeError('TCP calibration closed early')
                    remaining-=len(data);tcp_bytes+=len(data)
        publish(folder/'receiver-result.json',{'pid':os.getpid(),'udp_bytes':udp_bytes,'tcp_bytes':tcp_bytes})


def sender(folder):
    target=wait_json(folder/'receiver-ready.json',90)
    publish(folder/'sender-ready.json',{'pid':os.getpid()})
    wait_json(folder/'go.json',90)
    expected=[]
    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as udp:
        udp.bind(('127.0.0.1',0))
        for size in (127,499,999):
            sent=udp.sendto(bytes(size),('127.0.0.1',target['udp_port']))
            expected.append({'protocol':'UDP','source_port':udp.getsockname()[1],'destination_port':target['udp_port'],'socket_bytes':sent})
    with socket.socket() as tcp:
        tcp.settimeout(5);tcp.connect(('127.0.0.1',target['tcp_port']))
        for size in (137,509,997):
            tcp.sendall(bytes(size))
            expected.append({'protocol':'TCP','source_port':tcp.getsockname()[1],'destination_port':target['tcp_port'],'socket_bytes':size})
    publish(folder/'sender-result.json',{'pid':os.getpid(),'expected':expected})


def prepare(folder):
    if folder.exists():
        if (not (folder/'helper-ready.json').exists() or (folder/'helper-exited.json').exists()
                or (folder/'processes.json').exists() or (folder/'receiver-ready.json').exists()):
            raise RuntimeError('Use a new owned calibration directory or a ready helper with no prior fixture')
    else:
        folder.mkdir(parents=True)
    processes=[]
    for role in ('receiver','sender'):
        arguments=[sys.executable,str(Path(__file__).resolve()),'--'+role,str(folder)]
        with (folder/f'{role}-stdout.log').open('w',encoding='utf-8') as out, (folder/f'{role}-stderr.log').open('w',encoding='utf-8') as err:
            child=subprocess.Popen(arguments,stdout=out,stderr=err,creationflags=subprocess.CREATE_NO_WINDOW)
        processes.append({'pid':child.pid,'role':role,'arguments':arguments,'elevated':False})
        wait_json(folder/f'{role}-ready.json',10)
    publish(folder/'processes.json',{'processes':processes,'capture_id':IDENTITY})
    publish(folder/'request.json',{'operation':'start','id':IDENTITY,'pids':[p['pid'] for p in processes]})
    return processes


def finish(folder):
    result={'passed':False,'two_pid_attribution':False,'fixtures_elevated':False}
    try:
        processes=wait_json(folder/'processes.json')['processes']
        ready=wait_json(folder/f'{IDENTITY}-ready.json',60)
        begin=qpc();publish(folder/'go.json',{'begin_qpc':begin})
        sent=wait_json(folder/'sender-result.json',10);received=wait_json(folder/'receiver-result.json',10);end=qpc()
        publish(folder/'request.json',{'operation':'stop','id':IDENTITY,'begin_qpc':begin,'end_qpc':end})
        report=wait_json(folder/f'{IDENTITY}-report.json',20)
        with gzip.open(folder/report['event_file'],'rt',encoding='utf-8') as source:
            report['events']=[json.loads(line) for line in source]
        totals={}
        for item in sent['expected']:
            key=(item['protocol'],item['source_port'],item['destination_port'])
            totals[key]=totals.get(key,0)+item['socket_bytes']
        calibration,errors=calibrate(report,totals)
        for protocol,size in [('UDP',received['udp_bytes']),('TCP',received['tcp_bytes'])]:
            for direction,pid in [('send',sent['pid']),('receive',received['pid'])]:
                events=[e for e in report['events'] if e['protocol']==protocol and e['direction']==direction]
                if not events or {e['pid'] for e in events}!={pid} or sum(e['size'] for e in events)!=size:
                    errors.append(f'{protocol}/{direction}: expected payload PID {pid} and {size} bytes')
        result.update(passed=bool(report['valid'] and not errors),two_pid_attribution=not errors,
                      pids=ready['pids'],trace=report,calibration=calibration,errors=errors,
                      sender=sent,receiver=received,processes=processes)
    except Exception as error:
        result['error']=str(error)
    finally:
        publish(folder/'request.json',{'operation':'shutdown','id':'calibration-shutdown'})
        publish(folder/'result.json',result)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    mode=parser.add_mutually_exclusive_group(required=True)
    for option in ('prepare','finish','sender','receiver'):mode.add_argument('--'+option,type=Path)
    arguments=parser.parse_args()
    if arguments.prepare:print(json.dumps(prepare(arguments.prepare.resolve())))
    elif arguments.finish:
        result=finish(arguments.finish.resolve())
        print(json.dumps({k:v for k,v in result.items() if k in ('passed','two_pid_attribution','pids','errors','error')}))
        raise SystemExit(0 if result['passed'] else 1)
    elif arguments.receiver:receiver(arguments.receiver.resolve())
    else:sender(arguments.sender.resolve())
