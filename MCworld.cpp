#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <iostream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <vector>
#include <map>
#include <string>
#include <queue>
#include <list>
#include <bitset>
#include <format>
#include <algorithm>
#include <set>
#include <tuple>
#include <chrono>
#include <cmath>
#include <FastNoise/FastNoise.h>
using namespace std;
#define get_sector(x) ((x >= 0) ? (x / 16) : ((x - 15) / 16))
using ull = unsigned long long;

#ifdef _MSC_VER
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

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
// 法线：1 : (1, 0, 0)
//       2 : (-1, 0, 0)
//       3 : (0, 1, 0)
//       4 : (0, -1, 0)
//       5 : (0, 0, 1)
//       6 : (0, 0, -1)
//       0 : 不渲染
vector<vector<float>> cube_vertices(float x, float y, float z, float n)
{
    vector<vector<float>> ret(6);
    // 最后一个是法线
    ret[0] = vector<float>({x-n,y+n,z-n, x-n,y+n,z+n, x+n,y+n,z+n, x+n,y+n,z-n, 3.0f});    // top
    ret[1] = vector<float>({x-n,y-n,z-n, x+n,y-n,z-n, x+n,y-n,z+n, x-n,y-n,z+n, 4.0f});    // bottom
    ret[2] = vector<float>({x-n,y-n,z-n, x-n,y-n,z+n, x-n,y+n,z+n, x-n,y+n,z-n, 2.0f});    // left
    ret[3] = vector<float>({x+n,y-n,z+n, x+n,y-n,z-n, x+n,y+n,z-n, x+n,y+n,z+n, 1.0f});    // right
    ret[4] = vector<float>({x-n,y-n,z+n, x+n,y-n,z+n, x+n,y+n,z+n, x-n,y+n,z+n, 5.0f});    // front
    ret[5] = vector<float>({x+n,y-n,z-n, x-n,y-n,z-n, x-n,y+n,z-n, x+n,y+n,z-n, 6.0f});    // back
    return ret;
}

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
const unordered_map<string, unsigned short> id_block =
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
const unordered_map<unsigned short, const vector<unsigned short>> textures =
{
    {2, {0, 2, 1, 1, 1, 1}},
    {3, {2, 2, 2, 2, 2, 2}},
    {4, {4, 4, 4, 4, 4, 4}},
    {5, {3, 3, 3, 3, 3, 3}},
    {6, {5, 5, 6, 6, 6, 6}},
    {7, {7, 7, 7, 7, 7, 7}},
};
const float uvs[] = {0.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f};    // -1.0用于和顶点三个数据对齐
unordered_set<int> transparent_blocks = {1};

vector<float> get_tex_array_data(unsigned short id, int face_index, int x, int y, int z)
{
    int img_idx = textures.at(id)[face_index];
    vector<float> vertices = cube_vertices(x, y, z, 0.5)[face_index];
    vector<float> ret(7 * 4);
    for (unsigned int i = 0, j = 0; i != 4 * 7; i += 7, j += 3)
    {
        ret[i] = vertices[j];
        ret[i + 1] = vertices[j + 1];
        ret[i + 2] = vertices[j + 2];
        ret[i + 3] = uvs[j];
        ret[i + 4] = uvs[j + 1];
        ret[i + 5] = img_idx;
        ret[i + 6] = vertices[12];
    }
    return ret;
}

Block air{1, 0, false, nullptr};
Block sector_not_loaded{0, 0, false, nullptr};

struct Sector_block_pos_hash
{
    size_t operator()(const tuple<int, int, int>& p) const
    {
        return ((get<0>(p) % 16 + 16) % 16 * 16 + (get<2>(p) % 16 + 16) % 16) * 128 + get<1>(p);
    }
};

