V = {
 "r1": "M22 90 L22 46 C22 28 48 18 84 8 C62 24 40 36 40 60 L40 90 Z",
 "r2": "M24 90 L24 44 C24 26 50 16 80 6 C70 22 42 30 42 56 L42 90 Z",
 "r3": "M22 90 L22 50 C22 30 42 22 58 22 C68 22 76 16 84 6 C80 24 70 38 56 38 C46 38 40 44 40 56 L40 90 Z",
 "r4": "M22 90 L22 40 L40 40 L40 48 C46 36 60 22 86 10 C74 30 56 46 40 62 L40 90 Z",
}
for k,d in V.items():
    open(f"{k}.svg","w").write(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100"><rect width="100" height="100" fill="#fff"/><path d="{d}" fill="#141414"/></svg>')
