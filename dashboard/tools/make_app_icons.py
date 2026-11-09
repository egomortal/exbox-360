"""Make the PS5 home-screen icons (512x512 icon0.png) for Ex-Box 360 and Ex-Box Avatars.

Two styles, chosen in the installer under "Icons":
  Xbox style (from my files): built at install time from YOUR loading-screen art and an avatar render.
      python make_app_icons.py LOADING.png AVATAR.png OUT_DIR
  Ex-Box default: our own original artwork, already in ../installer-icons/.
      python make_app_icons.py --default OUT_DIR
No Microsoft artwork is stored in this repository; Xbox-style icons are only ever made on your PC.
"""
import sys, math
from pathlib import Path
from PIL import Image, ImageDraw, ImageFilter, ImageChops, ImageFont
import numpy as np
S=512; SS=4; N=S*SS
def radial(c_in,c_out,center=(0.5,0.42),scale=0.75,size=S):
    y,x=np.mgrid[0:size,0:size]/size
    d=np.clip(np.hypot(x-center[0],y-center[1])/scale,0,1)[...,None]
    a=np.array(c_in,float);b=np.array(c_out,float)
    return Image.fromarray((a+(b-a)*d**1.4).astype('uint8'),'RGB')
def circle_mask(size,r,cx,cy):
    m=Image.new('L',(size*SS,size*SS),0);ImageDraw.Draw(m).ellipse([(cx-r)*SS,(cy-r)*SS,(cx+r)*SS,(cy+r)*SS],255)
    return m.resize((size,size),Image.LANCZOS)
