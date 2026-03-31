#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <string>
#include <queue>
#include <list>
#include <format>
#include <algorithm>
#include <set>
#include <tuple>
#include <chrono>
#include <FastNoise/FastNoise.h>
using namespace std;
using ull = unsigned long long;

const map<string, string> emptyNBT;

int FACES[6][3] =
{
    0, 1, 0,
    0, -1, 0,
    -1, 0, 0,
    1, 0, 0,
    0, 0, 1,
    0, 0, -1,
};
int SECTOR_FACES[8][2] =
{
    1, 0,
    -1, 0,
    0, 1,
    0, -1,
    1, 1,
    1, -1,
    -1, 1,
    -1, -1,
};


struct Block
{
    unsigned short id;
    ull shown;
    bool has_NBT;
    map<string, string>* NBT;
    ~Block()
    {
        if (has_NBT)
            delete NBT;
    }
};

const string block_id[] =
{
    "block.minecraft.dev.sector_not_loaded",//0
    "block.minecraft.dev.air",//1
    "block.minecraft.nature.grass_block",//2
    "block.minecraft.nature.dirt",//3
    "block.minecraft.nature.stone",//4
    "block.minecraft.nature.bedrock",//5
    "block.minecraft.wood.oak_log",//6
    "block.minecraft.wood.oak_leaves",//7
};
const map<string, unsigned short> id_block =
{
    {"block.minecraft.dev.sector_not_loaded", 0},
    {"block.minecraft.dev.air", 1},
    {"block.minecraft.nature.grass_block", 2},
    {"block.minecraft.nature.dirt", 3},
    {"block.minecraft.nature.stone", 4},
    {"block.minecraft.nature.bedrock", 5},
    {"block.minecraft.wood.oak_log", 6},
    {"block.minecraft.wood.oak_leaves", 7},
};
unordered_set<int> transparent_blocks = {1};

Block air{1, 0, false, nullptr};
Block sector_not_loaded{0, 0, false, nullptr};

struct Sector_block_pos_hash
{
    size_t operator()(const tuple<int, int, int>& p) const
    {
        return ((get<0>(p) % 16 + 16) % 16 * 16 + (get<2>(p) % 16 + 16) % 16) * 128 + get<1>(p);
    }
};

struct Sector
{
    Block* blocks;
    int* data;
    Sector()
    {
        blocks = new Block[16 * 128 * 16];    // (x, y, z)为ret[x * 16 * 128 + y * 16 + z]
        data = new int[16 * 16];    // (x, z)为ret[x * 16 + z]
        fill(blocks, blocks + 16 * 128 * 16, air);
    }
    ~Sector()
    {
        delete[] blocks;
        delete[] data;
    }
};

struct Sector_pos
{
    int x, y;
    Sector_pos operator+(const Sector_pos &b) const
    {
        return {x + b.x, y + b.y};
    }
    bool operator==(const Sector_pos &b) const
    {
        return x == b.x && y == b.y;
    }
};

struct Sector_pos_hash
{
    size_t operator()(const Sector_pos& p) const
    {
        return (((long long)p.x) << 32) + p.y;
    }
};

struct Pos
{
    double x, y, z;
    Pos operator+(const Pos &b) const
    {
        return {x + b.x, y + b.y, z + b.z};
    }
};

struct entity_box
{
    double minx, maxx, miny, maxy, minz, maxz;
    // 自动生成比较运算符
    auto operator<=>(const entity_box&) const = default;
};

bool AABB(const entity_box &a, const entity_box &b, Pos da, Pos db)
{
    /* 如果没有碰撞返回False */
    if (a.maxx + da.x < b.minx + db.x || b.maxx + db.x < a.minx + da.x || a.maxy + da.y < b.miny + db.y || b.maxy + db.y < a.miny + da.y || a.maxz + da.z < b.minz + db.z || b.maxz + db.z < a.minz + da.z)
        return false;
    return true;
}

float landscape_calc(float n)
{
    if (n <= 0.3)
        return 1.5;
    else if (n > 0.3 && n <= 0.8)
        return n * 25 - 6;
    else
        return 14;
}

