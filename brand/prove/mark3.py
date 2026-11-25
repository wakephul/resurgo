import math
exec(open("mark.py").read().split("variants =")[0])
def canvas(w,h): return [[0]*w for _ in range(h)]
def dot(c,x,y,r=0.5):
    for yy in range(len(c)):
        for xx in range(len(c[0])):
            if (xx-x)**2+(yy-y)**2<=r*r: c[yy][xx]=1
def line(c,x0,y0,x1,y1,r):
    n=int(max(abs(x1-x0),abs(y1-y0))*4)+1
    for i in range(n+1):
        t=i/n; dot(c,x0+(x1-x0)*t,y0+(y1-y0)*t,r)
def poly(c,pts):
    for yy in range(len(c)):
        for xx in range(len(c[0])):
            px,py=xx,yy; inside=False; j=len(pts)-1
            for i in range(len(pts)):
                xi,yi=pts[i]; xj,yj=pts[j]
                if (yi>py)!=(yj>py) and px<(xj-xi)*(py-yi)/(yj-yi)+xi: inside=not inside
                j=i
            if inside: c[yy][xx]=1
def rows(c): return ["".join("X" if v else "." for v in r) for r in c]
def phoenix(W=23, flames=True, gap=True):
    c=canvas(W,W); m=W//2
    # ali: tre penne per lato, a ventaglio verso l'alto
    for side in (-1,1):
        bx=m+side*1.5; by=10.5
        for tx,ty,r in ((m+side*10.5,1.0,1.0),(m+side*8.0,0.0,1.0),(m+side*5.0,2.5,0.9)):
            line(c,bx,by,tx,ty,r)
        poly(c,[(bx,by+1.5),(m+side*10.5,1.5),(m+side*10.5,4.5),(m+side*4,11.5)])
    # corpo
    for yy in range(len(c)):
        for xx in range(W):
            if ((xx-m)/3.2)**2+((yy-11.5)/3.0)**2<=1: c[yy][xx]=1
    # collo e testa di profilo verso destra
    line(c,m,9,m+0.5,4.5,1.0)
    dot(c,m+1,3.6,1.6); line(c,m+2,3.2,m+3.6,3.6,0.6)
    # occhio
    c[3][m+1]=0
    if flames:
        # tre lingue di fuoco sotto
        top=16 if gap else 15
        for fx,ty in ((m-4,top+2),(m,top),(m+4,top+2)):
            poly(c,[(fx-2.2,21.6),(fx,ty-0.5),(fx+2.2,21.6)])
        for x in range(m-7,m+8): c[21][x]=1
        if gap:
            for x in range(W): c[15][x]=0
    return rows(c)
for k,(W,fl) in {"g":(23,True),"h":(23,False)}.items():
    open(f"{k}.svg","w").write(svg(phoenix(W,fl)))
    print(k); print("\n".join(phoenix(W,fl)))