def shadow(canvas,mask,offset=(0,10),blur=14,alpha=110):
    sh=Image.new('L',canvas.size,0);sh.paste(mask,offset);sh=sh.filter(ImageFilter.GaussianBlur(blur)).point(lambda v:v*alpha//255)
    canvas.paste((0,0,0),(0,0),sh)
def find_orb(img):
    a=np.asarray(img.convert('RGB')).astype(int)
    nonwhite=(a.min(-1)<235); h=a.shape[0]
    ys,xs=np.nonzero(nonwhite[:int(h*0.6)])
    # orb = biggest blob in upper 60%: use bbox of pixels whose row has many nonwhite
    rows=np.nonzero(nonwhite[:int(h*0.6)].sum(1)>40)[0]; y0,y1=rows.min(),rows.max()
    cols=np.nonzero(nonwhite[y0:y1+1].sum(0)>40)[0]; x0,x1=cols.min(),cols.max()
    return x0,y0,x1,y1
def orb_rgba(src,dia):
    x0,y0,x1,y1=find_orb(src); cx=(x0+x1)/2; cy=(y0+y1)/2; r=max(x1-x0,y1-y0)/2+1
    crop=src.convert('RGB').crop((int(cx-r),int(cy-r),int(cx+r),int(cy+r))).resize((dia,dia),Image.LANCZOS)
    m=circle_mask(dia,dia/2-1.5,dia/2,dia/2); o=crop.convert('RGBA'); o.putalpha(m); return o
def key_white(img):
    if img.mode=='RGBA' and img.getextrema()[3][0]<255: return img
    a=np.asarray(img.convert('RGB')).astype(int); white=(a.min(-1)>242)
    from collections import deque
    h,w=white.shape; bg=np.zeros_like(white); q=deque()
    for x in range(w):
        for y in (0,h-1):
            if white[y,x] and not bg[y,x]: bg[y,x]=1;q.append((y,x))
    for y in range(h):
        for x in (0,w-1):
            if white[y,x] and not bg[y,x]: bg[y,x]=1;q.append((y,x))
    while q:
        y,x=q.popleft()
        for dy,dx in ((1,0),(-1,0),(0,1),(0,-1)):
            ny,nx=y+dy,x+dx
            if 0<=ny<h and 0<=nx<w and white[ny,nx] and not bg[ny,nx]: bg[ny,nx]=1;q.append((ny,nx))
    alpha=Image.fromarray(((~bg)*255).astype('uint8')).filter(ImageFilter.GaussianBlur(0.8))
    o=img.convert('RGBA'); o.putalpha(alpha); return o

def xbox_dashboard(loading,out):
    bg=radial((255,255,255),(196,200,204),scale=0.8).convert('RGBA')
    # green glow behind orb
    glow=Image.new('L',(S,S),0);ImageDraw.Draw(glow).ellipse([96,92,416,412],200);glow=glow.filter(ImageFilter.GaussianBlur(40))
    bg.paste((120,200,40),(0,0),glow.point(lambda v:v*0.55))
    orb=orb_rgba(loading,360); shadow(bg,orb.getchannel('A'),(76,90),16,90)
    bg.alpha_composite(orb,(76,66)); bg.convert('RGB').save(out)
def xbox_avatars(loading,avatar,out):
    bg=radial((140,215,40),(20,80,10),center=(0.5,0.38),scale=0.85).convert('RGBA')
    av=key_white(avatar); bb=av.getbbox(); av=av.crop(bb)
    w,h=av.size; head=av.crop((0,0,w,int(w*1.25)))  # head + upper body, square-ish
    t=400; head=head.resize((t,int(head.size[1]*t/head.size[0])),Image.LANCZOS)
    x=(S-t)//2; y=S-head.size[1]+6
    shadow(bg,head.getchannel('A'),(x+6,y+10),14,120); bg.alpha_composite(head,(x,y))
    orb=orb_rgba(loading,132); ring=Image.new('RGBA',(148,148),(0,0,0,0)); ImageDraw.Draw(ring).ellipse([0,0,147,147],(255,255,255,255))
    shadow(bg,ring.getchannel('A'),(352,352),8,120); bg.alpha_composite(ring,(348,348)); bg.alpha_composite(orb,(356,356))
    bg.convert('RGB').save(out)

FONT='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf'
def default_base():
    bg=radial((58,64,70),(14,16,18),center=(0.5,0.35),scale=0.9).convert('RGBA')
    # glossy green sphere (original drawing)
    sph=radial((170,235,80),(30,110,20),center=(0.40,0.32),scale=0.62,size=340).convert('RGBA'); sph.putalpha(circle_mask(340,168,170,170))
    hl=Image.new('L',(340,340),0);ImageDraw.Draw(hl).ellipse([70,26,270,150],140);hl=hl.filter(ImageFilter.GaussianBlur(18))
    sph.paste((255,255,255),(0,0),ImageChops.multiply(hl,sph.getchannel('A')))
    shadow(bg,sph.getchannel('A'),(86,96),18,150); bg.alpha_composite(sph,(86,72)); return bg
def default_dashboard(out):
    bg=default_base(); d=ImageDraw.Draw(bg); f=ImageFont.truetype(FONT,150)
    d.text((256,248),'EX',font=f,fill=(255,255,255),anchor='mm',stroke_width=3,stroke_fill=(20,70,10))
    d.arc([60,52,452,444],200,340,fill=(255,255,255,200),width=10)
    bg.convert('RGB').save(out)
def default_avatars(out):
    bg=default_base(); d=ImageDraw.Draw(bg)
    sil=Image.new('L',(S,S),0); sd=ImageDraw.Draw(sil)
    sd.ellipse([200,112,312,224],255); sd.ellipse([140,236,372,470],255)
    sil=ImageChops.multiply(sil,circle_mask(S,160,256,242))
    bg.paste((255,255,255),(0,0),sil)
    bg.convert('RGB').save(out)
if __name__=='__main__':
    if sys.argv[1]=='--default':
        o=Path(sys.argv[2]); o.mkdir(parents=True,exist_ok=True)
        default_dashboard(o/'exbox360-default.png'); default_avatars(o/'avatars-default.png')
    else:
        loading=Image.open(sys.argv[1]); avatar=Image.open(sys.argv[2]); o=Path(sys.argv[3]); o.mkdir(parents=True,exist_ok=True)
        xbox_dashboard(loading,o/'exbox360-xbox.png'); xbox_avatars(loading,avatar,o/'avatars-xbox.png')
