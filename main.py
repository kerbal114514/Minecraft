import math
import time
import random
import ctypes
import os
import sys
import importlib

import numpy as np
import pyglet
from pyglet.gl import *
from pyglet.window import key, mouse

from MCworld import World

sys.path.append('./codes/')
Button = getattr(importlib.import_module('PygletButton'), 'Button')

block_id = [
    "block.minecraft.dev.sector_not_loaded",
    "block.minecraft.dev.air",
    "block.minecraft.nature.grass_block",
    "block.minecraft.nature.dirt",
    "block.minecraft.nature.stone",
    "block.minecraft.nature.bedrock",
    "block.minecraft.wood.oak_log",
    "block.minecraft.wood.oak_leaves",
]

gamerule = {
    'tick_per_second': 20,
    'random_tick_speed': 3,
    'walk_speed': 4.3,
    'sneek_speed': 1.52,
    'run_speed': 5.6,
    'run_jump_speed': 7.5,
    'fly_speed': 11,
    'fly_run_speed': 22,
    'gravity': 32,
    'jump_speed': 8.85,
    'max_speed': 64.0,
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

simulate_distance = 8
simulate_sectors = set()
for x in range(-simulate_distance, simulate_distance + 1):
    for y in range(-simulate_distance, simulate_distance + 1):
        if x ** 2 + y ** 2 <= simulate_distance ** 2:
            simulate_sectors.add((x, y))

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
    'block.minecraft.wood.oak_leaves': (7, 7, 7, 7, 7, 7),
}

def create_texture_array(image_list, path='./Textures'):
    width, height = 16, 16
    layers = len(image_list)

    tex_id = GLuint()
    glGenTextures(1, ctypes.byref(tex_id))
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_id)

    # 预分配空间
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA8, width, height, layers, 0, GL_RGBA, GL_UNSIGNED_BYTE, None)

    for i, img_name in enumerate(image_list):
        img_path = os.path.join(path, img_name)
        img = pyglet.image.load(img_path)
        data = img.get_data('RGBA', img.width * 4)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, data)

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR)
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY)

    return tex_id

def replace_mipmap(tex_id, layer, level, img_name):
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_id)
    img_path = os.path.join("./Textures", img_name)
    img = pyglet.image.load(img_path)
    data = img.get_data('RGBA', img.width * 4)
    glTexSubImage3D(
        GL_TEXTURE_2D_ARRAY,
        level,
        0, 0, layer,
        16 // (2 ** level), 16 // (2 ** level), 1,
        GL_RGBA, GL_UNSIGNED_BYTE, data
    )

tex_array_id = create_texture_array(images)
replace_mipmap(tex_array_id, 7, 1, "oak_leaves_mipmap_1.png")
replace_mipmap(tex_array_id, 7, 2, "oak_leaves_mipmap_2.png")
replace_mipmap(tex_array_id, 7, 3, "oak_leaves_mipmap_3.png")
replace_mipmap(tex_array_id, 7, 4, "oak_leaves_mipmap_4.png")

def get_tex_array_data(block_name, face_index):
    img_idx = textures[block_name][face_index]
    uvs = [0,0, 1,0, 1,1, 0,1]
    res = []
    for i in range(0, 8, 2):
        res.extend([uvs[i], uvs[i+1], float(img_idx)])
    return res


