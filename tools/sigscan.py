import sys, struct, re
G = r"C:\Program Files (x86)\Steam\steamapps\common\Counter-Strike Global Offensive\game"
MODS = {"client.dll": G + r"\csgo\bin\win64\client.dll", "rendersystemdx11.dll": G + r"\bin\win64\rendersystemdx11.dll", "engine2.dll": G + r"\bin\win64\engine2.dll"}

def load(path):
    d = open(path, "rb").read()
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    opt = struct.unpack_from("<H", d, pe + 20)[0]
    size = struct.unpack_from("<I", d, pe + 24 + 56)[0]
    img = bytearray(size)
    img[:0x1000] = d[:0x1000]
    so = pe + 24 + opt
    for i in range(nsec):
        name, vs, va, rs, ro = struct.unpack_from("<8sIIII", d, so + i * 40)
        img[va:va + rs] = d[ro:ro + rs]
    return bytes(img)

def compile_sig(s):
    s = s.replace(" ", "")
    star = s.find("*")
    s = s.replace("*", "")
    toks = [s[i:i+2] for i in range(0, len(s), 2)] if "?" not in s or "??" in s else None
    toks = []
    i = 0
    while i < len(s):
        if s[i] == "?":
            toks.append(None); i += 2 if i + 1 < len(s) and s[i+1] == "?" else 1
        else:
            toks.append(int(s[i:i+2], 16)); i += 2
    rx = b"".join(b"." if t is None else re.escape(bytes([t])) for t in toks)
    return re.compile(rx, re.S), (star // 2 if star >= 0 else -1)

def scan(img, sig):
    rx, star = compile_sig(sig)
    hits = [m.start() for m in rx.finditer(img)]
    return hits, star

cache = {}
for line in sys.argv[1:]:
    mod, sig = line.split(":", 1)
    img = cache.setdefault(mod, load(MODS[mod]))
    hits, star = scan(img, sig)
    out = f"{mod}:{sig} -> {len(hits)} hits"
    if len(hits) >= 1 and star >= 0:
        rvas = []
        for h in hits[:3]:
            p = h + star
            rel = struct.unpack_from("<i", img, p)[0]
            rvas.append(hex(p + 4 + rel))
        out += " rva=" + ",".join(rvas)
    elif hits:
        out += " at " + ",".join(hex(h) for h in hits[:3])
    print(out)
