from PIL import Image
from tkinter.filedialog import askopenfilename as f
name = f()
img = Image.open(name)
img = img.convert("RGBA")
for l in range(1, 5):
    aa = img.size
    ans = Image.new('RGBA', (aa[0] // 2, aa[1] // 2))
    for i in range(0, aa[0], 2):
        for j in range(0, aa[1], 2):
            pixels = [img.getpixel((i+dx, j+dy)) for dx, dy in [(0,0), (0,1), (1,0), (1,1)]]
            r = int(sum((p[0] * p[3] / 255)**2 for p in pixels) / 4)**0.5
            g = int(sum((p[1] * p[3] / 255)**2 for p in pixels) / 4)**0.5
            b = int(sum((p[2] * p[3] / 255)**2 for p in pixels) / 4)**0.5
            avg_a = sum(p[3] for p in pixels) / 4
            final_a = 255 if avg_a > 64 else 0
            ans.putpixel((i // 2, j // 2), (int(r), int(g), int(b), final_a))
    ans.save(name[:-4] + f'_mipmap_{l}.png')
    img = ans