float hash2D(int x, int z, int seed) {
    unsigned int n = seed + x * 374761393 + z * 668265263;
    n = (n ^ (n >> 13)) * 1274126177;
    return (float)((n ^ (n >> 16)) & 0x7fffffff) / 0x7fffffff;
}

struct World
{
    unordered_map<Sector_pos, Sector*, Sector_pos_hash> world;
    int stoped_threads = 0;
    int simulate_distance;
    unordered_set<Sector_pos, Sector_pos_hash> simulate_sectors;
    unordered_set<Sector_pos, Sector_pos_hash> decorate_sectors;
    list<string> operations;    //双链表，可以O(1)连接两个链表，在generate_sector中使用
    map<string, set<entity_box>> block_entity_boxes;
    map<string, set<entity_box>> entity_entity_boxes;
    Pos position;
    unordered_set<Sector_pos, Sector_pos_hash> shown_sectors, generated_sectors, decorated_sectors;
    FastNoise::SmartNode<> noise;
    int seed;
    // 添加互斥锁，使用 recursive_mutex 允许同一个线程多次加锁
    mutable recursive_mutex world_mutex;    // world
    mutable recursive_mutex operations_mutex;    // operations
    mutable recursive_mutex position_mutex;    // position

    World(int seed_arg, int simulate_distance_arg)
    {
        noise = FastNoise::New<FastNoise::Simplex>();
        seed = seed_arg;
        simulate_distance = simulate_distance_arg;
        for (int x = -simulate_distance; x <= simulate_distance; ++x)
            for (int y = -simulate_distance; y <= simulate_distance; ++y)
                if (x * x + y * y <= simulate_distance * simulate_distance)
                    simulate_sectors.insert({x, y});
        --simulate_distance_arg;
        for (int x = -simulate_distance; x <= simulate_distance; ++x)
            for (int y = -simulate_distance; y <= simulate_distance; ++y)
            {
                bool flag = true;
                for (int i = 0, dx, dy; i < 8; ++i)
                {
                    dx = SECTOR_FACES[i][0], dy = SECTOR_FACES[i][1];
                    if (!simulate_sectors.count({x + dx, y + dy}))
                    {
                        flag = false;
                        break;
                    }
                }
                if (flag)
                    decorate_sectors.insert({x, y});
            }
    };

    ~World()
    {
        stop_all_thread();
        for (auto& pair : world) delete pair.second;
    }

    void stop_all_thread()
    {
        stoped_threads = 1;
        while (stoped_threads < 2)
            this_thread::sleep_for(chrono::milliseconds(200));
    }

    void set_position(double x, double y, double z)
    {
        lock_guard<recursive_mutex> lock_position(position_mutex);
        position = {x, y, z};
    }

    Block* find_block(int x, int y, int z) const
    {
        if (y < 0 || y >= 128)
            return &sector_not_loaded;
        int sector_x = (x >= 0) ? (x / 16) : ((x - 15) / 16);
        int sector_y = (z >= 0) ? (z / 16) : ((z - 15) / 16);
        x = (x % 16 + 16) % 16;
        z = (z % 16 + 16) % 16;
        if (world.count({sector_x, sector_y}) == 0)    //区块没有加载
            return &sector_not_loaded;
        return (world.at({sector_x, sector_y})->blocks) + ((x * 16 + z) * 128 + y);    // world[sector]不是const
    }

    ull exposed(int x, int y, int z) const
    {
        if (find_block(x, y, z)->id == 0 || find_block(x, y, z)->id == 1)
            return 0;
        ull ret = 0;
        for (int i = 0, dx, dy, dz; i < 6; ++i)
        {
            dx = FACES[i][0], dy = FACES[i][1], dz = FACES[i][2];
            if (y + dy < 0 || y + dy > 127)
                ret |= (1ull << i);
            // 同样的透明方块之间的面隐藏
            else if (transparent_blocks.count(find_block(x + dx, y + dy, z + dz)->id) && find_block(x + dx, y + dy, z + dz)->id != find_block(x, y, z)->id)
                ret |= (1ull << i);
        }
        return ret;
    }

