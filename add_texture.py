from PIL import Image
textures = Image.open(input("贴图文件路径："))
texture = Image.open(input("要加入的文件路径"))
x = int(input("x:")) * 16
y = textures.height - int(input("y:")) * 16 - 16
textures.paste(texture, (x, y))
textures.save("./texture.png")