struct GL_QUADS_vbo_data
{
    unsigned int size;
    vector<unsigned int> unoccupied_ids;
    vector<float> data;
    GL_QUADS_vbo_data()
    {
        size = 0;
        data.reserve(4 * 7 * 256);
    }
    int add(vector<float> vertices)    // vertices长度为4 * 7表示一个面（一个顶点长度为7，三个坐标，三个纹理，最后一个是法线和渲染状态，压缩为一个GLfloat）
    // 法线：1 : (1, 0, 0)
    //       2 : (-1, 0, 0)
    //       3 : (0, 1, 0)
    //       4 : (0, -1, 0)
    //       5 : (0, 0, 1)
    //       6 : (0, 0, -1)
    //       0 : 不渲染
    {
        int id;
        if (unoccupied_ids.empty())
            id = size++;
        else
        {
            id = unoccupied_ids[unoccupied_ids.size() - 1];
            unoccupied_ids.pop_back();
        }
        data.resize(size * 4 * 7);
        copy(vertices.begin(), vertices.end(), data.begin() + id * 4 * 7);
        return id;
    }
    void erase(int id)
    {
        fill(data.begin() + id * 4 * 7, data.begin() + (id + 1) * 4 * 7, 0.0f);
        unoccupied_ids.push_back(id);
    }
};

