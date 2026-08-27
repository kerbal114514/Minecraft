import math
import time
import random
import ctypes
import os
import sys
import importlib

import pyglet
from pyglet.gl import *
from pyglet.window import key, mouse

from MCworld import World

sys.path.append("./codes/")
Button = getattr(importlib.import_module("PygletButton"), "Button")

block_id = (
    "block.minecraft.sector_not_loaded",
    "block.minecraft.air",
    "block.minecraft.grass_block",
    "block.minecraft.dirt",
    "block.minecraft.stone",
    "block.minecraft.bedrock",
    "block.minecraft.oak_log",
    "block.minecraft.oak_leaves",
    "block.minecraft.glowstone",
)
id_block = {
    "block.minecraft.sector_not_loaded":   0,
    "block.minecraft.air":                 1,
    "block.minecraft.grass_block":         2,
    "block.minecraft.dirt":                3,
    "block.minecraft.stone":               4,
    "block.minecraft.bedrock":             5,
    "block.minecraft.oak_log":             6,
    "block.minecraft.oak_leaves":          7,
    "block.minecraft.glowstone":           8,
};

gamerule = {
    "tick_per_second": 20,
    "random_tick_speed": 3,
    "walk_speed": 4.3,
    "sneek_speed": 1.52,
    "run_speed": 5.6,
    "run_jump_speed": 7.5,
    "fly_speed": 11,
    "fly_run_speed": 22,
    "gravity": 32,
    "jump_speed": 8.85,
    "terminal_speed": 78.4,
}

settings = {
    "gui_size": 2,
    "simulate_distance": 16,
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

simulate_distance = settings["simulate_distance"]
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
    "glowstone.png",          # 8
]
textures = {
    "block.minecraft.grass_block": (0, 2, 1, 1, 1, 1),
    "block.minecraft.dirt": (2, 2, 2, 2, 2, 2),
    "block.minecraft.bedrock": (3, 3, 3, 3, 3, 3),
    "block.minecraft.stone": (4, 4, 4, 4, 4, 4),
    "block.minecraft.oak_log": (5, 5, 6, 6, 6, 6),
    "block.minecraft.oak_leaves": (7, 7, 7, 7, 7, 7),
    "block.minecraft.glowstone": (8, 8, 8, 8, 8, 8),
}

def create_texture_array(image_list, path="./Textures"):
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
        data = img.get_data("RGBA", img.width * 4)
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i, width, height, 1, GL_RGBA, GL_UNSIGNED_BYTE, data)

    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_LINEAR)
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY)

    return tex_id

def replace_mipmap(tex_id, layer, level, img_name):
    glBindTexture(GL_TEXTURE_2D_ARRAY, tex_id)
    img_path = os.path.join("./Textures", img_name)
    img = pyglet.image.load(img_path)
    data = img.get_data("RGBA", img.width * 4)
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

gui_texture_id = GLuint(0)
glGenTextures(1, ctypes.byref(gui_texture_id))
img = pyglet.image.load("./Textures/gui.png")
glBindTexture(GL_TEXTURE_2D, gui_texture_id)
glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.get_data("RGBA", img.width * 4))
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST)
glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
glBindTexture(GL_TEXTURE_2D, 0)

def get_tex_array_data(block_name, face_index):
    img_idx = textures[block_name][face_index]
    uvs = [0,0, 1,0, 1,1, 0,1]
    res = []
    for i in range(0, 8, 2):
        res.extend([uvs[i], uvs[i+1], float(img_idx)])
    return res


