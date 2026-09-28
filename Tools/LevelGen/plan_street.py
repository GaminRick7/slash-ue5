import math, json, random
random.seed(11)
# Walkable outline, CCW (interior on the left of each edge). cm, X east, Y north.
P=[(-700,-3600),(700,-3600),(700,-2037),(1145,-1000),(1700,-1000),
   (1700,-300),(2100,-300),(2100,100),(1700,100),           # east market nook
   (1700,700),(789,700),(189,2100),(700,2100),(700,3500),(-1300,3500),
   (-1300,2900),(-1700,2900),(-1700,2500),(-1300,2500),      # west yard nook
   (-1300,2100),(-790,2100),(-190,700),(-300,700),(-300,-1000),(56,-1000),(-700,-2763)]
# per-edge: (start_inset, end_inset, allow houses)
ROWS={}
def edges():
    for i in range(len(P)):
        yield i,P[i],P[(i+1)%len(P)]
def ang(p,q): return math.degrees(math.atan2(q[1]-p[1],q[0]-p[0]))
ROOF={(2,2),(2,3),(2,4),(3,2),(3,3),(3,4),(3,5),(3,6),(3,7),(4,4),(4,5),(4,6),(4,7)}
def cross(o,a,b): return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
n=len(P)
reflex=[cross(P[i-1],P[i],P[(i+1)%n])<0 for i in range(n)]
houses=[]
def comp(cells):
    for _ in range(200):
        seq=[];c=cells
        while c>=2:
            opts=[w for w in (2,3,4) if w<=c and c-w!=1]
            if not opts: break
            w=random.choice(opts); seq.append(w); c-=w
        if c==0: return seq
    return seq
SPECIAL={13:[(None,),('TOWER',)]}
for i,p,q in edges():
    L=math.dist(p,q); a=ang(p,q); d=((q[0]-p[0])/L,(q[1]-p[1])/L)
    if i in (5,6,7,15,16,17): continue
    cells=max(2,int(L//200))
    if i==13:
        seq=[3,'T',4]
    else:
        seq=comp(cells)
    used=sum(3 if w=='T' else w for w in seq)
    gap=(L-used*200)/max(1,len(seq)+1)
    t=gap*0.5 if i!=13 else gap
    for w in seq:
        W=3 if w=='T' else w
        if w=='T': D=3; fl=5
        else:
            D=random.choice([dd for dd in range(max(2,W-1),W+3) if (W,dd) in ROOF])
            fl=random.choice([2,2,3,3,3])
        mid=t+W*100
        fx,fy=p[0]+d[0]*mid,p[1]+d[1]*mid
        h=a+180
        ar=math.radians(a)
        cx,cy=fx+D*100*math.sin(ar),fy-D*100*math.cos(ar)
        houses.append([round(cx),round(cy),round(h,1),W,D,fl,random.randint(1,999),i,1 if w=='T' else 0])
        t+=W*200+gap
# tower: north edge of T square (edge 13: (700,3500)->(-1300,3500))
print(len(houses),'houses')
def est(hh):
    W,D,fl=hh[3],hh[4],hh[5]
    return (2*(W+D))*fl*1.8+4*fl+6
print('est actors',sum(est(h) for h in houses))
json.dump({'P':P,'houses':houses},open('plan.json','w'))
# svg
minx,maxx,miny,maxy=-4500,4500,-4500,4500
S=0.1
def tx(x,y): return ((x-minx)*S,(maxy-y)*S)
out=['<svg xmlns="http://www.w3.org/2000/svg" width="%d" height="%d"><rect width="100%%" height="100%%" fill="#222"/>'%((maxx-minx)*S,(maxy-miny)*S)]
out.append('<polygon points="%s" fill="#886" stroke="#ff0"/>'%' '.join('%.1f,%.1f'%tx(*p) for p in P))
for hh in houses:
    cx,cy,h,W,D=hh[:5]
    hr=math.radians(h)
    pts=[]
    for lx,ly in [(-W*100,-D*100),(W*100,-D*100),(W*100,D*100),(-W*100,D*100)]:
        pts.append(tx(cx+lx*math.cos(hr)-ly*math.sin(hr),cy+lx*math.sin(hr)+ly*math.cos(hr)))
    col={2:'#a55',3:'#5a5',4:'#55a'}[hh[5]] if hh[5] in (2,3,4) else '#999'
    out.append('<polygon points="%s" fill="%s" fill-opacity="0.7" stroke="#fff"/>'%(' '.join('%.1f,%.1f'%p for p in pts),col))
    # front marker
    fx,fy=cx+D*100*math.sin(hr),cy-D*100*math.cos(hr)
    out.append('<circle cx="%.1f" cy="%.1f" r="3" fill="#fff"/>'%tx(fx,fy))
out.append('</svg>')
open('plan.svg','w').write('\n'.join(out))
