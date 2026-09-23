#!/usr/bin/env python3
"""Shuffle-specific device checks; run on RADIO_UI_TEST=ON, then restore saved NVS.
Leaves shuffle enabled so the caller can reset and verify persistence.
"""
import argparse
from serial_ui import Device

def station(d):
    return d.state()['station_id']

def check(d):
    d.screen('player', 0)
    d.command('stop')
    if d.state()['shuffle']:
        d.touch(248, 20)
    before = station(d)
    d.touch(248, 20)
    assert d.state()['shuffle'] == 1
    assert station(d) == before
    assert 'player=stopped' in d.status()
    d.frame('shuffle-on')
    d.command('play 0', 5)
    for _ in range(3):
        previous = station(d)
        d.touch(225, 184)
        assert station(d) != previous
        d.touch(94, 184)
        assert station(d) == previous
    d.command('stop')
    d.command('theme dark')
    assert d.state()['shuffle'] == 1
    d.frame('shuffle-dark')
    d.screen('settings', 2)
    d.touch(285, 184)
    assert d.state()['time24'] == 0
    d.screen('player', 0)
    d.command('sleep 5400')
    d.frame('shuffle-clock-sleep')
    # Adjacent hit targets must not toggle shuffle or accidentally change playback.
    d.touch(292, 20)
    assert d.state()['screen'] == 4 and d.state()['shuffle'] == 1
    d.screen('player', 0)
    d.touch(27, 20)
    d.frame('drawer')
    d.touch(55, 78)
    assert d.state()['awake'] == 0
    d.touch(248, 20)
    assert d.state()['awake'] == 1 and d.state()['shuffle'] == 1
    d.command('sleep off')
    d.command('theme light')
    d.touch(248, 20)
    assert d.state()['shuffle'] == 0
    d.frame('shuffle-off')
    d.touch(248, 20)
    assert d.state()['shuffle'] == 1
    assert any('SELFTEST PASS' in line for line in d.command('selftest'))
    print('PASS shuffle toggle/history, theme rebuild, 12h+sleep, adjacent targets and wake', flush=True)

if __name__ == '__main__':
    p = argparse.ArgumentParser()
    p.add_argument('--port', required=True)
    p.add_argument('--output', required=True)
    a = p.parse_args()
    device = Device(a.port, a.output)
    try:
        check(device)
    finally:
        device.close()