class Shader:
    def __init__(self, vert_code, frag_code, *uniform_args):
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
        self.uniform_args_loc = {}
        for i in uniform_args:
            self.uniform_args_loc[i] = glGetUniformLocation(self.program, bytes(i, encoding="ascii"))

    def compile_shader(self, code, shader_type):
        shader = glCreateShader(shader_type)
        # 转换字符串为底层 C 指针
        src = ctypes.create_string_buffer(code.encode("utf-8"))
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

    def bind(self, **uniform_args):
        glUseProgram(self.program)
        for key in uniform_args:
            word = uniform_args[key]
            if len(word) == 1:
                glUniform1f(self.uniform_args_loc[key], *word)
            elif len(word) == 2:
                glUniform1f(self.uniform_args_loc[key], *word)
            elif len(word) == 3:
                glUniform3f(self.uniform_args_loc[key], *word)
            elif len(word) == 4:
                glUniform4f(self.uniform_args_loc[key], *word)

    def unbind(self):
        glUseProgram(0)


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
    "block.minecraft.sector_not_loaded": empty_update_func,
    "block.minecraft.air": empty_update_func,
    "block.minecraft.grass_block": empty_update_func,
    "block.minecraft.dirt": empty_update_func,
    "block.minecraft.bedrock": empty_update_func,
    "block.minecraft.stone": empty_update_func,
    "block.minecraft.oak_log": empty_update_func,
    "block.minecraft.oak_leaves": empty_update_func,
    "block.minecraft.glowstone": empty_update_func,
}

def grass_block_random_tick_func(self, x, y, z):
    if self.world.get_block(x, y + 1, z) != 1 and self.world.get_block(x, y + 1, z) != 0:
        self.world.remove_block(x, y, z)
        self.world.add_block(x, y, z, id_block["block.minecraft.dirt"])
        return True
    return False

def dirt_random_tick_func(self, x, y, z):
    for dy in (-1, 0, 1):
        for dx, _, dz in FACES[2:]:
            nx, ny, nz = x + dx, y + dy, z + dz
            if (self.world.get_block(nx, ny, nz) == 2 and (self.world.get_block(x, y + 1, z) == 1 or self.world.get_block(x, y + 1, z) == 0)):
                self.world.remove_block(x, y, z)
                self.world.add_block(x, y, z, id_block["block.minecraft.grass_block"])
                return True

block_random_tick_func = {
    "block.minecraft.sector_not_loaded": empty_update_func,
    "block.minecraft.air": empty_update_func,
    "block.minecraft.grass_block": empty_update_func,#grass_block_random_tick_func,
    "block.minecraft.dirt": empty_update_func,#dirt_random_tick_func,
    "block.minecraft.bedrock": empty_update_func,
    "block.minecraft.stone": empty_update_func,
    "block.minecraft.oak_log": empty_update_func,
    "block.minecraft.oak_leaves": empty_update_func,
    "block.minecraft.glowstone": empty_update_func,
}

def empty_item_use_func(self):
    pass

def block_item_use_func(self, name):
    x, y, z = self.position
    y += 1.2
    if self.shift and not self.flying:
        y -= 0.5
    block, previous = self.world.hit_test(x, y, z, *self.get_sight_vector(), 5)
    if previous:
        self.world.add_block(*previous, id_block[name])

item_use_func = {
    "item.minecraft.null": empty_item_use_func,
    "item.minecraft.grass_block": lambda self : block_item_use_func(self, "block.minecraft.grass_block"),
    "item.minecraft.dirt": lambda self : block_item_use_func(self, "block.minecraft.dirt"),
    "item.minecraft.bedrock": lambda self : block_item_use_func(self, "block.minecraft.bedrock"),
    "item.minecraft.stone": lambda self : block_item_use_func(self, "block.minecraft.stone"),
    "item.minecraft.oak_log": lambda self : block_item_use_func(self, "block.minecraft.oak_log"),
    "item.minecraft.oak_leaves": lambda self : block_item_use_func(self, "block.minecraft.oak_leaves"),
    "item.minecraft.glowstone": lambda self : block_item_use_func(self, "block.minecraft.glowstone"),
}

items_texture_id = {}
for key_ in item_use_func.keys():
    items_texture_id[key_] = GLuint(0)
    glGenTextures(1, ctypes.byref(items_texture_id[key_]))
    img = pyglet.image.load(f"./Textures/{key_}.png")
    glBindTexture(GL_TEXTURE_2D, items_texture_id[key_])
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, img.width, img.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, img.get_data("RGBA", img.width * 4))
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST)
    glGenerateMipmap(GL_TEXTURE_2D)
    glBindTexture(GL_TEXTURE_2D, 0)

