exec(open("mark.py").read().split("variants =")[0])
L = [
"X.........",
"XX.X......",
"XXXXX.X...",
".XXXXXXX..",
".XXXXXXXX.",
"..XXXXXXXX",
"...XXXXXXX",
"....XXXXXX",
"......XXXX",
".......XXX",
"........XX",
".........X",
"..........",
]
C = ".XXXXXXXXXXXX"
grid=[]
for r,(l,c) in enumerate(zip(L,C)):
    row=list(l+c+l[::-1]); grid.append(row)
head={(1,9),(1,10),(1,11),(2,9),(2,10),(2,11),(2,12),(3,10),(3,11)}
for (r,c) in head: grid[r][c]='X'
grid[1][11]='X'
flames = [
"..........X..........",
"....X.....X.....X....",
"....X....XXX....X....",
"...XXX...XXX...XXX...",
"...XXX..XXXXX..XXX...",
"..XXXXX.XXXXX.XXXXX..",
"..XXXXXXXXXXXXXXXXX..",
]
rows=["".join(r) for r in grid]+["."*21]+flames
eye=[list(r) for r in rows]; eye[2][10]='.'
open("i.svg","w").write(svg(rows)); open("j.svg","w").write(svg(["".join(r) for r in eye]))
open("k.svg","w").write(svg(["".join(r) for r in grid]))
print("\n".join(rows))
