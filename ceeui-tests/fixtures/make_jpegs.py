#!/usr/bin/env python3
"""Writes the JPEG decoder's fixtures with Pillow: baseline 4:4:4, 4:2:2 and 4:2:0, greyscale, progressive, restart
intervals, odd sizes, and an EXIF orientation of 6 (a phone held upright). Each NAME.jpg has NAME.rgb, Pillow's
decoding of it (libjpeg's), turned as its orientation says: R, G, B bytes, row-major. Two correct decoders differ by
their rounding, so the test allows a difference of 3. cases.txt lists the names and sizes.

Usage: make_jpegs.py OUT_DIR
"""
import sys, math
from PIL import Image, ImageOps, ImageDraw
out=sys.argv[1]
def picture(w,h):
    im=Image.new("RGB",(w,h))
    px=im.load()
    for y in range(h):
        for x in range(w):
            px[x,y]=(int(255*x/max(1,w-1)), int(255*y/max(1,h-1)), int(128+127*math.sin((x+y)/9)))
    d=ImageDraw.Draw(im)
    d.ellipse((w//5,h//5,w*3//5,h*3//5),fill=(240,30,60))
    d.rectangle((w//2,h//2,w-3,h-3),outline=(10,200,40),width=3)
    return im
cases=[]
def save(name, im, **kw):
    im.save(f"{out}/{name}.jpg","JPEG",**kw)
    ref=Image.open(f"{out}/{name}.jpg")
    ref=ImageOps.exif_transpose(ref).convert("RGB")
    open(f"{out}/{name}.rgb","wb").write(ref.tobytes())
    cases.append((name,ref.size[0],ref.size[1]))
save("s444", picture(64,48), quality=95, subsampling=0)
save("s422", picture(64,48), quality=90, subsampling=1)
save("s420", picture(75,53), quality=85, subsampling=2)
save("grey", picture(40,33).convert("L"), quality=90)
save("progressive", picture(97,61), quality=88, progressive=True, subsampling=2)
save("progressive444", picture(50,50), quality=92, progressive=True, subsampling=0)
save("restarts", picture(80,64), quality=90, subsampling=2, restart_marker_blocks=3)
ex=Image.Exif(); ex[0x0112]=6
save("rotated", picture(60,40), quality=92, subsampling=0, exif=ex.tobytes())
save("large", picture(160,120), quality=80, subsampling=2)
open(f"{out}/cases.txt","w").write("\n".join(f"{n} {w} {h}" for n,w,h in cases)+"\n")
print(open(f"{out}/cases.txt").read())
