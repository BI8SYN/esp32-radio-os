#!/usr/bin/env python3
"""Real-device regression: each RTHK channel must decode and sustain data reception.
Requires pyserial and an ordinary firmware build. Does not install OTA or edit Wi-Fi.
Leaves the last tested channel selected; retains the volume and favorites.
"""
import argparse
import json
import re
from pathlib import Path
from serial_ui import Device


EXPECTED_STATIONS = {
    537: '香港电台第1台',
    538: '香港电台第2台',
    539: '香港电台第3台AM567',
    540: '香港电台第4台FM97.6/FM98.9',
    541: '香港电台第5台AM783',
    542: '香港电台普通话台AM621/FM100.9',
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--port', required=True)
    parser.add_argument('--output', required=True)
    parser.add_argument('--indices', nargs='+', type=int, choices=EXPECTED_STATIONS, default=list(EXPECTED_STATIONS))
    args = parser.parse_args()
    device = Device(args.port, args.output)
    results = []
    try:
        for index in args.indices:
            response = device.command(f'play {index}')
            assert f'RESULT play {index} ok' in response, response
            consecutive = 0
            status = ''
            for _ in range(10):
                device.read(3)
                status = device.status()
                values = {key: int(value) for key, value in
                          re.findall(r'(rate|bitrate|net_kbps|reconnects|underruns)=(\d+)', status)}
                selected = f'station="{EXPECTED_STATIONS[index]}"' in status
                healthy = (selected and 'player=playing' in status and values.get('rate', 0) > 0
                           and values.get('bitrate', 0) > 0 and values.get('net_kbps', 0) > 0)
                consecutive = consecutive + 1 if healthy else 0
                if consecutive >= 2:
                    break
            result = {'index': index, 'passed': consecutive >= 2, 'status': status}
            results.append(result)
            print(('PASS' if result['passed'] else 'FAIL'), index, status, flush=True)
            assert result['passed'], f'RTHK {index} did not decode and sustain reception'
        assert any('SELFTEST PASS' in line for line in device.command('selftest', 1))
    finally:
        Path(args.output, 'results.json').write_text(json.dumps(results, ensure_ascii=False, indent=2)+'\n')
        device.close()


if __name__ == '__main__':
    main()
