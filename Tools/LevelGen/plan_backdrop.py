import json, math, random
random.seed(21)
d=json.load(open('plan.json')); P=[tuple(p) for p in d['P']]; H=json.load(open('H.json'))
n=len(P)
def rect(cx,cy,h,W,D,m=0):
    hr=math.radians(h); pts=[]
    for lx,ly in [(-W*100-m,-D*100-m),(W*100+m,-D*100-m),(W*100+m,D*100+m),(-W*100-m,D*100+m)]:
        pts.append((cx+lx*math.cos(hr)-ly*math.sin(hr),cy+lx*math.sin(hr)+ly*math.cos(hr)))
    return pts
def inside(poly,px,py):
    c=False
    for i in range(len(poly)):
        (x1,y1),(x2,y2)=poly[i],poly[(i+1)%len(poly)]
        if (y1>py)!=(y2>py) and px<(x2-x1)*(py-y1)/(y2-y1)+x1: c=not c
    return c
def segd(px,py,a,b):
    dx,dy=b[0]-a[0],b[1]-a[1]; L2=dx*dx+dy*dy
    t=max(0,min(1,((px-a[0])*dx+(py-a[1])*dy)/L2)); return math.hypot(px-a[0]-t*dx,py-a[1]-t*dy)
def samples(r):
    pts=list(r)
    for i in range(4):
        a,b=r[i],r[(i+1)%4]
        for k in range(1,6): pts.append((a[0]+(b[0]-a[0])*k/6,a[1]+(b[1]-a[1])*k/6))
    cx=sum(p[0] for p in r)/4; cy=sum(p[1] for p in r)/4; pts.append((cx,cy))
    return pts
def overlaps(r1,r2):
    return any(inside(r2,*p) for p in samples(r1)) or any(inside(r1,*p) for p in samples(r2))
front=[rect(*h[:5]) for h in H]
extra=[rect(2300,-100,270,2,2),rect(-1900,2700,90,2,2)]
walk=P
out=[]
ROOF={(2,2),(2,3),(2,4),(3,2),(3,3),(3,4),(3,5),(3,6),(3,7),(4,4),(4,5),(4,6),(4,7)}
cands=[]
# grid of candidate backdrop blocks around the town, facing the nearest outline point
for gx in range(-3200,3400,350):
    for gy in range(-5200,5200,350):
        dmin=min(segd(gx,gy,P[i],P[(i+1)%n]) for i in range(n))
        if inside(walk,gx,gy) or dmin<750 or dmin>2300: continue
        cands.append((dmin+random.random()*200,gx,gy))
random.shuffle(cands)
cands.sort()
placed=[]
for dmin,gx,gy in cands:
    # face toward nearest outline point
    best=None
    for i in range(n):
        a,b=P[i],P[(i+1)%n]; dx,dy=b[0]-a[0],b[1]-a[1]; L2=dx*dx+dy*dy
        t=max(0,min(1,((gx-a[0])*dx+(gy-a[1])*dy)/L2)); qx,qy=a[0]+t*dx,a[1]+t*dy
        dd=math.hypot(gx-qx,gy-qy)
        if best is None or dd<best[0]: best=(dd,qx,qy)
    _,qx,qy=best
    fd=math.atan2(qy-gy,qx-gx)  # direction to town
    h=math.degrees(fd)+90       # front (sin h,-cos h) == (cos fd, sin fd)
    h=round(h/15)*15+random.choice([-4,0,4])
    W=random.choice([2,3,3,4]); D=random.choice([dd for dd in range(max(2,W-1),W+3) if (W,dd) in ROOF])
    fl=random.choice([3,3,4,4])
    r=rect(gx,gy,h,W,D,40)
    if any(inside(walk,*p) or min(segd(p[0],p[1],P[i],P[(i+1)%n]) for i in range(n))<150 for p in samples(r)): continue
    if any(overlaps(r,f) for f in front+extra): continue
    if any(overlaps(r,f) for f in placed): continue
    placed.append(r); out.append([gx,gy,h,W,D,fl,random.randint(1,999)])
print(len(out),'backdrop houses')
json.dump(out,open('B.json','w'))
# svg
minx,maxx,miny,maxy=-5500,5500,-5500,5500; S=0.09
def tx(x,y): return ((x-minx)*S,(maxy-y)*S)
o=['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d"><rect width="100%%" height="100%%" fill="#222"/>'%((maxx-minx)*S,(maxy-miny)*S)]
o.append('<polygon points="%s" fill="#886"/>'%' '.join('%.1f,%.1f'%tx(*p) for p in P))
for r in front+extra: o.append('<polygon points="%s" fill="#5a5" stroke="#fff"/>'%' '.join('%.1f,%.1f'%tx(*p) for p in r))
for r in placed: o.append('<polygon points="%s" fill="#55a" stroke="#fff"/>'%' '.join('%.1f,%.1f'%tx(*p) for p in r))
o.append('</svg>'); open('back.svg','w').write('\n'.join(o))
