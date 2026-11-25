import sys
def svg(rows, color="#141414", px=1, pad=1):
    h=len(rows); w=len(rows[0])
    rects=[]
    for y,r in enumerate(rows):
        x=0
        while x<w:
            if r[x]=='X':
                s=x
                while x<w and r[x]=='X': x+=1
                rects.append(f'<rect x="{s+pad}" y="{y+pad}" width="{x-s}" height="1"/>')
            else: x+=1
    W=w+2*pad; H=h+2*pad
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" shape-rendering="crispEdges"><rect width="{W}" height="{H}" fill="#fff"/><g fill="{color}">{"".join(rects)}</g></svg>'
variants = {
"a": [
"X......X......X",
"XX....XXX....XX",
"XXX...XXX...XXX",
".XXX...X...XXX.",
".XXXX.XXX.XXXX.",
"..XXXXXXXXXXX..",
"...XXXXXXXXX...",
"....XXXXXXX....",
".....XXXXX.....",
"......XXX......",
".....X.X.X.....",
"....X..X..X....",
"..X.X.XXX.X.X..",
".XXXXXXXXXXXXX.",
],
"b": [
".......X.......",
"X.....XXX.....X",
"XX....XXX....XX",
"XXX....X....XXX",
"XXXX..XXX..XXXX",
".XXXXXXXXXXXXX.",
"..XXXXXXXXXXX..",
"....XXXXXXX....",
"......XXX......",
"......XXX......",
"....X.XXX.X....",
"...XX..X..XX...",
"..XXX.XXX.XXX..",
"...............",
"XXXXXXXXXXXXXXX",
],
"c": [
"......XX.......",
".....XXX.......",
"X.....XX......X",
"XX....XX.....XX",
"XXX..XXXX...XXX",
".XXXXXXXXXXXXX.",
"..XXXXXXXXXXX..",
"....XXXXXXX....",
".....XXXXX.....",
"......XXX......",
".....XX.XX.....",
"....XX...XX....",
"...X.X.X.X.X...",
"...............",
"XXXXXXXXXXXXXXX",
],
}
for k,v in variants.items():
    open(f"{k}.svg","w").write(svg(v))
