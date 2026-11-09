"""Create a printable tag and a native detector test image from the vendored family.

No Python dependencies. Code bit positions come from the BSD-licensed AprilTag
family source; see lib/AprilTagESP32/LICENSE.md. Print the SVG at actual size.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]

def tag_cells(tag_id=0):
    source = (ROOT / "lib/AprilTagESP32/src/tag36h11.c").read_text()
    codes = re.findall(r"0x([0-9a-fA-F]+)UL", source)
    if not 0 <= tag_id < len(codes):
        raise ValueError("Tag ID outside the bundled family")
    code = int(codes[tag_id], 16)
    xs = {int(i): int(v) for i, v in re.findall(r"tf->bit_x\[(\d+)\] = (\d+);", source)}
    ys = {int(i): int(v) for i, v in re.findall(r"tf->bit_y\[(\d+)\] = (\d+);", source)}
    cells = [[255 if x in (0, 9) or y in (0, 9) else 0 for x in range(10)] for y in range(10)]
    for i in range(36):
        if code & (1 << (35 - i)):
            cells[ys[i] + 1][xs[i] + 1] = 255
    return cells

def generate(tag_id):
    cells = tag_cells(tag_id)
    (ROOT / "docs").mkdir(exist_ok=True)
    blocks = [f'<rect x="{x}" y="{y}" width="1" height="1"/>'
              for y in range(10) for x in range(10) if cells[y][x] == 0]
    svg = ('<svg xmlns="http://www.w3.org/2000/svg" width="125mm" height="125mm" viewBox="0 0 10 10" shape-rendering="crispEdges">\n'
           f'<title>AprilTag tag36h11 ID {tag_id}: outer black square 100 mm</title>\n'
           '<rect width="10" height="10" fill="white"/>\n<g fill="black">\n'
           + "\n".join(blocks) + '\n</g>\n</svg>\n')
    (ROOT / f"docs/tag36h11_id{tag_id}_100mm.svg").write_text(svg, encoding="utf-8")
    # Centered QVGA synthetic image: 80px black square, 10px per cell.
    frame = bytearray([255] * (320 * 240))
    for y in range(100):
        for x in range(100):
            frame[(70+y)*320+110+x] = cells[y//10][x//10]
    (ROOT / ".pio").mkdir(exist_ok=True)
    (ROOT / f".pio/test-tag-{tag_id}.pgm").write_bytes(b"P5\n320 240\n255\n" + frame)
    return svg

def main():
    pages = []
    for tag_id in range(4):
        svg = generate(tag_id)
        pages.append(f'<section><h1>Destination {tag_id+1} / Tag {tag_id}</h1><p>tag36h11 / 100 mm black square</p>{svg}</section>')
    html = ('<!doctype html><html lang="en"><meta charset="utf-8"><title>Robot destinations 0-3</title>'
            '<style>@page{size:A4;margin:20mm}body{font-family:Arial,sans-serif;text-align:center}'
            'section{break-after:page;padding-top:10mm}section:last-child{break-after:auto}'
            'svg{width:125mm;height:125mm;margin-top:10mm}h1{font-size:24px}'
            '@media screen{section{margin:30px auto;border-bottom:1px solid #ccc;max-width:700px}}</style>'
            '<body>'+''.join(pages)+'</body></html>')
    (ROOT / 'docs/PRINT_TAGS.html').write_text(html, encoding='utf-8')
    print('Created four printable tags (IDs 0-3), print booklet and detector fixtures')

if __name__ == "__main__":
    main()
