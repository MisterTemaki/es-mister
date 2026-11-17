import sys,os
from PIL import Image
import numpy as np
a_dir,b_dir=sys.argv[1],sys.argv[2]
# the clock (bottom right of the Carbon theme) changes with the minute: left out
mask_box=(1180,640,1280,720)
for f in sorted(os.listdir(a_dir)):  # every PNG of the first folder against the second
    if not f.endswith('.png') or not os.path.exists(os.path.join(b_dir,f)): continue
    A=np.array(Image.open(os.path.join(a_dir,f)).convert('RGBA')).astype(int)
    B=np.array(Image.open(os.path.join(b_dir,f)).convert('RGBA')).astype(int)
    if A.shape!=B.shape: print(f,'SIZE',A.shape,B.shape); continue
    D=np.abs(A-B).sum(axis=2)
    x0,y0,x1,y1=mask_box
    clock=int((D[y0:y1,x0:x1]>0).sum())
    D[y0:y1,x0:x1]=0
    n=int((D>0).sum())
    if n==0: print(f'{f}: identical (outside the clock; {clock} clock pixels differ)')
    else:
        ys,xs=np.nonzero(D); print(f'{f}: {n} pixels differ, box x {xs.min()}-{xs.max()} y {ys.min()}-{ys.max()}')
