import pyglet
from PygletButton import Button

class Window(pyglet.window.Window):
    def __init__(self, *args, **kw):
        super().__init__(*args, **kw)
        self.mouse_position = (0, 0)
        self.button = Button(500, 200, 100, 30, '文字', (50, 50, 50, 255), (100, 100, 200, 255), (255, 255, 255, 255))

    def on_draw(self):
        self.clear()
        self.button.draw(self.mouse_position)

    def on_mouse_motion(self, x, y, dx, dy):
        self.mouse_position = (x, y)

window = Window(resizable=True)
pyglet.app.run()
