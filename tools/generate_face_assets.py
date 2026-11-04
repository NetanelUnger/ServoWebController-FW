"""Original NatiLab face artwork -> shared 256-color RGB565 flash assets.

Run with Python, Pillow and numpy. Artwork is supersampled at 4x and reduced
before palette conversion. No downloaded artwork, runtime fonts or rasterizers.
The generated C file is checked in; normal firmware builds need no Python.
"""
from pathlib import Path
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'STM32CubeIDE/Application/User/Core/display'
S = 4
W, H = 68, 88
MW, MH = 64, 40


def canvas(w, h):
    return Image.new('RGBA', (w*S, h*S))


def ellipse(im, box, color, outline=None, width=1):
    ImageDraw.Draw(im).ellipse(tuple(round(v*S) for v in box), fill=color, outline=outline, width=round(width*S))


def line(im, points, color, width):
    points = [(round(x*S), round(y*S)) for x, y in points]
    ImageDraw.Draw(im).line(points, fill=color, width=round(width*S), joint='curve')
    r = width*S/2
    for x, y in (points[0], points[-1]):
        ImageDraw.Draw(im).ellipse((int(x-r), int(y-r), int(x+r), int(y+r)), fill=color)


def curve(im, points, color, width):
    p = np.array(points, dtype=float)
    values = []
    for t in np.linspace(0, 1, 80):
        q = (1-t)**3*p[0] + 3*(1-t)**2*t*p[1] + 3*(1-t)*t*t*p[2] + t**3*p[3]
        values.append(tuple(q))
    line(im, values, color, width)


def gradient_ellipse(im, box, top, bottom):
    x0, y0, x1, y1 = [round(v*S) for v in box]
    w, h = x1-x0+1, y1-y0+1
    t = np.linspace(0, 1, h)[:, None, None]
    rgb = np.tile(np.array(top)[None,None,:]*(1-t) + np.array(bottom)[None,None,:]*t, (1,w,1)).astype(np.uint8)
    layer = Image.fromarray(rgb).convert('RGBA')
    mask = Image.new('L', (w,h)); ImageDraw.Draw(mask).ellipse((0,0,w-1,h-1),fill=255)
    im.paste(layer, (x0,y0), mask)