block_friction = (    # 摩擦系数
    2,    # block.minecraft.air
    2,    # block.minecraft.sector_not_loaded
    15,   # block.minecraft.grass_block
    15,   # block.minecraft.dirt
    15,   # block.minecraft.bedrock
    15,   # block.minecraft.stone
    15,   # block.minecraft.oak_log
    15,   # block.minecraft.oak_leaves
    15,   # block.minecraft.glowstone
)

with open("Shaders/block_vertex_shader.glsl") as f:
    block_vertex_shader_code = f.read()
with open("Shaders/block_fragment_shader.glsl") as f:
    block_fragment_shader_code = f.read()
with open("Shaders/skybox_vertex_shader.glsl") as f:
    skybox_vertex_shader_code = f.read()
with open("Shaders/skybox_fragment_shader.glsl") as f:
    skybox_fragment_shader_code = f.read()

entity_id = {
    "entity.minecraft.player": 0,
}

class Window(pyglet.window.Window):
    def __init__(self, *args, **kw):
        super().__init__(*args, **kw)
        # 设置窗口图标
        self.set_icon(pyglet.image.load("./Textures/item.minecraft.grass_block.png"))
        # 是否锁定鼠标
        self.exclusive = False
        # Strafing is moving lateral to the direction you are facing,
        # e.g. moving to the left or right while continuing to face forward.
        # First element is -1 when moving forward, 1 when moving back, and 0
        # otherwise. The second element is -1 when moving left, 1 when moving
        # right, and 0 otherwise.
        # 按键
        self.strafe = [0, 0]
        self.position = (0, 180, 0)
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
        self.escape_menu_shadow = None
        self.loading_shadow = None
        self.inventory_gui = None
        self.render_distance = simulate_distance
        seed = -1
        for i in range(len(sys.argv)):
            if sys.argv[i] == "--seed":
                seed = int(sys.argv[i + 1])
        if seed == -1:
            seed = random.randint(0, 2 ** 31 - 5)
        print("seed:", seed)
        self.world = World(seed, self.render_distance)
        # 按键
        self.space = False
        self.last_space_press = -1
        self.shift = False
        self.control = False
        # 决定渲染哪些东西
        self.level = "loading_world"
        # 所有按钮
        self.mouse_position = (-1, -1)
        gs = settings["gui_size"]
        self.buttons = {
            "escape_menu.resume_game": Button(0, 0, 240 * gs , 20 * gs, "Resume game", (111, 111, 111, 255), (117, 127, 186, 255), (255, 255, 255, 255), ("Consolas", 6 * gs), self.resume_game),
            "escape_menu.save_and_return": Button(0, 0, 240 * gs, 20 * gs, "Save and return to the main menu", (111, 111, 111, 255), (117, 127, 186, 255), (255, 255, 255, 255), ("Consolas", 6 * gs), self.save_and_return),
        }
        # 按钮相对于窗口中心的偏移量，用于on_resize
        self.buttons_offset = {
            "escape_menu.resume_game": (0, 15),
            "escape_menu.save_and_return": (0, -15),
        }
        # 着色器
        self.block_shader = Shader(block_vertex_shader_code, block_fragment_shader_code, "u_render_distance", "u_position", "u_light_direction", "u_player_sky_light")
        self.skybox_shader = Shader(skybox_vertex_shader_code, skybox_fragment_shader_code, "u_player_sky_light")
        # VBO id
        self.vbo_id = {}
        self.vao_id = {}
        self.vbo_size = {}
        self.vbo_reserve_size = {}
        # 函数字典
        self.functions = {"block_update": self.block_update, "update_vbo_data": self.update_vbo_data, "set_schedule": self.set_schedule, "init_done": self.init_done}
        # 生成天空盒顶点数据
        vertex = []
        x, y, z = 0, 0, 0
        for i in range(0, 360):
            x1, z1 = math.cos(math.radians(i)) * 16 + x, math.sin(math.radians(i)) * 16 + z
            x2, z2 = math.cos(math.radians(i + 1)) * 16 + x, math.sin(math.radians(i + 1)) * 16 + z
            vertex.extend((x1, y - 16, z1, x1, y + 16, z1, x2, y + 16, z2, x2, y - 16, z2))
        vertex.extend((x - 16, y - 16, z - 16, x - 16, y - 16, z + 16, x + 16, y - 16, z + 16, x + 16, y - 16, z - 16))
        vertex.extend((x - 16, y + 16, z - 16, x - 16, y + 16, z + 16, x + 16, y + 16, z + 16, x + 16, y + 16, z - 16))
        self.sky_box = pyglet.graphics.vertex_list(len(vertex) // 3, ("v3f", vertex))
        self.inventory = []    # 二维数组，[0][x]是物品栏，[1-3][x]是背包，[4][x]备用，x: 1-9
        for i in range(5):
            self.inventory.append([None, "item.minecraft.null", "item.minecraft.null", "item.minecraft.null", "item.minecraft.null",\
                "item.minecraft.null", "item.minecraft.null", "item.minecraft.null", "item.minecraft.null", "item.minecraft.null"])
        self.inventory[0][1] = "item.minecraft.grass_block"
        self.inventory[0][2] = "item.minecraft.dirt"
        self.inventory[0][3] = "item.minecraft.stone"
        self.inventory[0][4] = "item.minecraft.oak_log"
        self.inventory[0][5] = "item.minecraft.oak_leaves"
        self.inventory[0][6] = "item.minecraft.glowstone"
        self.activated_inventory_id = 1
        self.world.set_position(*self.position)
        # 上一次渲染时的 player_sky_light 值
        self.last_player_sky_light = 0
        # 更新玩家位置
        pyglet.clock.schedule_interval(self.update, 1 / 60)
        # 随机刻
        pyglet.clock.schedule_interval(self.process_random_tick, 1 / gamerule["tick_per_second"])

    def set_schedule(self, sort: str, nowcnt: int, allcnt: int):
        print(f"{sort}: {nowcnt} of {allcnt}, {int(round(nowcnt / allcnt * 100))}%", end="             \r")

    def init_done(self):
        # 创建处理区块的线程
        self.world.start_process_sector_thread()
        self.resume_game()

    def resume_game(self):
        self.set_exclusive_mouse(True)
        self.level = "normal"

    def save_and_return(self):
        self.on_close()

    def update_vbo_data(self, x, y):
        if (x, y) not in self.vbo_id:
            vbo_id = GLuint(0)
            vao_id = GLuint(0)
            glGenBuffers(1, ctypes.byref(vbo_id))
            glGenVertexArrays(1, ctypes.byref(vao_id))
            self.vbo_id[(x, y)] = vbo_id
            self.vao_id[(x, y)] = vao_id
            self.vbo_reserve_size[(x, y)] = 0
            glBindVertexArray(vao_id)
            glBindBuffer(GL_ARRAY_BUFFER, vbo_id)
            # a_pos (location 0)
            glEnableVertexAttribArray(0)
            # a_tex_coords (location 1)
            glEnableVertexAttribArray(1)
            # draw_flag (location 2)
            glEnableVertexAttribArray(2)
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, ctypes.sizeof(GLfloat) * 7, ctypes.c_void_p(0 * 4))
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, ctypes.sizeof(GLfloat) * 7, ctypes.c_void_p(3 * 4))
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, ctypes.sizeof(GLfloat) * 7, ctypes.c_void_p(6 * 4))
            glBindVertexArray(0)
            glBindBuffer(GL_ARRAY_BUFFER, 0)
        vbo_id = self.vbo_id[(x, y)]
        glBindBuffer(GL_ARRAY_BUFFER, vbo_id)
        self.world.lock_world_mutex()
        #print(f"\n\n{(x, y)}\n\n")
        data = self.world.get_sector_vbo_data_ptr(x, y)
        data_ptr = ctypes.cast(data[0], ctypes.POINTER(GLfloat))
        if (data[1] > self.vbo_reserve_size[(x, y)]):
            self.vbo_reserve_size[(x, y)] = int(data[1] * 1.2)
            glBufferData(GL_ARRAY_BUFFER, self.vbo_reserve_size[(x, y)] * ctypes.sizeof(GLfloat), None, GL_DYNAMIC_DRAW)
        elif (data[1] * 1.5 < self.vbo_reserve_size[(x, y)]):
            self.vbo_reserve_size[(x, y)] = int(data[1] * 1.2)
            glBufferData(GL_ARRAY_BUFFER, self.vbo_reserve_size[(x, y)] * ctypes.sizeof(GLfloat), None, GL_DYNAMIC_DRAW)
        glBufferSubData(GL_ARRAY_BUFFER, 0, data[1] * ctypes.sizeof(GLfloat), data_ptr)
        self.world.unlock_world_mutex()
        glBindBuffer(GL_ARRAY_BUFFER, 0)
        self.vbo_size[(x, y)] = data[1] // 7

    def block_update(self, x, y, z):
        for dx, dy, dz in FACES:
            nx, ny, nz = x + dx, y + dy, z + dz
            block = self.world.get_block(nx, ny, nz)
            if block != 1 and block != 0:
                if block_update_func[block_id[block]](self, nx, ny, nz):
                    self.world.add_operation("block_update", nx, ny, nz)

    def process_random_tick(self, dt):
        nx, ny = int(round(self.position[0])) // 16, int(round(self.position[2])) // 16
        for dx, dy in simulate_sectors:
            x, y = nx + dx, ny + dy
            for _ in range(gamerule["random_tick_speed"]):
                sdx = random.randint(0, 15)
                sdz = random.randint(0, 15)
                sdy = random.randint(0, 255)
                pos = (x * 16 + sdx, sdy, y * 16 + sdz)
                if (self.world.get_block(*pos) != 1 and self.world.get_block(*pos) != 0):
                    if block_random_tick_func[block_id[self.world.get_block(*pos)]](self, *pos):
                        self.world.add_operation(f"block_update {pos[0]} {pos[1]} {pos[2]}")

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
        while time.perf_counter() - start < 1 / gamerule["tick_per_second"] / 2:
            operation = self.world.give_operation()
            if operation == "None":
                break
            operation = operation.split(" ")
            if operation[0] == "set_schedule":
                operation[2] = int(operation[2])
                operation[3] = int(operation[3])
            else:
                for i in range(1, len(operation)):
                    operation[i] = int(operation[i])
            self.functions[operation[0]](*operation[1:])

    def update(self, dt):
        """ This method is scheduled to be called repeatedly by the pyglet
        clock.

        Parameters
        ----------
        dt : float
            The change in time since the last call.
        循环调用，处理移动、C++传过来的操作(show_block)

        """
        x, y, z = self.position
        if self.exclusive:
            m = 16    # 数字越大，精度越高
            dt = min(dt, 0.2)
            if self.space and self.world.intersect(0, x, y - 0.001, z) and not self.flying:
                self.delta[1] = gamerule["jump_speed"]
            for _ in range(m):
                self._update(dt / m)
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
            speed = gamerule["fly_run_speed"]
        elif self.flying:
            speed = gamerule["fly_speed"]
        elif self.control:
            speed = gamerule["run_speed"]
        elif self.control and self.delta[1] != 0:
            speed = gamerule["run_jump_speed"]
        elif self.shift:
            speed = gamerule["sneek_speed"]
        else:
            speed = gamerule["walk_speed"]
        # 摩擦系数
        m = self.get_friction()
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
            self.delta[1] -= dt * gamerule["gravity"]
            # 阻力
            self.delta[1] *= (1 - (gamerule["gravity"] / gamerule["terminal_speed"]) * dt)
            #self.delta[1] = max(self.delta[1], -gamerule["max_speed"])
        else:
            # 阻力
            self.delta[1] *= (1 - dt * 15)
        if abs(self.delta[0]) < 0.001:
            self.delta[0] = 0
        if abs(self.delta[1]) < 0.001:
            self.delta[1] = 0
        if abs(self.delta[2]) < 0.001:
            self.delta[2] = 0
        # 处理碰撞
        x, y, z = self.position
        y += self.delta[1] * dt
        if self.world.intersect(0, x, y, z):
            y -= self.delta[1] * dt
            self.delta[1] = 0
        if self.flying and self.world.intersect(0, x, y - 0.001, z):
            self.flying = False
        x += self.delta[0] * dt
        if self.world.intersect(0, x, y, z):
            x -= self.delta[0] * dt
            self.delta[0] = 0
        elif self.shift and (not self.flying) and (not self.world.intersect(0, x, y - 0.001, z)):
            x -= self.delta[0] * dt
            self.delta[0] = 0
        z += self.delta[2] * dt
        if self.world.intersect(0, x, y, z):
            z -= self.delta[2] * dt
            self.delta[2] = 0
        elif self.shift and (not self.flying) and (not self.world.intersect(0, x, y - 0.001, z)):
            z -= self.delta[2] * dt
            self.delta[2] = 0
        self.position = (x, y, z)

    def get_friction(self):
        """获取摩擦系数

        """
        x, y, z = self.position
        max_friction = 0
        for dx, dz in ((-0.3, -0.3), (0.3, 0.3), (-0.3, 0.3), (0.3, -0.3), (0, 0)):
            max_friction = max(max_friction, block_friction[self.world.get_block(int(round(x + dx)), int(round(y - 0.55)), int(round(z + dz)))])
        return max_friction

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
        if self.exclusive:
            if (button == mouse.RIGHT):
                item_use_func[self.inventory[0][self.activated_inventory_id]](self)
            elif button == pyglet.window.mouse.LEFT:
                x, y, z = self.position
                y += 1.2
                if self.shift and not self.flying:
                    y -= 0.5
                block, previous = self.world.hit_test(x, y, z, *self.get_sight_vector(), 5)
                if block:
                    self.world.remove_block(*block)
        for key in self.buttons:
            origin_key = key
            key = key.split(".")
            key.pop()
            key = ".".join(key)
            if (self.level == key):
                self.buttons[origin_key].on_mouse_press(x, y, button, modifiers)

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

    def on_mouse_scroll(self, *args):
        """Pyglet1.5.27 has a bug, so I use *args"""
        scroll_x = -int(args[3])
        self.activated_inventory_id += scroll_x
        self.activated_inventory_id = (self.activated_inventory_id - 1) % 9 + 1

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
            this_space_press = time.perf_counter()
            if this_space_press - self.last_space_press < 0.3:
                self.flying = not self.flying
                self.delta[1] = 0
            self.last_space_press = this_space_press
        elif symbol == key.LSHIFT:
            self.shift = True
        elif symbol == key.LCTRL:
            self.control = True
        elif symbol == key.ESCAPE:
            if self.level == "normal":
                self.set_exclusive_mouse(False)
                self.level = "escape_menu"
            elif self.level == "escape_menu":
                self.set_exclusive_mouse(True)
                self.level = "normal"
        elif symbol == key.G:
            print(self.position, self.delta)
        elif symbol == key.L:
            x, y, z = self.position
            print(self.world.get_brightness(int(round(x)), int(round(y)), int(round(z))))
        elif symbol == key._1:
            self.activated_inventory_id = 1
        elif symbol == key._2:
            self.activated_inventory_id = 2
        elif symbol == key._3:
            self.activated_inventory_id = 3
        elif symbol == key._4:
            self.activated_inventory_id = 4
        elif symbol == key._5:
            self.activated_inventory_id = 5
        elif symbol == key._6:
            self.activated_inventory_id = 6
        elif symbol == key._7:
            self.activated_inventory_id = 7
        elif symbol == key._8:
            self.activated_inventory_id = 8
        elif symbol == key._9:
            self.activated_inventory_id = 9

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
        for i in self.vbo_id:
            glDeleteBuffers(1, self.vbo_id[i])
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
            ("v2i", (x - n, y, x + n, y, x, y - n, x, y + n)),
            ("c4B", (100, 100, 100, 255) * 4)
        )
        if self.escape_menu_shadow:
            self.escape_menu_shadow.delete()
        self.escape_menu_shadow = pyglet.graphics.vertex_list(4,
            ("v2i", (0, 0, width, 0, width, height, 0, height)),
            ("c4B", (0, 0, 0, 128) * 4)
        )
        if self.loading_shadow:
            self.loading_shadow.delete()
        self.loading_shadow = pyglet.graphics.vertex_list(4,
            ("v2i", (0, 0, width, 0, width, height, 0, height)),
            ("c4B", (0, 0, 0, 255) * 4)
        )
        if self.inventory_gui:
            self.inventory_gui.delete()
        gs = settings["gui_size"]
        self.inventory_gui = pyglet.graphics.vertex_list(4,
            ("v2i", (width // 2 - 90 * gs, 24, width // 2 + 90 * gs, 24, width // 2 + 90 * gs, 24 + 20 * gs, width // 2 - 90 * gs, 24 + 20 * gs)),
            ("t2f", (1 / 256, 1 - 21 / 64, 181 / 256, 1 - 21 / 64, 181 / 256, 1 - 1 / 64, 1 / 256, 1 - 1 / 64)),
            ("c4B", (255, 255, 255, 255) * 4)
        )
        for key in self.buttons.keys():
            self.buttons[key].replace(width // 2 + self.buttons_offset[key][0] * gs, height // 2 + self.buttons_offset[key][1] * gs, 240 * gs, 20 * gs, ("Consolas", 6 * gs))

    def on_deactivate(self):
        if self.level == "normal":
            self.set_exclusive_mouse(False)
            self.level = "escape_menu"

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

    def on_draw(self):
        """ Called by pyglet to draw the canvas.

        """
        self.clear()
        self.set_3d()
        x, y, z = self.position
        y += 1.2
        if self.shift and not self.flying:
            y -= 0.5
        # 计算玩家所在方块的天空光照
        if int(round(y)) >= 256 or self.world.get_max_height(int(round(x)), int(round(z))) - int(round(y)) < 5:
            player_sky_light = 1.0
        else:
            player_sky_light = self.world.get_brightness(int(round(x)), int(round(y)), int(round(z))) // 16 / 15
        new_player_sky_light = player_sky_light * 0.05 + self.last_player_sky_light * 0.95
        if abs(new_player_sky_light - player_sky_light) > 0.001:
            player_sky_light = new_player_sky_light
        self.last_player_sky_light = player_sky_light
        # 绘制天空
        glDepthMask(GL_FALSE)
        self.draw_sky(player_sky_light)
        glDepthMask(GL_TRUE)
        glTranslatef(-x, -y, -z)
        sector = (int(self.position[0]) // 16, int(self.position[2]) // 16)
        x, y = 45, 60
        # y ranges from -90 to 90, or -pi/2 to pi/2, so m ranges from 0 to 1 and
        # is 1 when looking ahead parallel to the ground and 0 when looking
        # straight up or down.
        m = math.cos(math.radians(y))
        # dy ranges from -1 to 1 and is -1 when looking straight down and 1 when
        # looking straight up.
        dy = math.sin(math.radians(y))
        dx = math.cos(math.radians(x - 90)) * m
        dz = math.sin(math.radians(x - 90)) * m
        if (dx >= dy and dx >= dz):
            tmp = 1 / dx
        if (dy >= dx and dy >= dz):
            tmp = 1 / dy
        if (dz >= dy and dz >= dx):
            tmp = 1 / dz
        # 绑定纹理
        glActiveTexture(GL_TEXTURE0)
        glBindTexture(GL_TEXTURE_2D_ARRAY, tex_array_id)
        self.block_shader.bind(u_render_distance=(float(self.render_distance * 16 - 16), ), u_position=self.position, u_light_direction=(dx * tmp, dy * tmp, dz * tmp), u_player_sky_light=(player_sky_light, ))
        glDepthFunc(GL_LEQUAL)
        for key in self.vao_id:
            if (sector[0] - key[0]) ** 2 + (sector[1] - key[1]) ** 2 > self.render_distance ** 2:
                continue
            glBindVertexArray(self.vao_id[key])
            glDrawArrays(GL_QUADS, 0, self.vbo_size[key])
        glBindVertexArray(0)
        glBindTexture(GL_TEXTURE_2D_ARRAY, 0)
        self.block_shader.unbind()
        self.draw_focused_block()
        glDepthFunc(GL_LESS)
        self.set_2d()
        glEnable(GL_TEXTURE_2D)
        glBindTexture(GL_TEXTURE_2D, gui_texture_id)
        self.inventory_gui.draw(GL_QUADS)
        gs = settings["gui_size"]
        pyglet.graphics.draw(4, GL_QUADS,
            ("v2i", (
                self.width // 2 - 90 * gs - gs + self.activated_inventory_id * 20 * gs - 20 * gs, 24 - gs,
                self.width // 2 - 70 * gs + gs + self.activated_inventory_id * 20 * gs - 20 * gs, 24 - gs,
                self.width // 2 - 70 * gs + gs + self.activated_inventory_id * 20 * gs - 20 * gs, 24 + gs * 20 + gs,
                self.width // 2 - 90 * gs - gs + self.activated_inventory_id * 20 * gs - 20 * gs, 24 + gs * 20 + gs,
            )),
            ("t2f", (1 / 256, 1 - 44 / 64, 22 / 256, 1 - 44 / 64, 22 / 256, 1 - 23 / 64, 1 / 256, 1 - 23 / 64)),
            ("c4B", (255, 255, 255, 255) * 4)
        )
        for i in range(1, 10):
            glBindTexture(GL_TEXTURE_2D, items_texture_id[self.inventory[0][i]])
            pyglet.graphics.draw(4, GL_QUADS,
                ("v2i", (
                    self.width // 2 - 90 * gs + i * 20 * gs - 20 * gs + gs * 2, 24 + gs * 2,
                    self.width // 2 - 70 * gs + i * 20 * gs - 20 * gs - gs * 2, 24 + gs * 2,
                    self.width // 2 - 70 * gs + i * 20 * gs - 20 * gs - gs * 2, 24 + gs * 20 - gs * 2,
                    self.width // 2 - 90 * gs + i * 20 * gs - 20 * gs + gs * 2, 24 + gs * 20 - gs * 2,
                )),
                ("t2f", (0, 0, 1, 0, 1, 1, 0, 1)),
                ("c4B", (255, 255, 255, 255) * 4)
            )
        glDisable(GL_TEXTURE_2D)
        self.reticle.draw(GL_LINES)
        if self.level == "escape_menu":
            self.escape_menu_shadow.draw(GL_QUADS)
        elif self.level == "loading_world":
            self.loading_shadow.draw(GL_QUADS)
        for key in self.buttons:
            origin_key = key
            key = key.split(".")
            key.pop()
            key = ".".join(key)
            if (self.level == key):
                self.buttons[origin_key].draw(self.mouse_position)
        err = glGetError()
        if err != GL_NO_ERROR:
            print(f"OpenGL Error: {err}")

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
                pyglet.graphics.draw(4, GL_LINE_LOOP, ("v3f", i))

    def draw_sky(self, player_sky_light):
        """ 绘制天空盒

        """
        self.skybox_shader.bind(u_player_sky_light=(player_sky_light, ))
        self.sky_box.draw(GL_QUADS)
        self.skybox_shader.unbind()

window = Window(width=800, height=600, caption="Minecraft", resizable=True)
glLineWidth(2.0)
# 启用Alpha混合
glEnable(GL_BLEND)
glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)
pyglet.app.run()
