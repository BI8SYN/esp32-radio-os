#!/usr/bin/env python3
"""Real-device regression for a RADIO_UI_TEST=ON build. Never submits Wi-Fi credentials or installs OTA.
Uses pyserial. Screenshots are LVGL renderings produced on the board, not camera photos.
"""
import argparse, base64, json, re, struct, time, zlib
from pathlib import Path
import serial

class Device:
    def __init__(self, port, output):
        self.out = Path(output); self.out.mkdir(parents=True, exist_ok=True)
        self.lines = []
        self.port = serial.Serial(port=None, baudrate=115200, timeout=.15)
        self.port.dtr = False; self.port.rts = False; self.port.port = port; self.port.open()
        self.read(8)  # USB opening can reset a board; allow startup/network time.
    def read(self, seconds):
        end = time.monotonic() + seconds; result = []
        while time.monotonic() < end:
            line = self.port.readline().decode(errors='replace').strip()
            if not line: continue
            if any(term in line for term in ('Guru Meditation', 'assert failed', 'CORRUPT HEAP', 'Stack canary', 'Task watchdog got triggered')):
                raise AssertionError(line)
            if 'TOUCH_POINT ' in line: line = line[line.index('TOUCH_POINT '):]
            if 'TOUCH_SUMMARY ' in line: line = line[line.index('TOUCH_SUMMARY '):]
            if line.startswith(('STATUS', 'SELFTEST', 'RESULT', 'UITEST', 'UIFRAME', 'TOUCH_')):
                line = re.sub(r'ssid="[^"]*"', 'ssid="[redacted]"', line)
                result.append(line)
                if not line.startswith('UIFRAME DATA'): self.lines.append(line)
                if line == 'UIFRAME END': break
        return result
    def command(self, command, wait=.6):
        self.lines.append('> '+command)
        self.port.write((command+'\n').encode()); return self.read(wait)
    def status(self):
        return next(x for x in self.command('status') if x.startswith('STATUS'))
    def state(self):
        line = next(x for x in self.command('uitest state') if x.startswith('UITEST STATE'))
        return {k:int(v) for k,v in re.findall(r'(\w+)=(-?\d+)', line)}
    def screen(self, name, number):
        self.command('screen '+name); assert self.state()['screen'] == number, name
    def touch(self, x, y, end_x=None, end_y=None):
        tail = '' if end_x is None else f' {end_x} {end_y}'
        lines = self.command(f'uitest touch {x} {y}'+tail, .9)
        assert 'UITEST TOUCH queued' in lines
        return lines
    def frame(self, name):
        lines = self.command('uitest frame', 9)
        header = next(x for x in lines if x.startswith('UIFRAME BEGIN')).split()
        w,h,size = map(int,header[2:]); blocks = {}
        for line in lines:
            if line.startswith('UIFRAME DATA'):
                _,_,offset,data = line.split(); blocks[int(offset)] = base64.b64decode(data,validate=True)
        raw = b''.join(blocks[k] for k in sorted(blocks)); assert len(raw)==size==w*h*2 and 'UIFRAME END' in lines
        # Board uses RGB565 with LV_COLOR_16_SWAP=y (big-endian bytes on the wire).
        pixels = bytearray()
        for y in range(h):
            pixels.append(0)
            for x in range(w):
                v = int.from_bytes(raw[(y*w+x)*2:(y*w+x)*2+2], 'big')
                pixels.extend((((v>>11)&31)*255//31, ((v>>5)&63)*255//63, (v&31)*255//31))
        def chunk(kind,data): return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        png=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',w,h,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
        (self.out/(name+'.png')).write_bytes(png)
    def close(self):
        (self.out/'serial-results.log').write_text('\n'.join(self.lines)+'\n'); self.port.close()

def smoke(d):
    assert any('SELFTEST PASS' in x for x in d.command('selftest'))
    initial = d.status(); (d.out/'initial-status.txt').write_text(initial+'\n')
    original = dict(re.findall(r'(volume|hue|accent)=([0-9]+)', initial))
    original_theme = re.search(r'theme=(\w+)/',initial)[1]
    original_state = d.state()
    d.command('stop');d.command('theme light');d.screen('player',0);d.frame('player-light')
    for name,number in [('stations',1),('filter',3),('settings',2),('about',6),('diagnostics',8),('touch',9),('ota',7),('wifi',4),('wifi-pass',5)]:
        d.screen(name,number);d.frame(name);print('PASS screen',name,flush=True)
        if name == 'settings':
            d.touch(285,184); assert d.state()['time24'] == 0
            d.touch(217 if original_state['time24'] else 285,184)
        if name == 'filter':
            d.touch(136,85); d.touch(24,20)
            assert d.state()['category'] == original_state['category']
            d.screen('filter',3); d.touch(136,85); d.touch(160,217)
            assert d.state()['category'] == 1
            d.command('filter 0 0')
        if name == 'touch':
            points = []
            for x,y in [(28,92),(292,92),(160,145),(28,215),(292,215)]: points += d.touch(x,y)
            assert any('TOUCH_SUMMARY mean=+0,+0 spread=0,0' in line for line in points), points
            d.frame('touch-completed')
    # Exit during scan, change theme, return. Worker may finish after page teardown.
    for _ in range(3):
        d.command('screen wifi',.1);d.command('screen settings',.1);d.command('theme dark',.2);d.command('theme light',1)
    d.screen('player',0)
    d.touch(28,20);d.frame('drawer')
    d.command('drawer close');d.command('screen theme-color');d.frame('theme-picker')
    d.touch(123,203);d.touch(228,153);assert 'accent=1' in d.status()
    d.command('screen theme-color');d.touch(191,203);d.touch(285,28)
    assert 'accent=1' in d.status()  # Cancel does not commit the preview.
    d.command('accent hue '+original['hue'])
    if original['accent'] != '4': d.command('accent '+original['accent'])
    d.screen('player',0);d.command('theme dark');d.frame('player-dark')
    # Full rebuilds stress page-owned buffers and lazy info-page cleanup.
    d.command('theme light'); baseline=d.command('selftest');before=d.status()
    for i in range(12):
        d.screen('about',6);d.screen('ota',7);d.screen('diagnostics',8);d.screen('player',0)
        d.command('theme '+('dark' if i%2==0 else 'light'))
    after=d.command('selftest');assert any('SELFTEST PASS' in x for x in after)
    def metric(lines,key):return int(re.search(r'\b'+key+r'=(\d+)',next(x for x in lines if x.startswith('SELFTEST')))[1])
    assert metric(after,'internal') >= metric(baseline,'internal') - 4096
    assert metric(after,'psram') >= metric(baseline,'psram') - 4096
    # Timer expiration, cancel and wake touch must not activate the covered control.
    d.command('sleep 1',1.8); assert d.state()['awake']==0
    d.touch(160,184);assert d.state()['awake']==1;assert 'player=stopped' in d.status()
    d.command('sleep 1',.1);d.command('sleep off',1.5);assert d.state()['awake']==1
    # Actual audio/network pipeline plus touch favorites and volume isolation.
    d.command('play 0',8); live=d.status()
    if 'player=playing' not in live:
        d.command('play 100',10);live=d.status()
    assert 'player=playing' in live, live
    assert int(re.search(r'rate=(\d+)',live)[1])>0
    d.frame('playing')
    count=int(re.search(r'favorites=(\d+)',live)[1]);d.touch(285,185)
    changed=int(re.search(r'favorites=(\d+)',d.status())[1]);assert abs(changed-count)==1
    d.touch(285,185);assert int(re.search(r'favorites=(\d+)',d.status())[1])==count
    station=re.search(r'station="([^"]*)"',d.status())[1]
    d.touch(110,227,240,227); after_drag=d.status()
    assert 'player=playing' in after_drag and re.search(r'station="([^"]*)"',after_drag)[1]==station
    d.command('volume '+original['volume']);d.command('stop');d.command('theme '+original_theme)
    d.command('screen player');assert any('SELFTEST PASS' in x for x in d.command('selftest'))
    print('PASS lifecycle, theme cycles, Wi-Fi scanning, sleep/wake, favorites, playback and volume',flush=True)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--port',required=True);parser.add_argument('--output',required=True)
    args=parser.parse_args();device=Device(args.port,args.output)
    try:smoke(device)
    finally:device.close()