class Shader:
    def __init__(self, vert_code, frag_code):
        self.program = glCreateProgram()

        # 编译顶点着色器
        self.vs = self.compile_shader(vert_code, GL_VERTEX_SHADER)
        # 编译片段着色器
        self.fs = self.compile_shader(frag_code, GL_FRAGMENT_SHADER)

        glAttachShader(self.program, self.vs)
        glAttachShader(self.program, self.fs)
        glLinkProgram(self.program)

        status = GLint()
        glGetProgramiv(self.program, GL_LINK_STATUS, ctypes.byref(status))
        if not status.value:
            # 如果链接失败，获取错误日志
            log_length = GLint()
            glGetProgramiv(self.program, GL_INFO_LOG_LENGTH, ctypes.byref(log_length))
            log = ctypes.create_string_buffer(log_length.value)
            glGetProgramInfoLog(self.program, log_length, None, log)
            print("Shader Link Error:")
            print(log.value.decode())
            raise RuntimeError("Shader linking failed.")

    def compile_shader(self, code, shader_type):
        shader = glCreateShader(shader_type)
        # 转换字符串为底层 C 指针
        src = ctypes.create_string_buffer(code.encode('ascii'))
        ptr = ctypes.cast(ctypes.pointer(src), ctypes.POINTER(ctypes.c_char))
        glShaderSource(shader, 1, ctypes.byref(ptr), None)
        glCompileShader(shader)
        status = GLint()
        glGetShaderiv(shader, GL_COMPILE_STATUS, ctypes.byref(status))
        if not status.value:
            log_length = GLint()
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, ctypes.byref(log_length))
            log = ctypes.create_string_buffer(log_length.value)
            glGetShaderInfoLog(shader, log_length, None, log)
            shader_name = "Vertex" if shader_type == GL_VERTEX_SHADER else "Fragment"
            print(f"{shader_name} Shader Compile Error:\n{log.value.decode()}")
            raise RuntimeError(f"{shader_name} compilation failed")
        return shader

    def bind(self):
        glUseProgram(self.program)


    def unbind(self):
        glUseProgram(0)

class BlockShader(Shader):
    def __init__(self, vert_code, frag_code):
        super().__init__(vert_code, frag_code)
        self.u_render_distance_loc = glGetUniformLocation(self.program, b"u_render_distance")
        self.u_position_loc = glGetUniformLocation(self.program, b"u_position")

    def bind(self, u_render_distance, u_position):
        super().bind()
        glUniform1f(self.u_render_distance_loc, u_render_distance)
        glUniform3f(self.u_position_loc, *u_position)

class SkyboxShader(Shader):
    def __init__(self, vert_code, frag_code):
        super().__init__(vert_code, frag_code)
        self.u_position_loc = glGetUniformLocation(self.program, b"u_position")

    def bind(self, u_position):
        super().bind()
        glUniform3f(self.u_position_loc, *u_position)


FACES = (
    (0, 1, 0),
    (0, -1, 0),
    (-1, 0, 0),
    (1, 0, 0),
    (0, 0, 1),
    (0, 0, -1),
)

def empty_update_func(self, x, y, z):
    return False

block_update_func = {
    'block.minecraft.nature.grass_block': empty_update_func,
    'block.minecraft.nature.dirt': empty_update_func,
    'block.minecraft.nature.bedrock': empty_update_func,
    'block.minecraft.nature.stone': empty_update_func,
    'block.minecraft.wood.oak_log': empty_update_func,
    'block.minecraft.wood.oak_leaves': empty_update_func,
}

def grass_block_random_tick_func(self, x, y, z):
    if self.world.get_block(x, y + 1, z) != 1 and self.world.get_block(x, y + 1, z) != 0:
        self.world.remove_block(x, y, z)
        self.world.add_block(x, y, z, 'block.minecraft.nature.dirt')
        return True
    return False

def dirt_random_tick_func(self, x, y, z):
    for dy in (-1, 0, 1):
        for dx, _, dz in FACES[2:]:
            nx, ny, nz = x + dx, y + dy, z + dz
            if (self.world.get_block(nx, ny, nz) == 2 and (self.world.get_block(x, y + 1, z) == 1 or self.world.get_block(x, y + 1, z) == 0)):
                self.world.remove_block(x, y, z)
                self.world.add_block(x, y, z, 'block.minecraft.nature.grass_block')
                return True

block_random_tick_func = {
    'block.minecraft.nature.grass_block': grass_block_random_tick_func,
    'block.minecraft.nature.dirt': dirt_random_tick_func,
    'block.minecraft.nature.bedrock': empty_update_func,
    'block.minecraft.nature.stone': empty_update_func,
    'block.minecraft.wood.oak_log': empty_update_func,
    'block.minecraft.wood.oak_leaves': empty_update_func,
}

with open("Shaders/block_vertex_shader.glsl") as f:
    block_vertex_shader_code = f.read()
with open("Shaders/block_fragment_shader.glsl") as f:
    block_fragment_shader_code = f.read()
