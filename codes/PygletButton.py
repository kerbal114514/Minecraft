import pyglet
from pyglet.window import mouse

class Button:
    def __init__(self, x, y, width, height, text, bg_color, intersect_color, fg_color, callback=None):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.callback = callback

        # 背景
        self.n = self.width // 2
        self.m = self.height // 2
        self.bg_color = bg_color
        self.intersect_color = intersect_color
        self.bg = pyglet.graphics.vertex_list(4,
            ('v2i', (x + self.n, y + self.m, x + self.n, y - self.m, x - self.n, y - self.m, x - self.n, y + self.m)),
            ('c4B', bg_color * 4)
        )
        self.intersect_bg = pyglet.graphics.vertex_list(4,
            ('v2i', (x + self.n, y + self.m, x + self.n, y - self.m, x - self.n, y - self.m, x - self.n, y + self.m)),
            ('c4B', intersect_color * 4)
        )

        # 文字
        self.label = pyglet.text.Label(
            text, font_name='微软雅黑', font_size=12,
            x=x, y=y,
            anchor_x='center', anchor_y='center',
            color=fg_color
        )

    def draw(self, mouse_position):
        if (self.is_hit(*mouse_position)):
            self.intersect_bg.draw(pyglet.gl.GL_QUADS)
        else:
            self.bg.draw(pyglet.gl.GL_QUADS)
        self.label.draw()

    def replace(self, x, y):
        self.x = x
        self.y = y
        self.label.x = x
        self.label.y = y
        self.bg = pyglet.graphics.vertex_list(4,
            ('v2i', (x + self.n, y + self.m, x + self.n, y - self.m, x - self.n, y - self.m, x - self.n, y + self.m)),
            ('c4B', self.bg_color * 4)
        )
        self.intersect_bg = pyglet.graphics.vertex_list(4,
            ('v2i', (x + self.n, y + self.m, x + self.n, y - self.m, x - self.n, y - self.m, x - self.n, y + self.m)),
            ('c4B', self.intersect_color * 4)
        )

    def is_hit(self, mx, my):
        """检查鼠标坐标 (mx, my) 是否在按钮范围内"""
        return self.x - self.n <= mx <= self.x + self.n and self.y - self.m <= my <= self.y + self.m

    def on_mouse_press(self, x, y, button, modifiers):
        if button == mouse.LEFT and self.is_hit(x, y):
            if self.callback:
                self.callback()