struct Sector
{
    Block* blocks;
    int* data;
    unsigned short* biome;
    GL_QUADS_vbo_data vertex_data_struct;
    //                   方块坐标        之前的渲染状态  面的id
    unordered_map<tuple<int, int, int>, pair<ull, vector<int>>, Sector_block_pos_hash> shown;
    Sector()
    {
        blocks = new Block[16 * 128 * 16];    // (x, y, z)为blocks[x * 16 * 128 + y * 16 + z]
        data = new int[16 * 16];    // (x, z)为data[x * 16 + z]
        biome = new unsigned short[16 * 16];
        fill(blocks, blocks + 16 * 128 * 16, air);
    }
    ~Sector()
    {
        delete[] blocks;
        delete[] data;
        delete[] biome;
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

inline bool AABB(const entity_box &a, const entity_box &b, Pos da, Pos db)
{
    /* 如果没有碰撞返回False */
    if (a.maxx + da.x < b.minx + db.x || b.maxx + db.x < a.minx + da.x || a.maxy + da.y < b.miny + db.y || b.maxy + db.y < a.miny + da.y || a.maxz + da.z < b.minz + db.z || b.maxz + db.z < a.minz + da.z)
        return false;
    return true;
}

inline float terrain_fluctuate_calc(float n)
{
    if (n <= 0.3)
        return 1.5;
    else if (n > 0.3 && n <= 0.8)
        return n * 25 - 6;
    else
        return 14;
}

inline float hole_calc(int n)    // n是高度
{
    if (n < 10)
        return n * 0.1f;
    if (n <= 40)
        return 1.0f;
    if (n < 60)
        return 1.0f - (n - 40) * 0.05f;
    else
        return 0.0f;
}

inline float hash2D(int x, int z, int seed) {
    unsigned int n = (unsigned int)seed + (unsigned int)x * 374761393u + (unsigned int)z * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return (float)((n ^ (n >> 16)) & 0x7fffffff) / 0x7fffffff;
}

const string biome_id[] =
{
    "biome.minecraft.plain",
    "biome.minecraft.mountain",
    "biome.minecraft.forest",
    "biome.minecraft.highland",
};
// 参数范围 [-1, 1]
//                                  整体高度        地形起伏         植被（树）密度      雨量
inline unsigned int get_biome(float altitude, float fluctuate, float tree_density, float rain)
{
    if (-1 <= altitude && altitude <  0 && -1 <= fluctuate && fluctuate <  0 && -1 <= tree_density && tree_density <  0) return 0u;    // plain
    if (-1 <= altitude && altitude <  1 &&  0 <= fluctuate && fluctuate <= 1 && -1 <= tree_density && tree_density <= 1) return 1u;    // mountain
    if (-1 <= altitude && altitude <  0 && -1 <= fluctuate && fluctuate <  0 &&  0 <= tree_density && tree_density <= 1) return 2u;    // forest
    if ( 0 <= altitude && altitude <  1 && -1 <= fluctuate && fluctuate <  0 && -1 <= tree_density && tree_density <= 1) return 3u;    // highland
    return -1u;
}

struct LCG
{
    unsigned int now;
    LCG(unsigned int seed)
    {
        now = seed;
    }
    inline unsigned int next()
    {
        return (now = now * 1664525u + 1013904223u);
    }
};

inline vector<float> normalize(float x, float y, float z)
{
    float tmp = sqrt(x * x + y * y + z * z);
    return {x / tmp, y / tmp, z / tmp};
}

struct World
{
    unordered_map<Sector_pos, Sector*, Sector_pos_hash> world;
    int stoped_threads = 0;
    int simulate_distance;
    unordered_set<Sector_pos, Sector_pos_hash> simulate_sectors, decorate_sectors, generate_holes_sectors;
    list<string> operations;    //双链表，可以O(1)连接两个链表，在generate_sector中使用
    map<string, set<entity_box>> block_entity_boxes;
    map<string, set<entity_box>> entity_entity_boxes;
    Pos position;
    unordered_set<Sector_pos, Sector_pos_hash> shown_sectors, generated_sectors, decorated_sectors, generated_holes;
    unordered_map<Sector_pos, bitset<16 * 128 * 16>, Sector_pos_hash> holes;    // true即洞穴
    FastNoise::SmartNode<> noise;
    int seed;
    // 添加互斥锁，使用 recursive_mutex 允许同一个线程多次加锁
    mutable recursive_mutex world_mutex;    // world
    mutable recursive_mutex operations_mutex;    // operations
    mutable recursive_mutex position_mutex;    // position
    mutable recursive_mutex shown_mutex;    // shown

    World(int seed_arg, int simulate_distance_arg)
    {
        ++simulate_distance_arg;
        noise = FastNoise::New<FastNoise::Simplex>();
        seed = seed_arg;
        simulate_distance = simulate_distance_arg;
        for (int x = -simulate_distance; x <= simulate_distance; ++x)
            for (int y = -simulate_distance; y <= simulate_distance; ++y)
                if (x * x + y * y <= simulate_distance * simulate_distance)
                    simulate_sectors.insert({x, y});
        for (int x = -simulate_distance - 5; x <= simulate_distance + 5; ++x)
            for (int y = -simulate_distance - 5; y <= simulate_distance + 5; ++y)
                if (x * x + y * y <= (simulate_distance + 5) * (simulate_distance + 5))
                    generate_holes_sectors.insert({x, y});
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
        for (Sector_pos hole_pos : generate_holes_sectors)
        {
            generate_holes(hole_pos.x * 16 + hash2D(hole_pos.x, hole_pos.y, seed + 2) * 16, hash2D(hole_pos.x, hole_pos.y, seed) * 24, hole_pos.y * 16 + hash2D(hole_pos.x, hole_pos.y, seed + 1) * 16);
            generated_holes.insert(hole_pos);
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
            return 0ull;
        ull ret = 0ull;
        for (int i = 0, dx, dy, dz; i < 6; ++i)
        {
            dx = FACES[i][0], dy = FACES[i][1], dz = FACES[i][2];
            if (y + dy < 0 || y + dy > 127)
                ret |= (1ull << i);
            // 同样的透明方块之间的面一个隐藏，一个显示
            else if (transparent_blocks.count(find_block(x + dx, y + dy, z + dz)->id) && find_block(x + dx, y + dy, z + dz)->id != find_block(x, y, z)->id)
                ret |= (1ull << i);
            else if (transparent_blocks.count(find_block(x + dx, y + dy, z + dz)->id) && find_block(x + dx, y + dy, z + dz)->id == find_block(x, y, z)->id && \
                 (dx == 1 || dy == 1 || dz == 1)
            )
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
        lock_guard<recursive_mutex> lock_shown(shown_mutex);
        for (int i = 0, dx, dy, dz, nx, ny, nz; i < 6; ++i)
        {
            dx = FACES[i][0], dy = FACES[i][1], dz = FACES[i][2];
            nx = x + dx, ny = y + dy, nz = z + dz;
            set_shown(nx, ny, nz, exposed(nx, ny, nz));
        }
    }

    void update_shown(int x, int y, int z, ull shown)
    {
        int sector_x = (x >= 0) ? (x / 16) : ((x - 15) / 16);
        int sector_y = (z >= 0) ? (z / 16) : ((z - 15) / 16);
        Sector* sector = world[{sector_x, sector_y}];
        vector<vector<float>> vertex_data = cube_vertices((float)x, (float)y, (float)z, 0.5);
        ull mask;
        unsigned short id = find_block(x, y, z)->id;
        if (!sector->shown.count({(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}))
        {
            sector->shown[{(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}].first = 0;
            sector->shown[{(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}].second.resize(6);
        }
        for (unsigned short i = 0; i != 64; ++i)
        {
            mask = 1ull << i;
            if ((shown & mask) != (sector->shown[{(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}].first & mask))
            {
                if (shown & mask)
                {
                    vector<float> vertex_texture_data = get_tex_array_data(id, i, x, y, z);
                    sector->shown[{(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
                }
                else
                    sector->vertex_data_struct.erase(sector->shown[{(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}].second[i]);
            }
        }
        sector->shown[{(x % 16 + 16) % 16, y, (z % 16 + 16) % 16}].first = shown;
        if (!shown)
        {
            sector->shown.erase({(x % 16 + 16) % 16, y, (z % 16 + 16) % 16});
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
            for (const entity_box &a : block_entity_boxes[sid])
                for (const entity_box &b : entity_entity_boxes["entity.minecraft.player"])
                    if (AABB(a, b, {(double)x, (double)y, (double)z}, position_local))
                    {
                        if (block->has_NBT)
                            delete block->NBT;
                        (*block) = air;
                        return;
                    }
            lock_guard<recursive_mutex> lock_shown(shown_mutex);
            set_shown(x, y, z, exposed(x, y, z));
            check_neighbors(x, y, z);
            lock_guard<recursive_mutex> lock_operations(operations_mutex);
            operations.push_back(format("block_update {} {} {}", x, y, z));
            operations.push_back(format("update_vbo_data {} {}", get_sector(x), get_sector(z)));
            for (int i = 0, dx, dz, nx, nz; i < 6; ++i)
            {
                dx = FACES[i][0], dz = FACES[i][2];
                nx = x + dx, nz = z + dz;
                if (get_sector(nx) != get_sector(x) || get_sector(nz) != get_sector(z))
                    operations.push_back(format("update_vbo_data {} {}", get_sector(nx), get_sector(nz)));
            }
        }
    }

    void remove_block(int x, int y, int z, bool auto_process=true)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        lock_guard<recursive_mutex> lock_shown(shown_mutex);
        Block* block = find_block(x, y, z);
        if (block->id == 0 || block->id == 1)
            return;
        if (auto_process)
            set_shown(x, y, z, 0ull);
        if (block->has_NBT)
            delete block->NBT;
        (*block) = air;
        if (auto_process)
        {
            check_neighbors(x, y, z);
            lock_guard<recursive_mutex> lock_operations(operations_mutex);
            operations.push_back(format("block_update {} {} {}", x, y, z));
            operations.push_back(format("update_vbo_data {} {}", get_sector(x), get_sector(z)));
            for (int i = 0, dx, dz, nx, nz; i < 6; ++i)
            {
                dx = FACES[i][0], dz = FACES[i][2];
                nx = x + dx, nz = z + dz;
                if (get_sector(nx) != get_sector(x) || get_sector(nz) != get_sector(z))
                    operations.push_back(format("update_vbo_data {} {}", get_sector(nx), get_sector(nz)));
            }
        }
    }

    NOINLINE void set_shown(int x, int y, int z, ull shown)
    {
        // 不加锁，调用这个函数一定要在外面加锁(world_mutex, shown_mutex)
        if (find_block(x, y, z)->shown == shown)
            return;
        find_block(x, y, z)->shown = shown;
        update_shown(x, y, z, shown);
    }

    void lock_world_mutex()
    {
        world_mutex.lock();
    }

    void unlock_world_mutex()
    {
        world_mutex.unlock();
    }

    void generate_sector(int dx, int dy)
    {
        Sector* sector = new Sector;
        bitset<16 * 128 * 16> hole = holes[{dx, dy}];
        holes.erase({dx, dy});
        //                                     整体高度        地形起伏         植被（树）密度      雨量
        // inline unsigned int get_biome(float altitude, float fluctuate, float tree_density, float rain)
        float* noise_altitude = new float[16 * 16];
        float* noise_fluctuate = new float[16 * 16];
        float* noise_terrain = new float[16 * 16];
        float* noise_tree_density = new float[16 * 16];
        float* noise_rain = new float[16 * 16];
        /*float* cave_noise = new float[5 * 33 * 5];
        float* cave_noise_mix = new float[16 * 128 * 16];    // 差值后的洞穴噪声*/
        int old_dx = dx, old_dy = dy;
        dx *= 16, dy *= 16;
        //float tmp;
        int height1, height2;
        noise->GenUniformGrid2D(noise_altitude, dx * 0.05f, dy * 0.05f, 16, 16, 0.05f, 0.05f, seed);
        noise->GenUniformGrid2D(noise_fluctuate, dx * 0.3f, dy * 0.3f, 16, 16, 0.3f, 0.3f, seed + 1);
        noise->GenUniformGrid2D(noise_terrain, dx, dy, 16, 16, 1, 1, seed + 1);
        noise->GenUniformGrid2D(noise_tree_density, dx * 0.5f, dy * 0.5f, 16, 16, 0.5f, 0.5f, seed + 2);
        noise->GenUniformGrid2D(noise_rain, dx * 0.1f, dy * 0.1f, 16, 16, 0.1f, 0.1f, seed + 3);
        //noise->GenUniformGrid3D(cave_noise, dx / 16 * 5 * 4, seed / 1048576, dy / 16 * 5 * 4, 5, 33, 5, 5.0f, 20.0f, 5.0f, seed);    // 洞穴的3D噪声，后期用线性差值处理
        /*float n000, n001, n010, n011, n100, n101, n110, n111, u, v, w;
        for (int x = 0, nx, ny, nz; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
                for (int y = 0; y < 128; ++y)
                {
                    nx = x / 4, ny = y / 4, nz = z / 4;
                    n000 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4, ny = y / 4, nz = z / 4 + 1;
                    n001 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4, ny = y / 4 + 1, nz = z / 4;
                    n010 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4, ny = y / 4 + 1, nz = z / 4 + 1;
                    n011 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4 + 1, ny = y / 4, nz = z / 4;
                    n100 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4 + 1, ny = y / 4, nz = z / 4 + 1;
                    n101 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4 + 1, ny = y / 4 + 1, nz = z / 4;
                    n110 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    nx = x / 4 + 1, ny = y / 4 + 1, nz = z / 4 + 1;
                    n111 = cave_noise[(nz * 33 + ny) * 5 + nx];
                    tmp = 0;
                    u = (x % 4) / 4.0f;
                    v = (y % 4) / 4.0f;
                    w = (z % 4) / 4.0f;
                    tmp += n000 * (1 - u) * (1 - v) * (1 - w);
                    tmp += n001 * (1 - u) * (1 - v) * (w);
                    tmp += n010 * (1 - u) * (v) * (1 - w);
                    tmp += n011 * (1 - u) * (v) * (w);
                    tmp += n100 * (u) * (1 - v) * (1 - w);
                    tmp += n101 * (u) * (1 - v) * (w);
                    tmp += n110 * (u) * (v) * (1 - w);
                    tmp += n111 * (u) * (v) * (w);
                    cave_noise_mix[(x * 16 + z) * 128 + y] = tmp * hole_calc(y);
                }*/
        for (int x = 0; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
            {
                height1 = noise_altitude[z * 16 + x] * 30 + 40 + (noise_terrain[z * 16 + x] + 1) * terrain_fluctuate_calc(noise_fluctuate[z * 16 + x] * 0.5f + 0.5f);
                height2 = height1 + 5;
                sector->blocks[(x * 16 + z) * 128].id = 5;    //bedrock
                for (int y = 1; y < height1; ++y)
                    if (!hole[(x * 16 + z) * 128 + y])
                        sector->blocks[(x * 16 + z) * 128 + y].id = 4;    //stone
                for (int y = height1; y < height2; ++y)
                    if (!hole[(x * 16 + z) * 128 + y])
                        sector->blocks[(x * 16 + z) * 128 + y].id = 3;    //dirt
                if (!hole[(x * 16 + z) * 128 + height2])
                    sector->blocks[(x * 16 + z) * 128 + height2].id = 2;    //grass_block
                sector->data[x * 16 + z] = height2;
                sector->biome[x * 16 + z] = get_biome(noise_altitude[z * 16 + x], noise_fluctuate[z * 16 + x], noise_tree_density[z * 16 + x], noise_rain[z * 16 + x]);
            }
        delete[] noise_altitude;
        delete[] noise_fluctuate;
        delete[] noise_terrain;
        delete[] noise_tree_density;
        delete[] noise_rain;
        /*delete[] cave_noise;
        delete[] cave_noise_mix;*/
        lock_guard<recursive_mutex> lock_world(world_mutex);
        world[{old_dx, old_dy}] = sector;
        ull exp;
        // 检查显示的方块，周边区块边缘的也要检查
        lock_guard<recursive_mutex> lock_shown(shown_mutex);
        for (int x = -1; x != 17; ++x)
            for (int z = -1; z != 17; ++z)
                for (int y = 0; y != 128; ++y)
                    if ((exp = exposed(dx + x, y, dy + z)) != 0ull)
                        set_shown(dx + x, y, dy + z, exp);
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        operations.push_back(format("update_vbo_data {} {}", old_dx, old_dy));
        if (world.count({old_dx + 1, old_dy}))
            operations.push_back(format("update_vbo_data {} {}", old_dx + 1, old_dy));
        if (world.count({old_dx, old_dy + 1}))
            operations.push_back(format("update_vbo_data {} {}", old_dx, old_dy + 1));
        if (world.count({old_dx - 1, old_dy}))
            operations.push_back(format("update_vbo_data {} {}", old_dx - 1, old_dy));
        if (world.count({old_dx, old_dy - 1}))
            operations.push_back(format("update_vbo_data {} {}", old_dx, old_dy - 1));
    }

    bool has_tree(int x, int z, float tree_calc_value)
    {
        float hash = hash2D(x, z, seed);
        if (hash <= 1.0f - tree_calc_value)
            return false;
        for (int dx = -3; dx <= 3; ++dx)
            for (int dz = -3; dz <= 3; ++dz)
            {
                if (dx == 0 && dz == 0)
                    continue;
                if (dx * dx + dz * dz <= 9 && hash2D(x + dx, z + dz, seed) >= hash)
                    return false;
            }
        return true;
    }

    void decorate_sector(int sx, int sy)
    {
        lock_guard<recursive_mutex> lock_world(world_mutex);
        lock_guard<recursive_mutex> lock_shown(shown_mutex);
        Sector* sector = world[{sx, sy}];
        Block* block;
        unsigned int tmp;
        float tree_density;
        for (int x = 0, y; x != 16; ++x)
            for (int z = 0; z != 16; ++z)
            {
                y = sector->data[x * 16 + z];
                tmp = sector->biome[x * 16 + z];
                switch(tmp)
                {
                    case 0u:    // plain
                        tree_density = 0.0015f;
                        break;
                    case 1u:    // mountain
                        tree_density = 0.005f;
                        break;
                    case 2u:    // forest
                        tree_density = 0.03f;
                        break;
                    case 3u:    // highland
                        tree_density = 0.0005f;
                        break;
                    default:
                        tree_density = 0;
                }
                if (has_tree(sx * 16 + x, sy * 16 + z, tree_density) && sector->blocks[(x * 16 + z) * 128 + y].id != 1)
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
        lock_guard<recursive_mutex> lock_operations(operations_mutex);
        operations.push_back(format("update_vbo_data {} {}", sx, sy));
        operations.push_back(format("update_vbo_data {} {}", sx + 1, sy));
        operations.push_back(format("update_vbo_data {} {}", sx, sy + 1));
        operations.push_back(format("update_vbo_data {} {}", sx - 1, sy));
        operations.push_back(format("update_vbo_data {} {}", sx, sy - 1));
    }

    // 洞穴最大长度：64, 每次以玩家为圆心，simulate_distance + 5为半径，在圆上每个区块（随机高度）生成洞穴。
    void generate_holes(float x, float y, float z)
    {
        //LCG rng((unsigned int)((hash2D(x, y) + hash2D(y, z) + hash2D(z, x)) * 1431655765));    // (2 ^ 32 - 1) / 3
        float dx[64], dy[64], dz[64], size[64];
        int length = (hash2D(x, y, seed) + hash2D(y, z, seed) + hash2D(z, x, seed)) * 64 / 6 + 32;    // [32, 64]
        noise->GenUniformGrid2D(dx, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 3.0f, seed);
        noise->GenUniformGrid2D(dy, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 2.0f, seed + 1);
        noise->GenUniformGrid2D(dz, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 3.0f, seed + 2);
        noise->GenUniformGrid2D(size, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 2.0f, seed + 3);
        vector<float> tmp;
        for (int i = 0, size_int, x_int, y_int, z_int, final_x, final_y, final_z, sector_x, sector_y; i < length; ++i)
        {
            tmp = normalize(dx[i], dy[i] * 0.5, dz[i]);
            x += tmp[0], y += tmp[1], z += tmp[2];
            x_int = x, y_int = y, z_int = z;
            size_int = (int)(size[i] + 3);
            for (int bx = -size_int; bx <= size_int; ++bx)
                for (int by = -size_int; by <= size_int; ++by)
                    for (int bz = -size_int; bz <= size_int; ++bz)
                        if (bx * bx + by * by + bz * bz <= size_int * size_int)
                        {
                            final_x = x_int + bx, final_y = y_int + by, final_z = z_int + bz;
                            if (final_y <= 0 || final_y >= 128)
                                break;
                            sector_x = (final_x >= 0) ? (final_x / 16) : ((final_x - 15) / 16);
                            sector_y = (final_z >= 0) ? (final_z / 16) : ((final_z - 15) / 16);
                            final_x = (final_x % 16 + 16) % 16;
                            final_z = (final_z % 16 + 16) % 16;
                            holes[{sector_x, sector_y}][(final_x * 16 + final_z) * 128 + final_y] = true;
                        }
        }
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
            Sector_pos player_sector = {nx, ny}, now;
            // 生成洞穴
            for (Sector_pos ds : generate_holes_sectors)
            {
                now = player_sector + ds;
                if (!generated_holes.count(now))
                {
                    generated_holes.insert(now);
                    generate_holes(now.x * 16 + hash2D(now.x, now.y, seed + 2) * 16, hash2D(now.x, now.y, seed) * 24, now.y * 16 + hash2D(now.x, now.y, seed + 1) * 16);
                }
            }
            // 生成区块
            for (Sector_pos ds : simulate_sectors)
            {
                now = player_sector + ds;
                if (!generated_sectors.count(now))
                {
                    generated_sectors.insert(now);
                    shown_sectors.insert(now);
                    generate_sector(now.x, now.y);
                }
            }
            // 装饰区块
            for (Sector_pos ds : decorate_sectors)
            {
                now = player_sector + ds;
                if (!decorated_sectors.count(now))
                {
                    decorated_sectors.insert(now);
                    decorate_sector(now.x, now.y);
                }
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

    void set_block_entity_box_to_none(string name)
    {
        block_entity_boxes[name] = set<entity_box>();
    }

    void add_entity_entity_box(string name, double minx, double maxx, double miny, double maxy, double minz, double maxz)
    {
        entity_entity_boxes[name].insert({minx, maxx, miny, maxy, minz, maxz});
    }

    bool intersect(string entity, double x, double y, double z) const    // 如果与方块碰撞，返回true
    {
        int ix = x, iy = y, iz = z;
        int nx, ny, nz;
        lock_guard<recursive_mutex> lock_world(world_mutex);
        for (int dx = -2; dx <= 2; ++dx)
            for (int dy = -2; dy <= 3; ++dy)
                for (int dz = -2; dz <= 2; ++dz)
                {
                    nx = ix + dx, ny = iy + dy, nz = iz + dz;
                    if (ny >= 128 || ny < 0)
                        continue;
                    for (entity_box i : entity_entity_boxes.at(entity))
                        for (entity_box j : block_entity_boxes.at(block_id[find_block(nx, ny, nz)->id]))
                            if (AABB(i, j, {x, y, z}, {(double)nx, (double)ny, (double)nz}))
                                return true;
                }
        return false;
    }

    pybind11::tuple hit_test(double x, double y, double z, double dx, double dy, double dz, double max_distance) const
    {
        int m = 16;    // 精度
        int px = x, py = y, pz = z;
        lock_guard<recursive_mutex> lock_world(world_mutex);
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

    pybind11::tuple get_sector_vbo_data_ptr(int x, int y) const
    {
        // 第一个是指针，第二个是长度
        return pybind11::make_tuple(reinterpret_cast<uintptr_t>(world.at({x, y})->vertex_data_struct.data.data()), world.at({x, y})->vertex_data_struct.data.size());
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
        .def("set_block_entity_box_to_none", &World::set_block_entity_box_to_none)
        .def("add_entity_entity_box", &World::add_entity_entity_box)
        .def("intersect", &World::intersect)
        .def("hit_test", &World::hit_test)
        .def("set_position", &World::set_position)
        .def("add_operation", &World::add_operation)
        .def("get_sector_vbo_data_ptr", &World::get_sector_vbo_data_ptr)
        .def("generate_sector", &World::generate_sector)
        .def("lock_world_mutex", &World::lock_world_mutex)
        .def("unlock_world_mutex", &World::unlock_world_mutex);
}