with open("Shaders/skybox_vertex_shader.glsl") as f:
    skybox_vertex_shader_code = f.read()
with open("Shaders/skybox_fragment_shader.glsl") as f:
    skybox_fragment_shader_code = f.read()


class Window(pyglet.window.Window):
    def __init__(self, *args, **kw):
        super().__init__(*args, **kw)
        # 是否锁定鼠标
        self.exclusive = False
        # Strafing is moving lateral to the direction you are facing,
        # e.g. moving to the left or right while continuing to face forward.
        # First element is -1 when moving forward, 1 when moving back, and 0
        # otherwise. The second element is -1 when moving left, 1 when moving
        # right, and 0 otherwise.
        # 按键
        self.strafe = [0, 0]
        self.position = (0, 80, 0)
        self.flying = False
        self.delta = [0, 0, 0]
        # First element is rotation of the player in the x-z plane (ground
        # plane) measured from the z-axis down. The second is the rotation
        # angle from the ground plane up. Rotation is in degrees.
        #
        # The vertical plane rotation ranges from -90 (looking straight down) to
        # 90 (looking straight up). The horizontal rotation range is unbounded.
        # 视线方向
        self.rotation = (0, 0)
        # 准星
        self.reticle = None
        self.escape_menu_shade = None
        self.render_distance = simulate_distance
        self.world = World(random.randint(0, 2 ** 31 - 5), self.render_distance)
        # 显示的方块，pyglet渲染对象
        self.shown = {}
        self.batch = pyglet.graphics.Batch()
        # 按键
        self.space = False
        self.shift = False
        self.control = False
        self.shown_sectors = set()
        self.loaded_sectors = set()
        # 注册碰撞箱
        self.world.add_block_entity_box('block.minecraft.nature.grass_block', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_block_entity_box('block.minecraft.nature.dirt', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_block_entity_box('block.minecraft.nature.bedrock', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_block_entity_box('block.minecraft.nature.stone', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_block_entity_box('block.minecraft.nature.stone', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_block_entity_box('block.minecraft.wood.oak_log', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_block_entity_box('block.minecraft.wood.oak_leaves', -0.5, 0.5, -0.5, 0.5, -0.5, 0.5)
        self.world.add_entity_entity_box('entity.minecraft.player', -0.3, 0.3, -0.5, 1.3, -0.3, 0.3)
        # 决定渲染哪些东西
        self.level = 'escape_menu'
        # 所有按钮
        self.mouse_position = (-1, -1)
        self.buttons = {
            'escape_menu.resume_game': Button(0, 0, 480, 40, 'Resume game', (111, 111, 111, 255), (117, 127, 186, 255), (255, 255, 255, 255), self.resume_game),
            'escape_menu.save_and_return': Button(0, 0, 480, 40, 'Save and return to the main menu', (111, 111, 111, 255), (117, 127, 186, 255), (255, 255, 255, 255), self.save_and_return),
        }
        # 按钮相对于窗口中心的偏移量，用于on_resize
        self.buttons_offset = {
            'escape_menu.resume_game': (0, 64),
            'escape_menu.save_and_return': (0, -64),
        }
        # 注册透明方块
        self.world.add_transparent_block('block.minecraft.wood.oak_leaves')
        # 着色器
        self.block_shader = BlockShader(block_vertex_shader_code, block_fragment_shader_code)
        self.skybox_shader = SkyboxShader(skybox_vertex_shader_code, skybox_fragment_shader_code)
        # 函数字典
        self.functions = {'update_shown': self.update_shown, 'hide_block': self.hide_block, 'block_update': self.block_update}
        # 更新玩家位置
        pyglet.clock.schedule_interval(self.update, 1 / 60)
        # 随机刻
        pyglet.clock.schedule_interval(self.porcess_random_tick, 1 / gamerule['tick_per_second'])
        # 创建处理区块的线程
        self.world.start_process_sector_thread()

    def resume_game(self):
        self.set_exclusive_mouse(True)
        self.level = 'normal'

    def save_and_return(self):
        self.on_close()

    def update_shown(self, x, y, z):
        vertex_data = cube_vertices(x, y, z, 0.5)
        if (x, y, z) not in self.shown:
            self.shown[(x, y, z)] = [0, {}]
        shown = self.world.get_shown(x, y, z)
        block_type = block_id[self.world.get_block(x, y, z)]
        for i in range(64):
            mask = 1 << i
            if ((shown & mask) != (self.shown[(x, y, z)][0] & mask)):
                if (self.shown[(x, y, z)][0] & mask):
                    self.shown[(x, y, z)][1][i].delete()
                    del self.shown[(x, y, z)][1][i]
                else:
                    tex_coords = get_tex_array_data(block_type, i)
                    self.shown[(x, y, z)][1][i] = self.batch.add(
                        4, GL_QUADS, None,
                        ('v3f/static', vertex_data[i]),
                        ('t3f/static', tex_coords)
                    )
        self.shown[(x, y, z)][0] = shown

    def hide_block(self, x, y, z):
        if (x, y, z) not in self.shown:
            return
        for i in self.shown[(x, y, z)][1]:
            self.shown[(x, y, z)][1][i].delete()
        del self.shown[(x, y, z)]

    def block_update(self, x, y, z):
        for dx, dy, dz in FACES:
            nx, ny, nz = x + dx, y + dy, z + dz
            block = self.world.get_block(nx, ny, nz)
            if block != 1 and block != 0:
                if block_update_func[block_id[block]](self, nx, ny, nz):
                    self.world.add_operation("block_update", nx, ny, nz)

    def porcess_random_tick(self, dt):
        nx, ny = int(round(self.position[0])) // 16, int(round(self.position[2])) // 16
        for dx, dy in simulate_sectors:
            x, y = nx + dx, ny + dy
            for _ in range(gamerule['random_tick_speed']):
                sdx = random.randint(0, 15)
                sdz = random.randint(0, 15)
                sdy = random.randint(0, 127)
                pos = (x * 16 + sdx, sdy, y * 16 + sdz)
                if (self.world.get_block(*pos) != 1 and self.world.get_block(*pos) != 0):
                    if block_random_tick_func[block_id[self.world.get_block(*pos)]](self, *pos):
                        self.world.add_operation(f'block_update {pos[0]} {pos[1]} {pos[2]}')

    def set_exclusive_mouse(self, exclusive):
        """ If `exclusive` is True, the game will capture the mouse, if False
        the game will ignore the mouse.
        显示/隐藏鼠标

        """
        super().set_exclusive_mouse(exclusive)
        self.exclusive = exclusive

    def get_sight_vector(self):
        """ Returns the current line of sight vector indicating the direction
        the player is looking.
        获取视线向量

        """
        x, y = self.rotation
        # y ranges from -90 to 90, or -pi/2 to pi/2, so m ranges from 0 to 1 and
        # is 1 when looking ahead parallel to the ground and 0 when looking
        # straight up or down.
        m = math.cos(math.radians(y))
        # dy ranges from -1 to 1 and is -1 when looking straight down and 1 when
        # looking straight up.
        dy = math.sin(math.radians(y))
        dx = math.cos(math.radians(x - 90)) * m
        dz = math.sin(math.radians(x - 90)) * m
        return (dx, dy, dz)

    def get_motion_vector(self):
        """ Returns the current motion vector indicating the velocity of the
        player.

        Returns
        -------
        vector : tuple of len 3
            Tuple containing the velocity in x, y, and z respectively.
        获取玩家移动向量

        """
        if any(self.strafe):
            x, y = self.rotation
            strafe = math.degrees(math.atan2(*self.strafe))
            y_angle = math.radians(y)
            x_angle = math.radians(x + strafe)
            dy = 0.0
            dx = math.cos(x_angle)
            dz = math.sin(x_angle)
        else:
            dy = 0.0
            dx = 0.0
            dz = 0.0
        if self.flying and self.space:
            dy = 0.5
        if self.flying and self.shift:
            dy = -0.5
        return (dx, dy, dz)

    def process_queue(self):
        start = time.perf_counter()
        while time.perf_counter() - start < 1 / gamerule['tick_per_second'] / 2:
            operation = self.world.give_operation()
            if operation == 'None':
                break
            operation = operation.split(' ')
            for i in range(1, len(operation)):
                operation[i] = int(operation[i])
            self.functions[operation[0]](*operation[1:])

    def porcess_sectors(self, dt):
        """ 处理区块的加载

        """
        nx, ny = int(round(self.position[0])) // 16, int(round(self.position[2])) // 16
        for dx, dy in simulate_sectors:
            x, y = nx + dx, ny + dy
            if (x, y) not in self.loaded_sectors:
                self.world.generate_sector_python(x, y)
                self.loaded_sectors.add((x, y))
                self.shown_sectors.add((x, y))
        now_shown = set()
        for dx, dy in simulate_sectors:
            x, y = nx + dx, ny + dy
            now_shown.add((x, y))
        show = now_shown - self.shown_sectors
        hide = self.shown_sectors - now_shown
        for x, y in show:
            blocks = self.world.get_sector_shown_blocks(x, y)
            for block in blocks:
                self.show_block(*block)
            self.shown_sectors.add((x, y))
        for x, y in hide:
            blocks = self.world.get_sector_shown_blocks(x, y)
            for block in blocks:
                self.hide_block(*block)
            self.shown_sectors.remove((x, y))

    def update(self, dt):
        """ This method is scheduled to be called repeatedly by the pyglet
        clock.

        Parameters
        ----------
        dt : float
            The change in time since the last call.
        循环调用，处理移动、C++传过来的操作(show_block)

        """
        if self.exclusive:
            m = 16    # 数字越大，精度越高
            dt = min(dt, 0.2)
            for _ in range(m):
                self._update(dt / m)
            if self.space and self.delta[1] == 0 and not self.flying:
                self.delta[1] = gamerule['jump_speed']
        self.world.set_position(*self.position)
        self.process_queue()

    def _update(self, dt):
        """ Private implementation of the `update()` method. This is where most
        of the motion logic lives, along with gravity and collision detection.

        Parameters
        ----------
        dt : float
            The change in time since the last call.
        处理移动

        """
        speed = 0
        if self.flying and self.control:
            speed = gamerule['fly_run_speed']
        elif self.flying:
            speed = gamerule['fly_speed']
        elif self.control:
            speed = gamerule['run_speed']
        elif self.control and self.delta[1] != 0:
            speed = gamerule['run_jump_speed']
        elif self.shift:
            speed = gamerule['sneek_speed']
        else:
            speed = gamerule['walk_speed']
        # 摩擦系数
        m = 3 if self.flying or self.delta[1] else 15
        d = dt * speed * m
        dx, dy, dz = self.get_motion_vector()
        dx, dz = dx * d + self.delta[0], dz * d + self.delta[2]
        if self.flying:
            dy = dy * d / m * 15 + self.delta[1]
        else:
            dy = dy * d + self.delta[1]
        dx, dz = dx * (1 - dt * m) , dz * (1 - dt * m)
        self.delta = [dx, dy, dz]
        # 重力
        if not self.flying:
            # Update your vertical speed: if you are falling, speed up until you
            # hit terminal velocity; if you are jumping, slow down until you
            # start falling.
            self.delta[1] -= dt * gamerule['gravity']
            # 阻力
            self.delta[1] *= (1 - 0.02 * dt)
            #self.delta[1] = max(self.delta[1], -gamerule['max_speed'])
        else:
            # 阻力
            self.delta[1] *= (1 - dt * 15)
        # 处理碰撞
        x, y, z = self.position
        y += self.delta[1] * dt
        if self.world.intersect('entity.minecraft.player', x, y, z):
            y -= self.delta[1] * dt
            self.delta[1] = 0
        x += self.delta[0] * dt
        if self.world.intersect('entity.minecraft.player', x, y, z):
            x -= self.delta[0] * dt
            self.delta[0] = 0
        elif self.shift and (not self.flying) and (not self.world.intersect('entity.minecraft.player', x, y - 0.05, z)):
            x -= self.delta[0] * dt
            self.delta[0] = 0
        z += self.delta[2] * dt
        if self.world.intersect('entity.minecraft.player', x, y, z):
            z -= self.delta[2] * dt
            self.delta[2] = 0
        elif self.shift and (not self.flying) and (not self.world.intersect('entity.minecraft.player', x, y - 0.05, z)):
            z -= self.delta[2] * dt
            self.delta[2] = 0
        self.position = (x, y, z)

    def on_mouse_press(self, x, y, button, modifiers):
        """ Called when a mouse button is pressed. See pyglet docs for button
        amd modifier mappings.

        Parameters
        ----------
        x, y : int
            The coordinates of the mouse click. Always center of the screen if
            the mouse is captured.
        button : int
            Number representing mouse button that was clicked. 1 = left button,
            4 = right button.
        modifiers : int
            Number representing any modifying keys that were pressed when the
            mouse button was clicked.
        处理鼠标点击

        """
        for key in self.buttons:
            origin_key = key
            key = key.split('.')
            key.pop()
            key = '.'.join(key)
            if (self.level == key):
                self.buttons[origin_key].on_mouse_press(x, y, button, modifiers)
        if self.exclusive:
            dx, dy, dz = self.get_sight_vector()
            x, y, z = self.position
            y += 1.2
            if self.shift and not self.flying:
                y -= 0.5
            block, previous = self.world.hit_test(x, y, z, dx, dy, dz, 5)
            #print(block, previous)
            if (button == mouse.RIGHT):
                if previous:
                    nx, ny, nz = previous
                    self.world.add_block(nx, ny, nz, "block.minecraft.wood.oak_leaves")
            elif button == pyglet.window.mouse.LEFT and block:
                self.world.remove_block(*block)

    def on_mouse_motion(self, x, y, dx, dy):
        """ Called when the player moves the mouse.

        Parameters
        ----------
        x, y : int
            The coordinates of the mouse click. Always center of the screen if
            the mouse is captured.
        dx, dy : float
            The movement of the mouse.
        处理鼠标移动

        """
        self.mouse_position = (x, y)
        if self.exclusive:
            m = 0.2
            x, y = self.rotation
            x, y = x + dx * m, y + dy * m
            y = max(-90, min(90, y))
            self.rotation = (x, y)

    def on_key_press(self, symbol, modifiers):
        """ Called when the player presses a key. See pyglet docs for key
        mappings.

        Parameters
        ----------
        symbol : int
            Number representing the key that was pressed.
        modifiers : int
            Number representing any modifying keys that were pressed.
        处理键盘事件

        """
        if symbol == key.W:
            self.strafe[0] -= 1
        elif symbol == key.S:
            self.strafe[0] += 1
        elif symbol == key.A:
            self.strafe[1] -= 1
        elif symbol == key.D:
            self.strafe[1] += 1
        elif symbol == key.SPACE:
            self.space = True
        elif symbol == key.LSHIFT:
            self.shift = True
        elif symbol == key.LCTRL:
            self.control = True
        elif symbol == key.ESCAPE:
            if self.level == 'normal':
                self.set_exclusive_mouse(False)
                self.level = 'escape_menu'
            elif self.level == 'escape_menu':
                self.set_exclusive_mouse(True)
                self.level = 'normal'
        elif symbol == key.TAB:
            self.flying = not self.flying
            self.delta[1] = 0
        elif symbol == key.G:
            print(self.position)

    def on_key_release(self, symbol, modifiers):
        """ Called when the player releases a key. See pyglet docs for key
        mappings.

        Parameters
        ----------
        symbol : int
            Number representing the key that was pressed.
        modifiers : int
            Number representing any modifying keys that were pressed.
        处理键盘事件

        """
        if symbol == key.W:
            self.strafe[0] += 1
        elif symbol == key.S:
            self.strafe[0] -= 1
        elif symbol == key.A:
            self.strafe[1] += 1
        elif symbol == key.D:
            self.strafe[1] -= 1
        elif symbol == key.SPACE:
            self.space = False
        elif symbol == key.LSHIFT:
            self.shift = False
        elif symbol == key.LCTRL:
            self.control = False

    def on_close(self):
        del self.world
        self.close()

    def on_resize(self, width, height):
        """ Called when the window is resized to a new `width` and `height`.
        窗口大小改变时调用

        """
        # label
        # self.label.y = height - 10
        # reticle
        if self.reticle:
            self.reticle.delete()
        x, y = self.width // 2, self.height // 2
        n = 12
        self.reticle = pyglet.graphics.vertex_list(4,
            ('v2i', (x - n, y, x + n, y, x, y - n, x, y + n)),
            ('c4B', (100, 100, 100, 255) * 4)
        )
        if self.escape_menu_shade:
            self.escape_menu_shade.delete()
        self.escape_menu_shade = pyglet.graphics.vertex_list(4,
            ('v2i', (0, 0, width, 0, width, height, 0, height)),
            ('c4B', (0, 0, 0, 128) * 4)
        )
        for key in self.buttons:
            self.buttons[key].replace(width // 2 + self.buttons_offset[key][0], height // 2 + self.buttons_offset[key][1])

    def set_2d(self):
        """ Configure OpenGL to draw in 2d.

        """
        width, height = self.get_size()
        glDisable(GL_DEPTH_TEST)
        viewport = self.get_viewport_size()
        glViewport(0, 0, max(1, viewport[0]), max(1, viewport[1]))
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        glOrtho(0, max(1, width), 0, max(1, height), -1, 1)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()

    def set_3d(self):
        """ Configure OpenGL to draw in 3d.

        """
        width, height = self.get_size()
        glEnable(GL_DEPTH_TEST)
        viewport = self.get_viewport_size()
        glViewport(0, 0, max(1, viewport[0]), max(1, viewport[1]))
        glMatrixMode(GL_PROJECTION)
        glLoadIdentity()
        gluPerspective(100.0, width / float(height), 0.05, 16 * self.render_distance + 16)
        glMatrixMode(GL_MODELVIEW)
        glLoadIdentity()
        x, y = self.rotation
        glRotatef(x, 0, 1, 0)
        glRotatef(-y, math.cos(math.radians(x)), 0, math.sin(math.radians(x)))
        x, y, z = self.position
        y += 1.2
        if self.shift and not self.flying:
            y -= 0.5
        glTranslatef(-x, -y, -z)

    def on_draw(self):
        """ Called by pyglet to draw the canvas.

        """
        self.clear()
        self.set_3d()
        # 绘制天空
        glDepthMask(GL_FALSE)
        self.draw_sky()
        glDepthMask(GL_TRUE)
        # 绑定纹理
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex_array_id)
        self.block_shader.bind(self.render_distance * 16 - 16, self.position)
        self.batch.draw()
        self.block_shader.unbind()
        glDepthFunc(GL_LEQUAL)
        self.draw_focused_block()
        glDepthFunc(GL_LESS)
        self.set_2d()
        self.reticle.draw(GL_LINES)
        if self.level == 'escape_menu':
            self.escape_menu_shade.draw(GL_QUADS)
        for key in self.buttons:
            origin_key = key
            key = key.split('.')
            key.pop()
            key = '.'.join(key)
            if (self.level == key):
                self.buttons[origin_key].draw(self.mouse_position)

    def draw_focused_block(self):
        """ Draw black edges around the block that is currently under the
        crosshairs.

        """
        dx, dy, dz = self.get_sight_vector()
        x, y, z = self.position
        y += 1.2
        if self.shift and not self.flying:
            y -= 0.5
        block = self.world.hit_test(x, y, z, dx, dy, dz, 5)[0]
        if block:
            x, y, z = block
            vertex_data = cube_vertices(x, y, z, 0.501)
            glColor3d(0, 0, 0)
            for i in vertex_data:
                pyglet.graphics.draw(4, GL_LINE_LOOP, ('v3f', i))

    def draw_sky(self):
        """ 绘制天空盒

        """
        vertex = cube_vertices(*self.position, 16)
        self.skybox_shader.bind(self.position)
        for i in vertex:
            pyglet.graphics.draw(4, GL_QUADS, ('v3f', i))
        self.skybox_shader.unbind()

window = Window(width=960, height=540, caption='Minecraft', resizable=True)
glLineWidth(2.0)
# 启用Alpha混合
glEnable(GL_BLEND)
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
pyglet.app.run()
