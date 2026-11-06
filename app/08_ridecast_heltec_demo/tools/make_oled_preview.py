#!/usr/bin/env python3
"""Create illustrative 128x64 OLED screenshots for README (no board capture)."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets"
ASSETS.mkdir(exist_ok=True)
FONT = ImageFont.load_default()


def segment(draw, x, y, which):
    endpoints = [((2,0),(9,0)),((10,1),(10,8)),((10,10),(10,17)),
                 ((2,18),(9,18)),((1,10),(1,17)),((1,1),(1,8)),
                 ((2,9),(9,9))]
    a,b = endpoints[which]
    draw.line((x+a[0], y+a[1], x+b[0], y+b[1]), fill='white')


def digit(draw, x,y,d):
    bits=[0x3f,0x06,0x5b,0x4f,0x66,0x6d,0x7d,0x07,0x7f,0x6f][d]
    for seg in range(7):
        if bits & (1 << seg):
            segment(draw, x, y, seg)


def create(name, status):
    pic = Image.new("1",(128,64),0)
    d=ImageDraw.Draw(pic)
    d.text((0,-1),"RIDECAST NAV", fill=1, font=FONT)
    d.text((128-d.textbbox((0,0),status,font=FONT)[2],-1), status,fill=1,font=FONT)
    d.line((0,11,127,11),fill=1)
    for coords in [(14,44,14,29),(14,29,38,29),(38,29,30,21),(38,29,30,37)]:
        d.line(coords,fill=1)
    x=52
    for n in (2,5,0):
        digit(d,x,17,n)
        x+=14
    d.text((95,24),'m',fill=1,font=FONT)
    d.text((52,39),'TURN RIGHT',fill=1,font=FONT)
    d.line((0,51,127,51),fill=1)
    d.text((0,54),'35km/h', fill=1,font=FONT)
    d.text((128-d.textbbox((0,0),'GPS:OK',font=FONT)[2],54),'GPS:OK', fill=1,font=FONT)
    # Scale exactly 4x with nearest neighbor, no smoothed/non-native color pixels.
    big = pic.convert('RGB').resize((512,256),Image.Resampling.NEAREST)
    big.save(ASSETS/name)

create('ridecast_nav_sim.png','SIM')
create('ridecast_nav_online.png','ONLINE')
print('Wrote illustrative OLED previews')
