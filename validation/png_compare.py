"""Exact RGB/RGBA PNG comparison using only stdlib; no tolerance or image edits."""
import argparse,json,pathlib,struct,zlib
def decode(path):
    data=pathlib.Path(path).read_bytes();assert data[:8]==b'\x89PNG\r\n\x1a\n'
    at=8;compressed=bytearray();width=height=bpp=None
    while at<len(data):
        size=struct.unpack_from('>I',data,at)[0];tag=data[at+4:at+8];chunk=data[at+8:at+8+size]
        assert zlib.crc32(tag+chunk)&0xffffffff==struct.unpack_from('>I',data,at+8+size)[0]
        if tag==b'IHDR':
            width,height,bits,color,compression,filtering,interlace=struct.unpack('>IIBBBBB',chunk)
            assert bits==8 and color in (2,6) and compression==filtering==interlace==0
            bpp=3 if color==2 else 4
        if tag==b'IDAT':compressed.extend(chunk)
        at+=size+12
    raw=zlib.decompress(compressed);stride=width*bpp;assert len(raw)==height*(stride+1)
    pixels=bytearray();previous=bytearray(stride);at=0
    for y in range(height):
        kind=raw[at];line=bytearray(raw[at+1:at+1+stride]);at+=stride+1
        for x in range(stride):
            left=line[x-bpp] if x>=bpp else 0;up=previous[x];corner=previous[x-bpp] if x>=bpp else 0
            if kind==1:predictor=left
            elif kind==2:predictor=up
            elif kind==3:predictor=(left+up)//2
            elif kind==4:
                p=left+up-corner;a=abs(p-left);b=abs(p-up);c=abs(p-corner)
                predictor=left if a<=b and a<=c else up if b<=c else corner
            else:assert kind==0;predictor=0
            line[x]=(line[x]+predictor)&255
        pixels.extend(line);previous=line
    return width,height,bpp,pixels
def compare(first,second):
    w,h,bpp,a=decode(first);w2,h2,bpp2,b=decode(second);assert (w,h,bpp)==(w2,h2,bpp2)
    count=0;bounds=[w,h,-1,-1];maximum=[0]*bpp;examples=[]
    for i in range(0,len(a),bpp):
        if a[i:i+bpp]==b[i:i+bpp]:continue
        count+=1;x=(i//bpp)%w;y=(i//bpp)//w
        bounds=[min(bounds[0],x),min(bounds[1],y),max(bounds[2],x),max(bounds[3],y)]
        maximum=[max(v,abs(a[i+c]-b[i+c])) for c,v in enumerate(maximum)]
        if len(examples)<8:examples.append(dict(x=x,y=y,first=list(a[i:i+bpp]),second=list(b[i:i+bpp])))
    return dict(width=w,height=h,channels=bpp,differing_pixels=count,bounds_inclusive=bounds if count else None,max_channel_difference=maximum,examples=examples)
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('first');p.add_argument('second');p.add_argument('--output');args=p.parse_args()
    result=compare(args.first,args.second);text=json.dumps(result,indent=2);print(text)
    if args.output:pathlib.Path(args.output).write_text(text)
