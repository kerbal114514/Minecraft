images = [
    "grass_block_top.png",    # 0
    "grass_block_side.png",   # 1
    "dirt.png",               # 2
    "bedrock.png",            # 3
    "stone.png",              # 4
    "oak_log_top.png",        # 5
    "oak_log_side.png",       # 6
    "oak_leaves.png",         # 7
]
textures = {
    'block.minecraft.nature.grass_block': (0, 2, 1, 1, 1, 1),
    'block.minecraft.nature.dirt': (2, 2, 2, 2, 2, 2),
    'block.minecraft.nature.bedrock': (3, 3, 3, 3, 3, 3),
    'block.minecraft.nature.stone': (4, 4, 4, 4, 4, 4),
    'block.minecraft.wood.oak_log': (5, 5, 6, 6, 6, 6),
    'block.minecraft.leaves.oak_leaves': (7, 7, 7, 7, 7, 7),
}
def cube_vertices(x, y, z, n):
    """ Return the vertices of the cube at position x, y, z with size 2*n.

    """
    return [
        [x-n,y+n,z-n, x-n,y+n,z+n, x+n,y+n,z+n, x+n,y+n,z-n],  # top
        [x-n,y-n,z-n, x+n,y-n,z-n, x+n,y-n,z+n, x-n,y-n,z+n],  # bottom
        [x-n,y-n,z-n, x-n,y-n,z+n, x-n,y+n,z+n, x-n,y+n,z-n],  # left
        [x+n,y-n,z+n, x+n,y-n,z-n, x+n,y+n,z-n, x+n,y+n,z+n],  # right
        [x-n,y-n,z+n, x+n,y-n,z+n, x+n,y+n,z+n, x-n,y+n,z+n],  # front
        [x+n,y-n,z-n, x-n,y-n,z-n, x-n,y+n,z-n, x+n,y+n,z-n],  # back
    ]

import pyglet
from pyglet.gl import *
from pyglet.window import key, mouse
import ctypes, math
from PIL import Image

block_id = 'block.minecraft.leaves.oak_leaves'
texture_ids = []
vertices = cube_vertices(0, 0, 0, 0.5)

for i in range(len(vertices)):
    tmp = GLuint(0)
    glGenTextures(1, ctypes.byref(tmp))
    img = pyglet.image.load(images[textures[block_id][i]])
    glBindTexture(GL_TEXTURE_2D, tmp)
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.get_data('RGBA', img.width * 4))
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
    glBindTexture(GL_TEXTURE_2D, 0)
    texture_ids.append(tmp)

window = pyglet.window.Window(width=256, height=256)

glEnable(GL_TEXTURE_2D)

light = (0.8, 0.8, 0.6, 0.6, 1, 1)

@window.event
def on_draw():
    window.clear()
    width, height = window.get_size()
    glEnable(GL_DEPTH_TEST)
    viewport = window.get_viewport_size()
    glViewport(0, 0, max(1, viewport[0]), max(1, viewport[1]))
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    gluPerspective(0.112, width / float(height), 100, 1000)
    glMatrixMode(GL_MODELVIEW)
    glLoadIdentity()
    gluLookAt(500, 300, 500, 0, 0, 0, 0, 1, 0)
    #glActiveTexture(GL_TEXTURE0)
    for i in range(len(vertices)):
        glBindTexture(GL_TEXTURE_2D, texture_ids[i])
        glColor3f(light[i], light[i], light[i])
        pyglet.graphics.draw(len(vertices[i]) // 3, GL_QUADS,
            ('v3f', vertices[i]),
            ('t2f', (0, 0, 1, 0, 1, 1, 0, 1))
        )
    # 读取颜色缓冲区
    image_data = pyglet.image.ImageData(width, height, 'RGB', None)
    pyglet.image.get_buffer_manager().get_color_buffer().save('tmp.png')
    im = Image.open("tmp.png")
    im = im.convert('RGBA')
    for x in range(im.size[0]):
        for y in range(im.size[1]):
            p = im.getpixel((x, y))
            if (not any(p[:3])):
                p = (255, 255, 255, 0)
            im.putpixel((x, y), p)
    im.save('item.minecraft.block_item.leaves.oak_leaves.png')
pyglet.app.run()
