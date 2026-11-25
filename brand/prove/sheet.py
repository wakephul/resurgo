D="M19 92 L19 50 C19 30 39 22 55 22 C65 22 73 16 81 5 C78 24 68 38 53 38 C43 38 37 44 37 56 L37 92 Z"
INK="#141414"; RED="#C4311B"; DARK="#161616"; RED_D="#FF6B4F"
def mark(fill=INK,bg=None,two=None):
    b=f'<rect width="100" height="100" fill="{bg}"/>' if bg else ''
    if two:
        lo,hi=two
        body=(f'<defs><clipPath id="a"><rect x="0" y="0" width="100" height="44"/></clipPath><clipPath id="b"><rect x="0" y="44" width="100" height="56"/></clipPath></defs>'
              f'<path d="{D}" fill="{hi}" clip-path="url(#a)"/><path d="{D}" fill="{lo}" clip-path="url(#b)"/>')
    else: body=f'<path d="{D}" fill="{fill}"/>'
    return f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 100">{b}{body}</svg>'
V={"m-ink":mark(INK,"#FFFFFF"),"m-two":mark(bg="#FFFFFF",two=(INK,RED)),"m-red":mark(RED,"#FFFFFF"),
   "m-onred":mark("#FFFFFF",RED),"m-dark":mark("#EDEDED",DARK),"m-dark-two":mark(bg=DARK,two=("#EDEDED",RED_D))}
for k,s in V.items(): open(k+".svg","w").write(s)
