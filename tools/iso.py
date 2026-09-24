import struct, os, sys
BIN=r"C:\Users\JimHa\Downloads\skipper-1\SKIPPER_1.BIN"
OUT=sys.argv[1]
f=open(BIN,'rb')
START=99406
def sec(lba):
    f.seek(lba*2352); s=f.read(2352)
    return s[24:24+2048]
# find PVD
for off in range(0,40):
    d=sec(START+off)
    if d[1:6]==b'CD001': print('VD at',START+off,d[0]); 
pvd=sec(START+16)
assert pvd[1:6]==b'CD001', pvd[:16]
root=pvd[156:156+34]
def walk(lba,size,path):
    data=b''.join(sec(lba+i) for i in range((size+2047)//2048))
    i=0
    while i<len(data):
        l=data[i]
        if l==0:
            i=((i//2048)+1)*2048; continue
        rec=data[i:i+l]
        elba=struct.unpack('<I',rec[2:6])[0]; esz=struct.unpack('<I',rec[10:14])[0]
        flags=rec[25]; nl=rec[32]; name=rec[33:33+nl]
        i+=l
        if name in (b'\x00',b'\x01'): continue
        n=name.decode('latin1').split(';')[0]
        p=os.path.join(path,n)
        if flags&2: os.makedirs(p,exist_ok=True); walk(elba,esz,p)
        else:
            print(f"{esz:>12}  {p[len(OUT):]}")
            with open(p,'wb') as o:
                left=esz; k=0
                while left>0:
                    o.write(sec(elba+k)[:min(2048,left)]); left-=2048; k+=1
rl=struct.unpack('<I',root[2:6])[0]; rs=struct.unpack('<I',root[10:14])[0]
os.makedirs(OUT,exist_ok=True)
walk(rl,rs,OUT)
