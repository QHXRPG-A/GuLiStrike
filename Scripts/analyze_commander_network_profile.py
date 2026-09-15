"""Read UE 5.7 NetworkProfiler v14 evidence; no gameplay or test execution.

Format and reverse actor/property association follow Epic's local
Engine/Source/Programs/NetworkProfiler/NetworkProfiler/{Tokens,StreamParser}.cs.
RPC totals already contain their content headers. Bunch/socket totals are
containers, never added again to RPC/property payload totals.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import csv
import json
from pathlib import Path
import struct


class Reader:
    def __init__(self, path: Path):
        self.data = path.read_bytes()
        self.pos = 0

    def number(self, fmt):
        value = struct.unpack_from('<' + fmt, self.data, self.pos)[0]
        self.pos += struct.calcsize('<' + fmt)
        return value

    def packed(self):
        value = 0
        for shift in range(0, 35, 7):
            byte = self.number('B')
            value |= (byte >> 1) << shift
            if not byte & 1:
                return value
        raise ValueError('Invalid packed integer')

    def string(self):
        count = self.number('I')
        value = self.data[self.pos:self.pos + count].decode('ascii')
        self.pos += count
        return value.rstrip('\x00')

    def bit_array(self):
        count = self.packed()
        return [self.packed() for _ in range((count + 31) // 32)]


def read_profile(path):
    reader = Reader(path)
    magic, version = reader.number('I'), reader.number('I')
    if magic != 0x1DBF348C or version != 14:
        raise ValueError(f'Expected UE 5.7 nprof v14, got {magic:x}/{version}')
    header = dict(version=version, tag=reader.string(), game=reader.string(), url=reader.string())
    names, connections, events, frames, pending = [], [], [], [], []
    connection, timestamp = 0, None
    token_counts = Counter()
    while True:
        token = reader.number('B')
        token_counts[token] += 1
        if token == 6:
            break
        if token == 17:
            names.append(reader.string())
            continue
        if token == 18:
            connections.append(reader.string())
            continue
        if token == 16:
            connection = reader.packed()
            continue
        if token == 0:
            timestamp = reader.number('f')
            frames.append(timestamp)
            continue
        event = dict(token=token, time=timestamp, connection=connection)
        if token == 1:
            event.update(socket=names[reader.packed()], bytes=reader.number('H'),
                         packet_bits=reader.number('H'), bunch_bits=reader.number('H'),
                         ack_bits=reader.number('H'), padding_bits=reader.number('H'))
        elif token == 2:
            event.update(channel=reader.number('H'), channel_type=names[reader.packed()],
                         header_bits=reader.number('H'), payload_bits=reader.number('H'))
        elif token == 3:
            event.update(actor=names[reader.packed()], function=names[reader.packed()],
                         header_bits=reader.packed(), parameter_bits=reader.packed(), footer_bits=reader.packed())
        elif token == 4:
            event.update(flags=reader.number('B'), actor=names[reader.packed()], ms=reader.number('f'))
            # Epic emits ReplicateActor after its property tokens.
            for prop in pending:
                prop.update(actor=event['actor'], connection=connection)
            pending.clear()
        elif token in (5, 10):
            event.update(property=names[reader.packed()], bits=reader.number('H'))
            pending.append(event)
        elif token == 7:
            event.update(name=names[reader.packed()], description=names[reader.packed()])
        elif token == 8:
            byte_count = reader.number('H')
            reader.pos += byte_count
        elif token in (9, 11, 15):
            event['bits'] = reader.number('H')
        elif token == 12:
            event.update(guids=reader.number('H'), bits=reader.number('H'))
        elif token in (13, 14):
            event.update(object=names[reader.packed()], bits=reader.number('H'))
        elif token == 19:
            event.update(object=names[reader.packed()], ms=reader.number('f'))
            reader.bit_array()
            reader.bit_array()
            event['properties'] = [names[reader.packed()] for _ in range(reader.packed())]
        elif token == 20:
            event.update(object=names[reader.packed()], flags=reader.number('B'))
            reader.bit_array()
        else:
            raise ValueError(f'Unknown token {token} at {reader.pos - 1}')
        events.append(event)
    if reader.pos != len(reader.data) or pending:
        raise ValueError(f'Unconsumed stream bytes or property ownership: {reader.pos}/{len(reader.data)}, {len(pending)}')
    return header, connections, events, frames, token_counts


def network_rows(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return [dict(connection=row['connection'], **{key: float(value) for key, value in row.items()
                    if key != 'connection'}) for row in csv.DictReader(stream)]


def rpc_category(name):
    if name == 'ClientReceiveSoldierPoseChunk':
        return 'soldier_poses'
    if any(part in name for part in ('Move', 'Selection', 'Command', 'Bootstrap', 'Resync')):
        return 'commands_and_endpoints'
    if any(part in name for part in ('Combat', 'Shot', 'Effect', 'Skill', 'Wingman')):
        return 'combat_and_effects'
    return 'other_rpc'


def property_category(actor, name):
    if actor == 'GuLiSoldierStateReplicator':
        return 'soldier_roster_and_state'
    if name in ('SelectionState', 'MoveEndpoints', 'SyncGeneration', 'BootstrapBinding'):
        return 'commands_and_endpoints'
    return 'world_buildings_and_other_state'


def analyze(run: Path, start_offset: float, seconds: float):
    profile = run / 'server/network.nprof'
    header, addresses, events, frames, token_counts = read_profile(profile)
    summary = json.loads((run / 'server/summary.json').read_text(encoding='utf-8-sig'))
    # Whole engine frames in the central part of the shared steady-state window.
    desired_start = summary['capture_start_engine_seconds'] + start_offset
    start = next(t for t in frames if t >= desired_start)
    end = next(t for t in frames if t >= start + seconds)
    duration = end - start
    selected = [e for e in events if e['time'] is not None and start <= e['time'] < end]
    server_rows = network_rows(run / 'server/network.csv')
    engine_offset = summary['capture_start_monotonic_seconds'] - summary['capture_start_engine_seconds']
    monotonic_start, monotonic_end = start + engine_offset, end + engine_offset
    result = dict(profile=str(profile.resolve()), header=header, token_counts=token_counts,
                  full_frame_range=[frames[0], frames[-1]], full_events=len(events),
                  window_engine_seconds=[start, end], window_monotonic_seconds=[monotonic_start, monotonic_end],
                  measured_seconds=duration, connections=[])
    for connection_id, address in enumerate(addresses):
        data = [e for e in selected if e['connection'] == connection_id]
        packets = [e for e in data if e['token'] == 1]
        if not packets:
            continue
        rows = [row for row in server_rows if row['connection'] == address]
        packet_overheads = {int(row['packet_overhead']) for row in rows}
        if len(packet_overheads) != 1:
            raise ValueError(f'Unexpected packet overheads for {address}: {packet_overheads}')
        packet_overhead = packet_overheads.pop()
        udp_bytes = sum(e['bytes'] for e in packets)
        # FlushNet writes one connection termination bit after packet/ACK/bunch
        # bits. IpConnection profiles socket bytes after PacketHandler, whereas
        # UNetConnection totals count before Outgoing / after Incoming handlers.
        connection_packet_bytes = sum((e['packet_bits'] + e['bunch_bits'] + e['ack_bits'] + 1 + 7) // 8
                                      for e in packets)
        accounted_bytes = connection_packet_bytes + len(packets) * packet_overhead
        wire_accounted_bytes = udp_bytes + len(packets) * packet_overhead
        handler_byte_deltas = Counter(e['bytes'] - (e['packet_bits'] + e['bunch_bits'] + e['ack_bits'] + 1 + 7) // 8
                                      for e in packets)
        counts, categories = Counter(), Counter()
        rpc_details, property_details = defaultdict(Counter), defaultdict(Counter)
        for event in data:
            token = event['token']
            if token == 3:
                name = event['function']
                bits = event['header_bits'] + event['parameter_bits'] + event['footer_bits']
                categories[rpc_category(name)] += bits
                counts[rpc_category(name)] += 1
                rpc_details[event['actor'] + '.' + name].update(count=1, bits=bits,
                    header_bits=event['header_bits'], parameter_bits=event['parameter_bits'])
            elif token == 5:
                name, actor = event['property'], event['actor']
                categories[property_category(actor, name)] += event['bits']
                counts[property_category(actor, name)] += 1
                property_details[actor + '.' + name].update(count=1, bits=event['bits'])
        attributed_bits = sum(categories.values())
        residual = accounted_bytes * 8 - attributed_bits
        if residual < 0:
            raise ValueError(f'Payload double count or serialization-only data: {address}: {residual} bits')
        categories['protocol_and_unattributed'] = residual
        details = lambda items: [dict(name=name, **value, bytes=value['bits'] / 8,
                                      bytes_s=value['bits'] / 8 / duration,
                                      percent=value['bits'] / 8 / accounted_bytes * 100)
                                  for name, value in sorted(items.items(), key=lambda item: -item[1]['bits'])]
        # Counters are sampled once each engine frame; retain boundary skew rather than hide it.
        a = min(rows, key=lambda row: abs(row['engine_seconds'] - start))
        b = min(rows, key=lambda row: abs(row['engine_seconds'] - end))
        counter_bytes = b['out_total_bytes'] - a['out_total_bytes']
        result['connections'].append(dict(address=address, socket_packet_count=len(packets),
            packet_overhead_bytes=packet_overhead, udp_bytes=udp_bytes, accounted_bytes=accounted_bytes,
            wire_accounted_bytes=wire_accounted_bytes, wire_bytes_s=wire_accounted_bytes/duration,
            packet_handler_extra_bytes=udp_bytes-connection_packet_bytes,
            packet_handler_extra_bytes_histogram=dict(handler_byte_deltas),
            bytes_s=accounted_bytes / duration, counter_delta_bytes=counter_bytes,
            counter_boundary_skew_ms=[(a['engine_seconds'] - start)*1000, (b['engine_seconds'] - end)*1000],
            counter_difference_bytes=counter_bytes-accounted_bytes,
            categories=[dict(name=name, bits=bits, bytes=bits/8, bytes_s=bits/8/duration,
                             percent=bits/8/accounted_bytes*100, events=counts[name])
                        for name,bits in categories.items()],
            rpcs=details(rpc_details), properties=details(property_details),
            layers=dict(packet_overhead_bits=len(packets)*packet_overhead*8,
                        socket_packet_bits=sum(e['packet_bits'] for e in packets),
                        socket_bunch_bits=sum(e['bunch_bits'] for e in packets),
                        socket_ack_bits=sum(e['ack_bits'] for e in packets),
                        socket_padding_bits=sum(e['padding_bits'] for e in packets),
                        bunch_header_bits=sum(e['header_bits'] for e in data if e['token']==2),
                        bunch_payload_bits=sum(e['payload_bits'] for e in data if e['token']==2),
                        property_header_bits=sum(e['bits'] for e in data if e['token']==10),
                        property_handle_bits=sum(e['bits'] for e in data if e['token']==15),
                        content_begin_bits_inclusive_rpc=sum(e['bits'] for e in data if e['token']==13))))
    result['client_counters'] = []
    for role in ('client1','client2'):
        rows = network_rows(run / role / 'network.csv')
        if rows[0]['monotonic_seconds'] > monotonic_start or rows[-1]['monotonic_seconds'] < monotonic_end:
            raise ValueError(f'{role} does not cover the shared window')
        a = min(rows, key=lambda row: abs(row['monotonic_seconds'] - monotonic_start))
        b = min(rows, key=lambda row: abs(row['monotonic_seconds'] - monotonic_end))
        byte_delta = b['in_total_bytes'] - a['in_total_bytes']
        result['client_counters'].append(dict(role=role, received_bytes=byte_delta,
            measured_seconds=b['monotonic_seconds']-a['monotonic_seconds'],
            bytes_s=byte_delta/(b['monotonic_seconds']-a['monotonic_seconds']),
            boundary_skew_ms=[(a['monotonic_seconds']-monotonic_start)*1000,
                              (b['monotonic_seconds']-monotonic_end)*1000]))
    totals = Counter()
    all_bytes = sum(item['accounted_bytes'] for item in result['connections'])
    for connection in result['connections']:
        for category in connection['categories']:
            totals[category['name']] += category['bits']
    result['aggregate_categories'] = [dict(name=name, bytes=bits/8, bytes_s_per_client=bits/8/duration/len(result['connections']),
                                           percent=bits/8/all_bytes*100)
                                      for name,bits in sorted(totals.items(), key=lambda item: -item[1])]
    result['per_client_mean_bytes_s'] = all_bytes/duration/len(result['connections'])
    result['per_client_mean_wire_bytes_s'] = sum(c['wire_accounted_bytes'] for c in result['connections'])/duration/len(result['connections'])
    framing = {name:sum(c['layers'][name] for c in result['connections'])
               for name in ('packet_overhead_bits','bunch_header_bits','socket_packet_bits','socket_ack_bits')}
    framing['other_framing_and_unattributed'] = totals['protocol_and_unattributed'] - sum(framing.values())
    result['protocol_subcategories'] = [dict(name=name, bits=bits, bytes=bits/8,
        bytes_s_per_client=bits/8/duration/len(result['connections']),percent=bits/8/all_bytes*100)
        for name,bits in framing.items()]
    result['cross_check'] = dict(
        reconstructed_connection_bytes=all_bytes,
        server_counter_bytes=sum(c['counter_delta_bytes'] for c in result['connections']),
        client_received_bytes=sum(c['received_bytes'] for c in result['client_counters']),
        server_counter_difference_percent=(sum(c['counter_delta_bytes'] for c in result['connections'])/all_bytes-1)*100,
        client_counter_difference_percent=(sum(c['received_bytes'] for c in result['client_counters'])/all_bytes-1)*100)
    result['notes'] = [
        'RPC byte attribution includes the RPC field header and content block header; it is not raw pose struct size.',
        'Properties are measured serialized values; container metadata remains in protocol_and_unattributed.',
        'Do not sum nested layer counters: RPC content headers also occur in content_begin events.',
        'Main denominator reconstructs pre-PacketHandler connection bytes from packet/ACK/bunch bits plus one termination bit, rounded per packet, plus runtime PacketOverhead; cross-checked against cumulative counters.',
        'Actual socket bytes plus PacketOverhead are separately retained as wire bytes; neither denominator includes Ethernet headers.',
        'No per-property classification of retransmitted bunches is inferred; any unclassified bytes remain explicit.',
        'Existing synthetic server-owned movement commands do not represent human mouse-command traffic.'
    ]
    (run / 'network-breakdown.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('run', type=Path)
    parser.add_argument('--start', type=float, default=5.0)
    parser.add_argument('--seconds', type=float, default=30.0)
    args = parser.parse_args()
    output = analyze(args.run, args.start, args.seconds)
    print(json.dumps({key: output[key] for key in ('measured_seconds','per_client_mean_bytes_s',
                                                 'aggregate_categories','client_counters')}, ensure_ascii=True, indent=2))