    void add_operation(string op)
    {
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        operations.push_back(op);
    }

    void check_neighbors(int x, int y, int z)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        for (int i = 0, dx, dy, dz, nx, ny, nz; i < 6; ++i)
        {
            dx = FACES[i][0], dy = FACES[i][1], dz = FACES[i][2];
            nx = x + dx, ny = y + dy, nz = z + dz;
            set_shown(nx, ny, nz, exposed(nx, ny, nz));
        }
    }

    void add_block(int x, int y, int z, string sid, bool auto_process=true, bool has_NBT=false, const map<string, string> &NBT=emptyNBT)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        unsigned short id = id_block.at(sid);
        Block* block = find_block(x, y, z);
        if (block->id == 0 || block->id != 1)
            return;
        block->id = id;
        block->shown = 0;
        block->has_NBT = has_NBT;
        if (has_NBT)
            block->NBT = new map<string, string>(NBT);
        else
            block->NBT = nullptr;
        Pos position_local;
        {
            lock_guard<recursive_mutex> lock_position(position_mutex);
            position_local = position;
        }
        if (auto_process)
        {
            for (const entity_box &a : block_entity_boxes[block_id[id]])
                for (const entity_box &b : entity_entity_boxes["entity.minecraft.player"])
                    if (AABB(a, b, {(double)x, (double)y, (double)z}, position_local))
                    {
                        (*block) = air;
                        return;
                    }
            lock_guard<recursive_mutex> lock_operations(operations_mutex);
            set_shown(x, y, z, exposed(x, y, z));
            check_neighbors(x, y, z);
            operations.push_back(format("block_update {} {} {}", x, y, z));
        }
    }

    void remove_block(int x, int y, int z, bool auto_process=true)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        Block* block = find_block(x, y, z);
        if (block->id == 0 || block->id == 1)
            return;
        if (auto_process)
            set_shown(x, y, z, 0);
        if (block->has_NBT)
            delete block->NBT;
        (*block) = air;
        if (auto_process)
        {
            check_neighbors(x, y, z);
            operations.push_back(format("block_update {} {} {}", x, y, z));
        }
    }

    inline void set_shown(int x, int y, int z, ull shown)
    {
        // 不加锁，调用这个函数一定要在外面加锁
        if (find_block(x, y, z)->shown == shown)
            return;
        find_block(x, y, z)->shown = shown;
        if (shown == 0)
            operations.push_back(format("hide_block {} {} {}", x, y, z));
        else
            operations.push_back(format("update_shown {} {} {}", x, y, z));
    }

    void generate_sector(int dx, int dy)
    {
        Sector* sector = new Sector;
        float* noise_generate_height_map1 = new float[16 * 16];
        float* noise_generate_height_map2 = new float[16 * 16];
        float* noise_generate_height_map3 = new float[16 * 16];
        float* noise_generate_height_map4 = new float[16 * 16];
        int old_dx = dx, old_dy = dy;
        dx *= 16, dy *= 16;
        float h1, h2, tmp;
        int height1, height2;
        noise->GenUniformGrid2D(noise_generate_height_map1, dx * 0.3f, dy * 0.3f, 16, 16, 0.3f, 0.3f, seed);    // 群系
        noise->GenUniformGrid2D(noise_generate_height_map2, dx, dy, 16, 16, 1, 1, seed + 1);    // 大地形
        noise->GenUniformGrid2D(noise_generate_height_map3, dx * 5, dy * 5, 16, 16, 5, 5, seed + 2);    // 小地形
        noise->GenUniformGrid2D(noise_generate_height_map4, dx * 0.1f, dy * 0.1f, 16, 16, 0.1f, 0.1f, seed + 4);    // 整体趋势
        for (int x = 0; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
            {
                tmp = (noise_generate_height_map1[z * 16 + x] + 1) * 0.5;
                h1 = (noise_generate_height_map2[z * 16 + x] + 1) * landscape_calc(tmp) + 44 + tmp * 20 + noise_generate_height_map4[z * 16 + x] * 20;
                h2 = noise_generate_height_map3[z * 16 + x] * 0.5 + h1 + 5;
                height1 = h1, height2 = h2;
                sector->blocks[(x * 16 + z) * 128].id = 5;    //bedrock
                for (int y = 1; y < height1; ++y)
                    sector->blocks[(x * 16 + z) * 128 + y].id = 4;    //stone
                for (int y = height1; y < height2; ++y)
                    sector->blocks[(x * 16 + z) * 128 + y].id = 3;    //dirt
                sector->blocks[(x * 16 + z) * 128 + height2].id = 2;    //grass_block
                sector->data[x * 16 + z] = height2;
            }
        delete[] noise_generate_height_map1;
        delete[] noise_generate_height_map2;
        delete[] noise_generate_height_map3;
        delete[] noise_generate_height_map4;
        list<string> operations_local;    //可以先不加锁，最后统一加入operations
        ull exposed_flag;
        // 区块边缘的方块之后检查，现在检查会因为周边区块没加载出问题
        for (int x = 1; x != 15; ++x)
            for (int z = 1; z != 15; ++z)
                for (int y = 0; y != 128; ++y)
                {
                    if (sector->blocks[(x * 16 + z) * 128 + y].id == 1)
                        continue;
                    exposed_flag = 0;
                    for (int i = 0, edx, edy, edz; i < 6; ++i)
                    {
                        edx = FACES[i][0], edy = FACES[i][1], edz = FACES[i][2];
                        if (y + edy < 0 || y + edy > 127)
                            exposed_flag |= (1ull << i);
                        else if (transparent_blocks.count(sector->blocks[((x + edx) * 16 + z + edz) * 128 + y + edy].id) && sector->blocks[((x + edx) * 16 + z + edz) * 128 + y + edy].id != sector->blocks[(x * 16 + z) * 128 + y].id)
                            exposed_flag |= (1ull << i);
                    }
                    if (exposed_flag)
                    {
                        operations_local.push_back(format("update_shown {} {} {}", x + dx, y, z + dy));
                        sector->blocks[(x * 16 + z) * 128 + y].shown = exposed_flag;
                    }
                }
        lock_guard<recursive_mutex> lock_world(world_mutex);
        world[{old_dx, old_dy}] = sector;
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        operations.splice(operations.end(), operations_local);
        // 检查该区块边缘的方块
        for (int x = dx, z = dy; z != dy + 16; ++z)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        for (int x = dx + 15, z = dy; z != dy + 16; ++z)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        for (int x = dx + 1, z = dy; x != dx + 15; ++x)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        for (int x = dx + 1, z = dy + 15; x != dx + 15; ++x)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        // 检查周围区块的方块
        for (int x = dx - 1, z = dy - 1; z != dy + 17; ++z)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        for (int x = dx + 16, z = dy - 1; z != dy + 17; ++z)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        for (int x = dx, z = dy - 1; x != dx + 16; ++x)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
        for (int x = dx, z = dy + 16; x != dx + 16; ++x)
            for (int y = 0; y != 128; ++y)
                set_shown(x, y, z, exposed(x, y, z));
    }

    void decorate_sector(int sx, int sy)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        Sector* sector = world[{sx, sy}];
        Block* block;
        for (int x = 0, y; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
            {
                y = sector->data[x * 16 + z];
                if (hash2D(sx * 16 + x, sy * 16 + z, seed) > 0.99)
                {
                    for (int dy = 1; dy < 6; ++dy)
                    {
                        block = find_block(sx * 16 + x, y + dy, sy * 16 + z);
                        if (block->has_NBT)
                            delete block->NBT;
                        block->id = 6;
                        block->shown = 0;
                        block->has_NBT = false;
                        block->NBT = nullptr;
                    }
                    for (int dx = -2; dx <= 2; ++dx)
                        for (int dz = -2; dz <= 2; ++dz)
                            for (int dy = 3; dy <= 4; ++dy)
                            {
                                block = find_block(sx * 16 + x + dx, y + dy, sy * 16 + z + dz);
                                if (block->id != 1)
                                    continue;
                                block->id = 7;
                                block->shown = 0;
                                block->has_NBT = false;
                                block->NBT = nullptr;
                            }
                    for (int dx = -1, dy = 5; dx <= 1; ++dx)
                        for (int dz = -1; dz <= 1; ++dz)
                        {
                            block = find_block(sx * 16 + x + dx, y + dy, sy * 16 + z + dz);
                            if (block->id != 1)
                                continue;
                            block->id = 7;
                            block->shown = 0;
                            block->has_NBT = false;
                            block->NBT = nullptr;
                        }
                    for (int dx = -1, dy = 6; dx <= 1; ++dx)
                        for (int dz = -1; dz <= 1; ++dz)
                        {
                            if (abs(dx) + abs(dz) >= 2)
                                continue;
                            block = find_block(sx * 16 + x + dx, y + dy, sy * 16 + z + dz);
                            if (block->id != 1)
                                continue;
                            block->id = 7;
                            block->shown = 0;
                            block->has_NBT = false;
                            block->NBT = nullptr;
                        }
                    for (int dx = -5; dx <= 5; ++dx)
                        for (int dz = -5; dz <= 5; ++dz)
                            for (int dy = -1; dy <= 9; ++dy)
                                set_shown(sx * 16 + x + dx, y + dy, sy * 16 + z + dz, exposed(sx * 16 + x + dx, y + dy, sy * 16 + z + dz));
                }
            }
    }

    void show_sector(int dx, int dy)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        Sector* sector = world[{dx, dy}];
        dx *= 16, dy *= 16;
        list<string> operations_local;
        for (int x = 0; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
                for (int y = 0; y != 128; ++y)
                    if (sector->blocks[(x * 16 + z) * 128 + y].shown)
                        operations_local.push_back(format("update_shown {} {} {}", dx + x, y, dy + z));
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        operations.splice(operations.end(), operations_local);
    }

    void hide_sector(int dx, int dy)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        Sector* sector = world[{dx, dy}];
        dx *= 16, dy *= 16;
        list<string> operations_local;
        for (int x = 0; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
                for (int y = 0; y != 128; ++y)
                    if (sector->blocks[(x * 16 + z) * 128 + y].shown)
                        operations_local.push_back(format("hide_block {} {} {}", dx + x, y, dy + z));
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        operations.splice(operations.end(), operations_local);
    }

    void process_sector_thread()
    {
        unordered_set<Sector_pos, Sector_pos_hash> ought_shown, show, hide;
        list<tuple<string, int, int, int>> operations_local;
        while (!stoped_threads)
        {
            int x, z;
            {
                lock_guard<recursive_mutex> lock_position(position_mutex);
                x = position.x, z = position.z;
            }
            int nx = (x >= 0) ? (x / 16) : ((x - 15) / 16);
            int ny = (z >= 0) ? (z / 16) : ((z - 15) / 16);
            Sector_pos player_sector = {nx, ny};
            // 加载区块
            for (Sector_pos ds : simulate_sectors)
            {
                Sector_pos now = player_sector + ds;
                if (!generated_sectors.count(now))
                {
                    generated_sectors.insert(now);
                    shown_sectors.insert(now);
                    generate_sector(now.x, now.y);
                }
            }
            for (Sector_pos ds : decorate_sectors)
            {
                Sector_pos now = player_sector + ds;
                if (!decorated_sectors.count(now))
                {
                    decorated_sectors.insert(now);
                    decorate_sector(now.x, now.y);
                }
            }
            // 显示/隐藏区块
            ought_shown.clear(), show.clear(), hide.clear();
            operations_local.clear();
            for (Sector_pos ds : simulate_sectors)
                ought_shown.insert(player_sector + ds);
            for (Sector_pos i : ought_shown)
                if (shown_sectors.count(i) == 0)
                    show.insert(i);
            for (Sector_pos i : shown_sectors)
                if (ought_shown.count(i) == 0)
                    hide.insert(i);
            // 先隐藏再显示，python shown字典元素少，速度快
            for (Sector_pos i : hide)
            {
                shown_sectors.erase(i);
                hide_sector(i.x, i.y);
            }
            for (Sector_pos i : show)
            {
                shown_sectors.insert(i);
                show_sector(i.x, i.y);
            }
            this_thread::sleep_for(chrono::milliseconds(300));
        }
        ++stoped_threads;
    }

    void start_process_sector_thread()
    {
        thread t(&World::process_sector_thread, this);
        t.detach();
    }

    string give_operation()
    {
        lock_guard<recursive_mutex> lock(operations_mutex);
        if (operations.empty())
            return "None";
        string ret = operations.front();
        operations.pop_front();
        return ret;
    }

    int get_block(int x, int y, int z) const
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        return find_block(x, y, z)->id;
    }

    ull get_shown(int x, int y, int z) const
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        return find_block(x, y, z)->shown;
    }

    void add_transparent_block(string name)
    {
        transparent_blocks.insert(id_block.at(name));
    }

    void add_block_entity_box(string name, double minx, double maxx, double miny, double maxy, double minz, double maxz)
    {
        block_entity_boxes[name].insert({minx, maxx, miny, maxy, minz, maxz});
    }

    void add_entity_entity_box(string name, double minx, double maxx, double miny, double maxy, double minz, double maxz)
    {
        entity_entity_boxes[name].insert({minx, maxx, miny, maxy, minz, maxz});
    }

    bool intersect(string entity, double x, double y, double z)    // 如果与方块碰撞，返回true
    {
        int ix = x, iy = y, iz = z;
        int nx, ny, nz;
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 3; ++dy)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    nx = ix + dx, ny = iy + dy, nz = iz + dz;
                    if (ny >= 128 || ny < 0)
                        continue;
                    for (entity_box i : entity_entity_boxes[entity])
                        for (entity_box j : block_entity_boxes[block_id[find_block(nx, ny, nz)->id]])
                            if (AABB(i, j, {x, y, z}, {(double)nx, (double)ny, (double)nz}))
                                return true;
                }
        return false;
    }

    pybind11::tuple hit_test(double x, double y, double z, double dx, double dy, double dz, double max_distance)
    {
        int m = 16;    // 精度
        int px = x, py = y, pz = z;
        for (int i = 0, kx, ky, kz; i < max_distance * m; ++i)
        {
            kx = round(x), ky = round(y), kz = round(z);
            if (ky < 0 || ky >= 128)
            {
                x = x + dx / m, y = y + dy / m, z = z + dz / m;
                continue;
            }
            if ((kx != px || ky != py || kz != pz) && find_block(kx, ky, kz)->id != 1 && find_block(kx, ky, kz)->id != 0)
                return pybind11::make_tuple(pybind11::make_tuple(kx, ky, kz), pybind11::make_tuple(px, py, pz));
            px = kx, py = ky, pz = kz;
            x = x + dx / m, y = y + dy / m, z = z + dz / m;
        }
        return pybind11::make_tuple(pybind11::none(), pybind11::none());
    }
};

PYBIND11_MODULE(MCworld, m)
{
    using namespace pybind11::literals; // 引入 _a 字面量，让代码更可读
    pybind11::class_<World>(m, "World")
        .def(pybind11::init<int, int>())
        .def("start_process_sector_thread", &World::start_process_sector_thread, pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("give_operation", &World::give_operation)
        .def("get_block", &World::get_block)
        .def("get_shown", &World::get_shown)
        .def("add_block", &World::add_block,
             "x"_a, "y"_a, "z"_a, "sid"_a, "auto_process"_a=true, "has_NBT"_a=false, pybind11::arg("NBT") = emptyNBT)
        .def("remove_block", &World::remove_block,
             "x"_a, "y"_a, "z"_a, "auto_process"_a=true)
        .def("add_transparent_block", &World::add_transparent_block)
        .def("add_block_entity_box", &World::add_block_entity_box)
        .def("add_entity_entity_box", &World::add_entity_entity_box)
        .def("intersect", &World::intersect)
        .def("hit_test", &World::hit_test)
        .def("set_position", &World::set_position)
        .def("add_operation", &World::add_operation);
}