def glow(im, box, color, blur=5):
    layer = canvas(im.width//S, im.height//S)
    ellipse(layer, box, color)
    im.alpha_composite(layer.filter(ImageFilter.GaussianBlur(blur*S)))


def star(im, cx, cy, size, color):
    points = [(cx,cy-size),(cx+size*.27,cy-size*.27),(cx+size,cy),
              (cx+size*.27,cy+size*.27),(cx,cy+size),(cx-size*.27,cy+size*.27),
              (cx-size,cy),(cx-size*.27,cy-size*.27)]
    ImageDraw.Draw(im).polygon([(round(x*S),round(y*S)) for x,y in points], fill=color)


def backdrop():
    yy,xx=np.mgrid[0:240*S,0:240*S]/S
    radius=np.minimum(1,np.hypot(xx-120,yy-108)/135)[...,None]
    a=np.array([31,44,98]); b=np.array([6,11,30])
    im=Image.fromarray((a*(1-radius)+b*radius).astype(np.uint8)).convert('RGBA')
    glow(im,(15,13,225,228),(22,124,202,90),7)
    ellipse(im,(9,9,231,231),None,'#324e86',1)
    for start,end,col in [(205,255,'#64eff0'),(285,330,'#bca2ff'),(25,70,'#fb8eac')]:
        ImageDraw.Draw(im).arc((9*S,9*S,231*S,231*S),start,end,fill=col,width=2*S)
    # Ear pods and a satin lavender shell around the dark glass faceplate.
    gradient_ellipse(im,(12,85,42,154),(105,226,241),(40,74,134))
    gradient_ellipse(im,(198,85,228,154),(173,143,245),(62,59,134))
    line(im,[(24,105),(24,132)],'#a5ffef',3)
    line(im,[(216,105),(216,132)],'#ddbcff',3)
    gradient_ellipse(im,(26,25,214,218),(214,216,255),(91,113,182))
    gradient_ellipse(im,(29,27,211,212),(114,137,200),(47,67,124))
    gradient_ellipse(im,(33,33,207,207),(29,48,87),(12,25,52))
    # Reflective upper rim, chin badge and little forehead lights.
    curve(im,[(54,54),(82,25),(157,25),(184,54)],'#b4e5ff',1.2)
    for x,c in [(112,'#7af6d4'),(120,'#acb5ff'),(128,'#ff9dc1')]:
        ellipse(im,(x-1,43,x+1,45),c)
    line(im,[(108,200),(132,200)],'#8bafe9',2)
    line(im,[(115,200),(125,200)],'#c3faff',2)
    # Colored cheeks, tiny freckles and outer decorative sparkles.
    for x in (54,186):
        glow(im,(x-13,143,x+13,157),(250,89,155,100),4)
        gradient_ellipse(im,(x-10,147,x+10,153),(252,135,180),(177,66,128))
        for dx in (-5,0,5): ellipse(im,(x+dx-.7,141,x+dx+.7,142.4),'#f0a3cb')
    star(im,30,61,4,'#9be8f4'); star(im,207,64,3,'#ffc98b')
    star(im,40,183,3,'#cdb6ff'); star(im,202,180,4,'#8fe8d3')
    ellipse(im,(117,139,123,143),'#577fb0')
    return im.resize((240,240),Image.Resampling.LANCZOS)


def eye(gaze=(0,0), opening=1., mood='normal'):
    im=canvas(W,H)
    # Each frame carries the eyebrow too, allowing curious and delighted poses.
    brow_y=11 if mood=='surprise' else 15 if mood=='happy' else 17
    curve(im,[(12,brow_y+2),(26,brow_y-5),(43,brow_y-5),(55,brow_y+1)],'#19263f',5)
    curve(im,[(12,brow_y),(26,brow_y-7),(43,brow_y-7),(55,brow_y-1)],'#9cd8f3',2.5)
    if opening==0 or mood=='happy':
        points=[(12,55),(24,46),(42,46),(56,55)] if mood=='happy' else [(12,51),(24,58),(43,58),(56,51)]
        curve(im,points,'#071324',7)
        curve(im,[(x,y-1) for x,y in points],'#7fe7ed',3)
        return im.resize((W,H),Image.Resampling.LANCZOS)
    cx,cy=34,51
    ry=27*opening
    mask=Image.new('L',(W*S,H*S))
    ImageDraw.Draw(mask).ellipse((8*S,int((cy-ry)*S),60*S,int((cy+ry)*S)),fill=255)
    eye_layer=canvas(W,H)
    gradient_ellipse(eye_layer,(5,21,63,83),(12,28,52),(5,12,30))
    gradient_ellipse(eye_layer,(8,24,60,78),(216,234,252),(253,246,237))
    # Blue/teal irises with radial fibers baked into the sprite.
    ix,iy=cx+gaze[0],cy+gaze[1]
    pupil=8 if mood!='surprise' else 6
    gradient_ellipse(eye_layer,(ix-19,iy-21,ix+19,iy+21),(13,75,142),(27,223,201))
    ellipse(eye_layer,(ix-19,iy-21,ix+19,iy+21),None,'#133b6e',1.4)
    for k in range(36):
        theta=k*math.tau/36
        r0,r1=11+(k%3),17+(k%2)
        col=['#58f3dc','#1cbbaa','#278fc6'][k%3]
        line(eye_layer,[(ix+math.cos(theta)*r0,iy+math.sin(theta)*r0),
                        (ix+math.cos(theta)*r1,iy+math.sin(theta)*r1*1.1)],col,.7)
    ellipse(eye_layer,(ix-pupil-2,iy-pupil-3,ix+pupil+2,iy+pupil+3),'#14658a')
    gradient_ellipse(eye_layer,(ix-pupil,iy-pupil-1,ix+pupil,iy+pupil+1),(4,12,32),(10,35,64))
    ellipse(eye_layer,(ix-11,iy-14,ix-1,iy-4),'#ffffff')
    ellipse(eye_layer,(ix+6,iy+7,ix+10,iy+11),'#c2ffff')
    ellipse(eye_layer,(ix+3,iy-14,ix+5,iy-12),'#e9ffff')
    im.paste(eye_layer,(0,0),mask)
    # Lids frame the opening; the top is slightly heavier like a soft lash line.
    ImageDraw.Draw(im).arc((8*S,int((cy-ry)*S),60*S,int((cy+ry)*S)),180,360,fill='#08162e',width=2*S)
    ImageDraw.Draw(im).arc((8*S,int((cy-ry)*S),60*S,int((cy+ry)*S)),0,180,fill='#5381aa',width=S)
    return im.resize((W,H),Image.Resampling.LANCZOS)


def mouth(kind):
    im=canvas(MW,MH)
    if kind=='smile':
        curve(im,[(12,12),(22,27),(44,27),(53,11)],'#060f24',6)
        curve(im,[(12,11),(22,25),(44,25),(53,10)],'#a4e6ec',2)
    elif kind=='oh':
        gradient_ellipse(im,(23,5,42,31),(6,11,26),(52,35,73))
        ImageDraw.Draw(im).arc((23*S,5*S,42*S,31*S),5,175,fill='#b9e1f7',width=S)
        ellipse(im,(28,24,38,29),'#f58db5')
    else:
        # Open happy mouth with ivory teeth, coral tongue and a colored lower lip.
        gradient_ellipse(im,(10,5,54,33),(6,13,29),(70,31,71))
        ImageDraw.Draw(im).rounded_rectangle((14*S,5*S,50*S,12*S),radius=3*S,fill='#e5f8ff')
        gradient_ellipse(im,(19,22,46,33),(255,151,186),(219,77,145))
        curve(im,[(11,20),(15,36),(48,39),(54,20)],'#85cbe8',1.3)
    return im.resize((MW,MH),Image.Resampling.LANCZOS)


def c_array(name, values, typename='uint8_t', columns=24):
    return f'const {typename} {name}[{len(values)}] = {{\n' + ''.join(
        '    '+','.join(f'0x{int(v):04x}' if typename=='uint16_t' else str(int(v)) for v in values[i:i+columns])+',\n'
        for i in range(0,len(values),columns))+'};\n\n'


def main():
    bg=backdrop()
    specs=[('CENTER',(0,0),1,'normal'),('LEFT_HALF',(-4,0),1,'normal'),
           ('LEFT',(-8,0),1,'normal'),('RIGHT_HALF',(4,0),1,'normal'),('RIGHT',(8,0),1,'normal'),
           ('UP_HALF',(0,-3),1,'normal'),('UP',(0,-6),1,'surprise'),
           ('BLINK_75',(0,0),.75,'normal'),('BLINK_50',(0,0),.5,'normal'),
           ('BLINK_25',(0,0),.25,'normal'),('CLOSED',(0,0),0,'normal'),
           ('HAPPY',(0,0),0,'happy'),('WIDE',(0,0),1,'surprise')]
    # Flatten antialiased alpha against the exact backdrop at each placement.
    # Store two banks: this preserves the asymmetric shading behind each eye.
    images=[bg.convert('RGB')]
    names=['BACKGROUND']
    for side,x in [('L',44),('R',128)]:
        for name,gaze,op,mood in specs:
            patch=bg.crop((x,56,x+W,56+H)); patch.alpha_composite(eye(gaze,op,mood))
            images.append(patch.convert('RGB')); names.append(side+'_'+name)
    for kind in ('smile','happy','oh'):
        patch=bg.crop((88,153,88+MW,153+MH)); patch.alpha_composite(mouth(kind))
        images.append(patch.convert('RGB')); names.append('MOUTH_'+kind.upper())
    # The initial full-screen image is already complete; no empty-face flash.
    for name,position in [('L_CENTER',(44,56)),('R_CENTER',(128,56)),('MOUTH_SMILE',(88,153))]:
        images[0].paste(images[names.index(name)],position)

    # One stable palette for every scene part avoids seams and palette flicker.
    sample=Image.new('RGB',(240,240+len(images[1:])*H))
    sample.paste(images[0],(0,0))
    for i,im in enumerate(images[1:]): sample.paste(im,(0,240+i*H))
    pal=sample.quantize(colors=256,method=Image.Quantize.MEDIANCUT)
    rgb=np.array(pal.getpalette(),dtype=np.uint16).reshape(-1,3)
    palette=((rgb[:,0]>>3)<<11)|((rgb[:,1]>>2)<<5)|(rgb[:,2]>>3)
    data=[]; records=[]; decoded=[]
    # Lossless per-row RLE after palette conversion. Each run is count,index.
    # Row offsets allow dirty rectangles without decoding preceding scanlines.
    rows=[]
    for im in images:
        indexed=im.quantize(palette=pal,dither=Image.Dither.NONE)
        a=np.array(indexed)
        start=len(rows)
        for row in a:
            rows.append(len(data))
            i=0
            while i<len(row):
                j=i+1
                while j<len(row) and row[j]==row[i] and j-i<255: j+=1
                data.extend((j-i,int(row[i]))); i=j
        records.append((start,im.width,im.height))
        decoded.append(indexed.convert('RGB'))
    rows.append(len(data))
    assert len(data)<190*1024, 'Asset budget exceeded'
    header='''/* Generated by tools/generate_face_assets.py; original project artwork. */
#ifndef FACE_ASSETS_H
#define FACE_ASSETS_H
#include <stdint.h>
typedef struct { uint16_t row; uint16_t width, height; } FaceAsset;
'''
    header+='enum {\n'+''.join(f'    FACE_{name} = {i},\n' for i,name in enumerate(names))+'    FACE_ASSET_COUNT\n};\n'
    header+='extern const uint16_t face_palette[256];\nextern const uint32_t face_rows[];\nextern const uint8_t face_pixels[];\nextern const FaceAsset face_assets[FACE_ASSET_COUNT];\n#endif\n'
    (OUT/'face_assets.h').write_text(header)
    code='/* Generated artwork: edit tools/generate_face_assets.py, then regenerate. */\n#include "face_assets.h"\n\n'
    code+=c_array('face_palette',palette.tolist(),'uint16_t',12)
    code+=c_array('face_rows',rows,'uint32_t',12)
    code+='const FaceAsset face_assets[FACE_ASSET_COUNT] = {\n'+''.join(f'    {{{r},{w},{h}}}, /* {n} */\n' for n,(r,w,h) in zip(names,records))+'};\n\n'
    code+=c_array('face_pixels',data)
    (OUT/'face_assets.c').write_text(code)
    preview=decoded[0].copy()
    preview.paste(decoded[names.index('L_CENTER')],(44,56))
    preview.paste(decoded[names.index('R_CENTER')],(128,56))
    preview.paste(decoded[names.index('MOUTH_SMILE')],(88,153))
    target=ROOT/'STM32CubeIDE/Debug'; target.mkdir(exist_ok=True)
    preview.resize((720,720),Image.Resampling.NEAREST).save(target/'face-artwork.png')
    print(f'{len(images)} assets; {len(data)+len(rows)*4+512+len(records)*6:,} flash bytes; palette 256 colors')


if __name__=='__main__': main()
