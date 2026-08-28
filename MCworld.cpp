#define FASTNOISE_STATIC_LIB
#include <FastNoise/FastNoise.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <bitset>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <list>
#include <map>
#include <mutex>
#include <queue>
#include <set>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

struct Sector_pos {
    int x, y;
    inline Sector_pos operator+(const Sector_pos &b) const {
        return {x + b.x, y + b.y};
    }
    inline bool operator==(const Sector_pos &b) const {
        return x == b.x && y == b.y;
    }
    inline bool operator<(const Sector_pos &b) const {
        return x != b.x ? x < b.x : y < b.y;
    }
};

struct Sector_pos_hash {
    inline size_t operator()(const Sector_pos &p) const {
        return (((size_t)p.x) << 32) + p.y;
    }
};

struct Block_pos {
    int x, y, z;
    inline Block_pos operator+(const Block_pos &b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    inline bool operator==(const Block_pos &b) const {
        return x == b.x && y == b.y && z == b.z;
    }
};

inline int mod16(const int &x) {
    return x & 15;
}

inline Sector_pos get_sector(int x, int y) {
    return {x >> 4, y >> 4};
}
inline int get_block_index(int x, int y, int z) {
    return (mod16(x) * 16 + mod16(z)) * 256 + y;
}

inline Sector_pos get_sector(const Block_pos &p) {
    return get_sector(p.x, p.z);
}
inline int get_block_index(const Block_pos &p) {
    return get_block_index(p.x, p.y, p.z);
}

struct Block_pos_hash {
    size_t operator()(const Block_pos &p) const {
        return get_block_index(p);
    }
};

struct Pos {
    double x, y, z;
    Pos operator+(const Pos &b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
};

const std::map<std::string, std::string> emptyNBT;

constexpr Block_pos FACES[6] = {
    {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, -1},
};
constexpr Sector_pos SECTOR_FACES[8] = {
    {1, 0}, {-1, 0}, {0, 1}, {0, -1}, {1, 1}, {1, -1}, {-1, 1}, {-1, -1},
};

// 法线：1 : ( 1,  0,  0)
//       2 : (-1,  0,  0)
//       3 : ( 0,  1,  0)
//       4 : ( 0, -1,  0)
//       5 : ( 0,  0,  1)
//       6 : ( 0,  0, -1)
//       0 : 不渲染
inline void cube_vertices(float x, float y, float z, float n, std::array<std::array<float, 13>, 6> &res) {
    // 最后一个是法线
    res[0] = {x - n, y + n, z - n, x - n, y + n, z + n, x + n, y + n, z + n, x + n, y + n, z - n, 3.0f};  // top
    res[1] = {x - n, y - n, z - n, x + n, y - n, z - n, x + n, y - n, z + n, x - n, y - n, z + n, 4.0f};  // bottom
    res[2] = {x - n, y - n, z - n, x - n, y - n, z + n, x - n, y + n, z + n, x - n, y + n, z - n, 2.0f};  // left
    res[3] = {x + n, y - n, z + n, x + n, y - n, z - n, x + n, y + n, z - n, x + n, y + n, z + n, 1.0f};  // right
    res[4] = {x - n, y - n, z + n, x + n, y - n, z + n, x + n, y + n, z + n, x - n, y + n, z + n, 5.0f};  // front
    res[5] = {x + n, y - n, z - n, x - n, y - n, z - n, x - n, y + n, z - n, x + n, y + n, z - n, 6.0f};  // back
}

struct Block {
    uint16_t id;
    uint8_t light;  // 高4位储存天空光照，低4位储存方块光照
};

const std::string block_id[] = {
    "block.minecraft.sector_not_loaded",  // 0
    "block.minecraft.air",                // 1
    "block.minecraft.grass_block",        // 2
    "block.minecraft.dirt",               // 3
    "block.minecraft.stone",              // 4
    "block.minecraft.bedrock",            // 5
    "block.minecraft.oak_log",            // 6
    "block.minecraft.oak_leaves",         // 7
    "block.minecraft.glowstone",          // 8
};
const std::unordered_map<std::string, uint16_t> id_block = {
    {"block.minecraft.sector_not_loaded", 0}, {"block.minecraft.air", 1},     {"block.minecraft.grass_block", 2}, {"block.minecraft.dirt", 3},      {"block.minecraft.stone", 4},
    {"block.minecraft.bedrock", 5},           {"block.minecraft.oak_log", 6}, {"block.minecraft.oak_leaves", 7},  {"block.minecraft.glowstone", 8},
};

constexpr uint8_t light_attenuation[] = {
    1, 1, 15, 15, 15, 15, 15, 3, 15,
};
constexpr uint8_t block_light[] = {
    0, 0, 0, 0, 0, 0, 0, 0, 15,
};

constexpr Block air{1, (0 << 4) + 0};
Block sector_not_loaded{0, (0 << 4) + 0};

class GL_QUADS_vbo_data {
private:
    uint64_t _size;
    std::vector<uint64_t> unoccupied_ids;
    std::vector<float> data;

public:
    GL_QUADS_vbo_data() {
        _size = 0;
        data.reserve(4 * 7 * 256);
    }
    uint64_t add(const std::array<float, 4 * 7> &vertices) {
        // vertices长度为4 *
        // 7表示一个面（一个顶点长度为7，三个坐标，三个纹理，最后一个是法线([1,
        // 6])和渲染状态({0, 1})和亮度([0, 15])，压缩为一个GLfloat）
        // 法线：1 : ( 1,  0,  0)
        //       2 : (-1,  0,  0)
        //       3 : ( 0,  1,  0)
        //       4 : ( 0, -1,  0)
        //       5 : ( 0,  0,  1)
        //       6 : ( 0,  0, -1)
        //       0 : 不渲染
        // 亮度 * 7 + 法线
        uint64_t id;
        if (unoccupied_ids.empty()) {
            id = _size++;
        } else {
            id = unoccupied_ids[unoccupied_ids.size() - 1];
            unoccupied_ids.pop_back();
        }
        data.resize(_size * 4 * 7);
        std::copy(vertices.begin(), vertices.end(), data.begin() + id * 4 * 7);
        return id;
    }
    inline void erase(uint32_t id) {
        fill(data.begin() + id * 4 * 7, data.begin() + (id + 1) * 4 * 7, 0.0f);
        unoccupied_ids.push_back(id);
    }
    inline void change_data(uint32_t id, const std::array<float, 4 * 7> &vertices) {
        copy(vertices.begin(), vertices.end(), data.begin() + id * 4 * 7);
    }
    inline float *get_data_ptr(uint32_t id) {
        return data.data() + id * 4 * 7;
    }
    inline size_t size() const {
        return data.size();
    }
};

struct Sector {
    std::array<Block, 16 * 256 * 16> blocks;
    std::unordered_map<Block_pos, std::map<std::string, std::string>, Block_pos_hash> NBTs;
    std::array<int, 16 * 16> data;
    std::array<int, 16 * 16> biome;
    mutable std::mutex vertex_data_struct_mutex;
    GL_QUADS_vbo_data vertex_data_struct;
    // 区块内方块坐标的index值(get_block_index)  之前的渲染状态
    // vector:面的id为索引，内容是VBO内的ID
    std::array<std::pair<uint64_t, std::vector<uint32_t>>, 16 * 256 * 16> shown;
};

constexpr std::array<uint16_t, 6> textures[] = {{0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0}, {0, 2, 1, 1, 1, 1}, {2, 2, 2, 2, 2, 2}, {4, 4, 4, 4, 4, 4},
                                                {3, 3, 3, 3, 3, 3}, {5, 5, 6, 6, 6, 6}, {7, 7, 7, 7, 7, 7}, {8, 8, 8, 8, 8, 8}};
constexpr float uvs[] = {0.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f};  // -1.0用于和顶点三个数据对齐
constexpr bool transparent_blocks[] = {false, true, false, false, false, false, false, true, false};

struct entity_box {
    double minx, maxx, miny, maxy, minz, maxz;
    auto operator<=>(const entity_box &) const = default;
};

const std::set<entity_box> entity_entity_boxes[] = {
    std::set<entity_box>({entity_box{-0.3, 0.3, -0.5, 1.3, -0.3, 0.3}}),
};
const std::set<entity_box> block_entity_boxes[] = {
    std::set<entity_box>(),
    std::set<entity_box>(),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
    std::set<entity_box>({entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}),
};

constexpr Block_pos normals[7] = {
    {0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1},
};

inline bool AABB(const entity_box &a, const entity_box &b, Pos da, Pos db) {
    /* 如果没有碰撞返回False */
    return !(a.maxx + da.x < b.minx + db.x || b.maxx + db.x < a.minx + da.x || a.maxy + da.y < b.miny + db.y || b.maxy + db.y < a.miny + da.y || a.maxz + da.z < b.minz + db.z ||
             b.maxz + db.z < a.minz + da.z);
}

inline float terrain_fluctuate_calc(float n) {
    if (n <= 0.3) {
        return 1.5;
    } else if (n > 0.3 && n <= 0.8) {
        return n * 25 - 6;
    } else {
        return 14;
    }
}

// n是高度
inline float hole_calc(int n) {
    if (n < 10) {
        return n * 0.1f;
    }
    if (n <= 40) {
        return 1.0f;
    }
    if (n < 60) {
        return 1.0f - (n - 40) * 0.05f;
    } else {
        return 0.0f;
    }
}

inline float hash2D(int x, int z, int seed) {
    uint64_t n = (uint64_t)seed + (uint64_t)x * 374761393u + (uint64_t)z * 668265263u;
    n = (n ^ (n >> 13)) * 1274126177u;
    return ((n ^ (n >> 16)) & 0x7fffffff) / (float)(0x7fffffff);
}

const std::string biome_id[] = {
    "biome.minecraft.plain",
    "biome.minecraft.mountain",
    "biome.minecraft.forest",
    "biome.minecraft.highland",
};
// 参数范围 [-1, 1]
//                         整体高度        地形起伏         植被（树）密度 雨量
inline int get_biome(float altitude, float fluctuate, float tree_density, float rain) {
    if (-1 <= altitude && altitude < 0 && -1 <= fluctuate && fluctuate < 0 && -1 <= tree_density && tree_density < 0) {
        return 0;  // plain
    }
    if (-1 <= altitude && altitude < 1 && 0 <= fluctuate && fluctuate <= 1 && -1 <= tree_density && tree_density <= 1) {
        return 1;  // mountain
    }
    if (-1 <= altitude && altitude < 0 && -1 <= fluctuate && fluctuate < 0 && 0 <= tree_density && tree_density <= 1) {
        return 2;  // forest
    }
    if (0 <= altitude && altitude < 1 && -1 <= fluctuate && fluctuate < 0 && -1 <= tree_density && tree_density <= 1) {
        return 3;  // highland
    }
    return -1;
}

inline std::tuple<float, float, float> normalize(float x, float y, float z) {
    float tmp = sqrt(x * x + y * y + z * z);
    return {x / tmp, y / tmp, z / tmp};
}

class World {
private:
    std::unordered_map<Sector_pos, Sector *, Sector_pos_hash> world;
    std::unordered_set<Sector_pos, Sector_pos_hash> full_light_calc_flag;
    int simulate_distance;
    std::unordered_set<Sector_pos, Sector_pos_hash> simulate_sectors, decorate_sectors, generate_holes_sectors;
    std::list<std::string> operations;  // 双链表，可以O(1)连接两个链表，在generate_sector中使用
    Pos position;
    std::unordered_set<Sector_pos, Sector_pos_hash> shown_sectors, generated_sectors, decorated_sectors, generated_holes, calced_light_sectors;
    std::unordered_map<Sector_pos, std::bitset<16 * 256 * 16>, Sector_pos_hash> holes;  // true即洞穴
    FastNoise::SmartNode<> noise;
    FastNoise::SmartNode<> terrain_noise_generater;
    int seed;

    // 添加互斥锁，使用 recursive_mutex 允许同一个线程多次加锁
    mutable std::recursive_mutex world_mutex;       // world
    mutable std::recursive_mutex operations_mutex;  // operations
    mutable std::recursive_mutex position_mutex;    // position
    // 对于多线程数据竞争的问题，现在除了主线程，另外有一个 process_sector_thread
    // 的线程，这个线程处理的都是边缘的区块。 主线程只有python调用 add_block,
    // remove_block, get_block, hit_test, intersect 会触发对 world
    // 的访问，且只会与玩家所在的区块相关。
    // unordered_map.at操作是线程安全的，所以除了 generate_sector 中 operator[],
    // 其他process_sector_thread 中调用的函数不需要加锁。

    int stoped_threads = 0;

    void stop_all_thread() {
        stoped_threads = 1;
        while (stoped_threads < 2) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

    Block *find_block(int x, int y, int z) const {
        if (y < 0 || y >= 256) {
            return &sector_not_loaded;
        }
        Sector_pos sector = get_sector(x, z);
        if (world.count(sector) == 0) {  // 区块没有加载
            return &sector_not_loaded;
        }
        return &(world.at(sector)->blocks[get_block_index(x, y, z)]);  // world[sector]不是const
    }

    inline Block *find_block(const Block_pos &p) const {
        return find_block(p.x, p.y, p.z);
    }

    void process_sector_thread() {
        Sector_pos player_sector, now;
        while (!stoped_threads) {
            int x, z;
            {
                std::lock_guard<std::recursive_mutex> lock_position(position_mutex);
                x = position.x, z = position.z;
            }
            player_sector = get_sector(x, z);
            // 生成洞穴
            for (Sector_pos ds : generate_holes_sectors) {
                now = player_sector + ds;
                if (!generated_holes.count(now)) {
                    generated_holes.insert(now);
                    generate_holes(now.x * 16 + hash2D(now.x, now.y, seed + 2) * 16, hash2D(now.x, now.y, seed) * 24 + 16, now.y * 16 + hash2D(now.x, now.y, seed + 1) * 16);
                    generate_holes(now.x * 16 + hash2D(now.x, now.y, seed + 2) * 16, hash2D(now.x, now.y, seed) * 24 + 48, now.y * 16 + hash2D(now.x, now.y, seed + 1) * 16);
                }
            }
            // 生成区块
            for (Sector_pos ds : simulate_sectors) {
                now = player_sector + ds;
                if (!generated_sectors.count(now)) {
                    generated_sectors.insert(now);
                    shown_sectors.insert(now);
                    generate_sector(now.x, now.y);
                    calc_sector_light(now.x, now.y);
                    check_exposed_blocks(now.x, now.y);
                }
            }
            // 装饰区块
            for (Sector_pos ds : decorate_sectors) {
                now = player_sector + ds;
                if (!decorated_sectors.count(now)) {
                    decorated_sectors.insert(now);
                    generate_tree(now.x, now.y);
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        ++stoped_threads;
    }

    void init_sectors() {
        int cnt;
        // 生成洞穴
        cnt = 0;
        for (Sector_pos now : generate_holes_sectors) {
            generated_holes.insert(now);
            generate_holes(now.x * 16 + hash2D(now.x, now.y, seed + 2) * 16, hash2D(now.x, now.y, seed) * 24 + 16, now.y * 16 + hash2D(now.x, now.y, seed + 1) * 16);
            generate_holes(now.x * 16 + hash2D(now.x, now.y, seed + 2) * 16, hash2D(now.x, now.y, seed) * 24 + 48, now.y * 16 + hash2D(now.x, now.y, seed + 1) * 16);
            ++cnt;
            {
                std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
                operations.push_back(std::format("set_schedule hole {} {}", cnt, generate_holes_sectors.size()));
            }
        }
        // 生成区块
        cnt = 0;
        for (Sector_pos now : simulate_sectors) {
            generated_sectors.insert(now);
            shown_sectors.insert(now);
            generate_sector(now.x, now.y);
            calc_sector_light(now.x, now.y);
            check_exposed_blocks(now.x, now.y);
            ++cnt;
            {
                std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
                operations.push_back(std::format("set_schedule sector {} {}", cnt, simulate_sectors.size()));
            }
        }
        // 装饰区块
        cnt = 0;
        for (Sector_pos now : decorate_sectors) {
            decorated_sectors.insert(now);
            generate_tree(now.x, now.y);
            ++cnt;
            {
                std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
                operations.push_back(std::format("set_schedule decorate {} {}", cnt, decorate_sectors.size()));
            }
        }
        std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
        operations.push_back("init_done");
    }

    void generate_sector(int dx, int dy) {
        Sector *sector = new Sector;
        Sector_pos p = {dx, dy};
        const std::bitset<16 * 256 * 16> &hole = holes.at(p);
        //                                整体高度        地形起伏         植被（树）密度 雨量
        // inline int32_t get_biome(float altitude, float fluctuate, float tree_density, float rain)
        std::array<float, 16 * 16> noise_altitude, noise_fluctuate, noise_terrain, noise_tree_density, noise_rain;
        dx *= 16, dy *= 16;
        // float tmp;
        int height1, height2;
        noise->GenUniformGrid2D(noise_altitude.data(), dx * 0.05f, dy * 0.05f, 16, 16, 0.05f, 0.05f, seed);
        noise->GenUniformGrid2D(noise_fluctuate.data(), dx * 0.3f, dy * 0.3f, 16, 16, 0.3f, 0.3f, seed + 1);
        terrain_noise_generater->GenUniformGrid2D(noise_terrain.data(), dx * 2, dy * 2, 16, 16, 2, 2, seed + 1);
        noise->GenUniformGrid2D(noise_tree_density.data(), dx * 0.5f, dy * 0.5f, 16, 16, 0.5f, 0.5f, seed + 2);
        noise->GenUniformGrid2D(noise_rain.data(), dx * 0.1f, dy * 0.1f, 16, 16, 0.1f, 0.1f, seed + 3);
        for (int x = 0, idx = 0; x != 16; ++x) {
            for (int z = 0; z != 16; ++z) {
                height1 = noise_altitude[z * 16 + x] * 40 + 40 + 64 + (noise_terrain[z * 16 + x] + 1) * terrain_fluctuate_calc(noise_fluctuate[z * 16 + x] * 0.5f + 0.5f) +
                          pow((noise_fluctuate[z * 16 + x] + 1) / 2, 2) * 20;
                height2 = height1 + 5;
                sector->blocks[idx] = {5, (0 << 4) + 0}, ++idx;  // bedrock
                for (int y = 1; y < height1; ++y, ++idx) {
                    sector->blocks[idx] = hole[idx] ? air : Block{4, (0 << 4) + 0};  // stone
                }
                for (int y = height1; y < height2; ++y, ++idx) {
                    sector->blocks[idx] = hole[idx] ? air : Block{3, (0 << 4) + 0};  // dirt
                }
                sector->blocks[idx] = hole[idx] ? full_light_calc_flag.insert(p), air : Block{2, (0 << 4) + 0}, ++idx;  // grass_block
                std::fill(sector->blocks.begin() + idx, sector->blocks.begin() + idx + 255 - height2, Block{1, (15 << 4) + 0});
                idx += 255 - height2;
                sector->data[x * 16 + z] = height2;
                sector->biome[x * 16 + z] = get_biome(noise_altitude[z * 16 + x], noise_fluctuate[z * 16 + x], noise_tree_density[z * 16 + x], noise_rain[z * 16 + x]);
            }
        }
        holes.erase(p);
        {
            std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
            world[p] = sector;
        }
        if (full_light_calc_flag.count(p)) {
            return;
        }
        // 这里写一个局部的find_block减少unordered_map.at操作的次数
        Sector* sector01 = world.find(p + Sector_pos{ 0,  1}) != world.end() ? world.at(p + Sector_pos{ 0,  1}) : nullptr;
        Sector* sector10 = world.find(p + Sector_pos{ 1,  0}) != world.end() ? world.at(p + Sector_pos{ 1,  0}) : nullptr;
        Sector* sector0m = world.find(p + Sector_pos{ 0, -1}) != world.end() ? world.at(p + Sector_pos{ 0, -1}) : nullptr;
        Sector* sectorm0 = world.find(p + Sector_pos{-1,  0}) != world.end() ? world.at(p + Sector_pos{-1,  0}) : nullptr;
        auto get_sector_without_hash = [p, sector, sector01, sector10, sector0m, sectorm0](int x, int z) -> Sector* {
            Sector_pos sp = get_sector(x, z);
            if (p == sp) return sector;
            else if (p + Sector_pos{ 0,  1} == sp) return sector01;
            else if (p + Sector_pos{ 1,  0} == sp) return sector10;
            else if (p + Sector_pos{ 0, -1} == sp) return sector0m;
            else if (p + Sector_pos{-1,  0} == sp) return sectorm0;
            else return nullptr;
        };
        auto find_block_without_hash = [&get_sector_without_hash](int x, int y, int z) {
            return &get_sector_without_hash(x, z)->blocks[get_block_index(x, y, z)];
        };
        // 让矿洞中的光跨区块传播
        Block* block;
        if (sector10 != nullptr) {
            for (int z = 0; z != 16; ++z) {
                for (int y = 0; y <= sector->data[15 * 16 + z]; ++y) {
                    block = find_block_without_hash(16 + dx, y, z + dy);
                    if (transparent_blocks[sector->blocks[(15 * 16 + z) * 256 + y].id] && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                        full_light_calc_flag.insert(p);
                        return;
                    }
                }
            }
        }
        if (sector01 != nullptr) {
            for (int x = 0; x != 16; ++x) {
                for (int y = 0; y <= sector->data[x * 16 + 15]; ++y) {
                    block = find_block_without_hash(x + dx, y, dy + 16);
                    if (transparent_blocks[sector->blocks[(x * 16 + 15) * 256 + y].id] && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                        full_light_calc_flag.insert(p);
                        return;
                    }
                }
            }
        }
        if (sectorm0 != nullptr) {
            for (int z = 0; z != 16; ++z) {
                for (int y = 0; y <= sector->data[z]; ++y) {
                    block = find_block_without_hash(-1 + dx, y, z + dy);
                    if (transparent_blocks[sector->blocks[z * 256 + y].id] && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                        full_light_calc_flag.insert(p);
                        return;
                    }
                }
            }
        }
        if (sector0m != nullptr) {
            for (int x = 0; x != 16; ++x) {
                for (int y = 0; y <= sector->data[x * 16]; ++y) {
                    block = find_block_without_hash(x + dx, y, dy - 1);
                    if (transparent_blocks[sector->blocks[x * 16 * 256 + y].id] && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                        full_light_calc_flag.insert(p);
                        return;
                    }
                }
            }
        }
    }

    bool has_tree(int x, int z, float tree_calc_value) {
        float hash = hash2D(x, z, seed);
        if (hash <= 1.0f - tree_calc_value)
            return false;
        for (int dx = -3; dx <= 3; ++dx)
            for (int dz = -3; dz <= 3; ++dz) {
                if (dx == 0 && dz == 0)
                    continue;
                if (dx * dx + dz * dz <= 9 && hash2D(x + dx, z + dz, seed) >= hash)
                    return false;
            }
        return true;
    }

    void generate_tree(int sx, int sy) {
        Sector *sector = world.at({sx, sy});
        float tree_density;
        for (int x = 0, y; x != 16; ++x) {
            for (int z = 0; z != 16; ++z) {
                y = sector->data[x * 16 + z];
                switch (sector->biome[x * 16 + z]) {
                    case 0:  // plain
                        tree_density = 0.0015f;
                        break;
                    case 1:  // mountain
                        tree_density = 0.005f;
                        break;
                    case 2:  // forest
                        tree_density = 0.03f;
                        break;
                    case 3:  // highland
                        tree_density = 0.0005f;
                        break;
                    default:
                        tree_density = 0.0f;
                }
                if (has_tree(sx * 16 + x, sy * 16 + z, tree_density) && sector->blocks[get_block_index(x, y, z)].id != 1) {
                    for (int dy = 1; dy < 6; ++dy) {
                        add_block(sx * 16 + x, y + dy, sy * 16 + z, 6, false);
                    }
                    for (int dx = -2; dx <= 2; ++dx) {
                        for (int dz = -2; dz <= 2; ++dz) {
                            for (int dy = 3; dy <= 4; ++dy) {
                                add_block(sx * 16 + x + dx, y + dy, sy * 16 + z + dz, 7, false);
                            }
                        }
                    }
                    for (int dx = -1, dy = 5; dx <= 1; ++dx) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            add_block(sx * 16 + x + dx, y + dy, sy * 16 + z + dz, 7, false);
                        }
                    }
                    for (int dx = -1, dy = 6; dx <= 1; ++dx) {
                        for (int dz = -1; dz <= 1; ++dz) {
                            if (abs(dx) + abs(dz) >= 2)
                                continue;
                            add_block(sx * 16 + x + dx, y + dy, sy * 16 + z + dz, 7, false);
                        }
                    }
                    for (int dx = -3; dx <= 3; ++dx) {
                        for (int dz = -3; dz <= 3; ++dz) {
                            for (int dy = 0; dy <= 7; ++dy) {
                                update_shown(sx * 16 + x + dx, y + dy, sy * 16 + z + dz, exposed(sx * 16 + x + dx, y + dy, sy * 16 + z + dz));
                            }
                        }
                    }
                    for (int dx = -3; dx <= 3; ++dx) {
                        for (int dz = -3; dz <= 3; ++dz) {
                            for (int dy = 0; dy <= 7; ++dy) {
                                std::vector<Block_pos> updated_brightness_blocks = calc_light(sx * 16 + x + dx, y + dy, sy * 16 + z + dz);
                                for (const Block_pos &p : updated_brightness_blocks) {
                                    update_shown(p, exposed(p), true);
                                }
                            }
                        }
                    }
                }
            }
        }
        std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
        operations.push_back(std::format("update_vbo_data {} {}", sx, sy));
        operations.push_back(std::format("update_vbo_data {} {}", sx + 1, sy));
        operations.push_back(std::format("update_vbo_data {} {}", sx, sy + 1));
        operations.push_back(std::format("update_vbo_data {} {}", sx - 1, sy));
        operations.push_back(std::format("update_vbo_data {} {}", sx, sy - 1));
        operations.push_back(std::format("update_vbo_data {} {}", sx + 1, sy + 1));
        operations.push_back(std::format("update_vbo_data {} {}", sx - 1, sy + 1));
        operations.push_back(std::format("update_vbo_data {} {}", sx - 1, sy - 1));
        operations.push_back(std::format("update_vbo_data {} {}", sx + 1, sy - 1));
    }

    void check_exposed_blocks(int dx, int dy) {
        // 这里写一个局部的find_block减少unordered_map.at操作的次数
        Sector_pos p = {dx, dy};
        Sector* sector = world.at(p);
        Sector* sector01 = world.find(p + Sector_pos{ 0,  1}) != world.end() ? world.at(p + Sector_pos{ 0,  1}) : nullptr;
        Sector* sector10 = world.find(p + Sector_pos{ 1,  0}) != world.end() ? world.at(p + Sector_pos{ 1,  0}) : nullptr;
        Sector* sector0m = world.find(p + Sector_pos{ 0, -1}) != world.end() ? world.at(p + Sector_pos{ 0, -1}) : nullptr;
        Sector* sectorm0 = world.find(p + Sector_pos{-1,  0}) != world.end() ? world.at(p + Sector_pos{-1,  0}) : nullptr;
        Sector* sector11 = world.find(p + Sector_pos{ 1,  1}) != world.end() ? world.at(p + Sector_pos{ 1,  1}) : nullptr;
        Sector* sectormm = world.find(p + Sector_pos{-1, -1}) != world.end() ? world.at(p + Sector_pos{-1, -1}) : nullptr;
        Sector* sector1m = world.find(p + Sector_pos{ 1, -1}) != world.end() ? world.at(p + Sector_pos{ 1, -1}) : nullptr;
        Sector* sectorm1 = world.find(p + Sector_pos{-1,  1}) != world.end() ? world.at(p + Sector_pos{-1,  1}) : nullptr;
        sector->vertex_data_struct_mutex.lock();
        if (sector01 != nullptr) sector01->vertex_data_struct_mutex.lock();
        if (sector10 != nullptr) sector10->vertex_data_struct_mutex.lock();
        if (sector0m != nullptr) sector0m->vertex_data_struct_mutex.lock();
        if (sectorm0 != nullptr) sectorm0->vertex_data_struct_mutex.lock();
        if (sector11 != nullptr) sector11->vertex_data_struct_mutex.lock();
        if (sectormm != nullptr) sectormm->vertex_data_struct_mutex.lock();
        if (sector1m != nullptr) sector1m->vertex_data_struct_mutex.lock();
        if (sectorm1 != nullptr) sectorm1->vertex_data_struct_mutex.lock();
        dx *= 16, dy *= 16;
        auto get_sector_without_hash = [p, sector, sector01, sector10, sector0m, sectorm0, sector11, sectormm, sector1m, sectorm1](Block_pos bp) -> Sector* {
            Sector_pos sp = get_sector(bp);
            if (p == sp) return sector;
            else if (p + Sector_pos{ 0,  1} == sp) return sector01;
            else if (p + Sector_pos{ 1,  0} == sp) return sector10;
            else if (p + Sector_pos{ 0, -1} == sp) return sector0m;
            else if (p + Sector_pos{-1,  0} == sp) return sectorm0;
            else if (p + Sector_pos{ 1,  1} == sp) return sector11;
            else if (p + Sector_pos{-1, -1} == sp) return sectormm;
            else if (p + Sector_pos{ 1, -1} == sp) return sector1m;
            else if (p + Sector_pos{-1,  1} == sp) return sectorm1;
            else return nullptr;
        };
        auto find_block_without_hash = [&get_sector_without_hash](Block_pos bp) {
            if (bp.y < 0 || bp.y >= 256) {
                return &sector_not_loaded;
            }
            Sector *sp = get_sector_without_hash(bp);
            if (sp != nullptr) {
                return &sp->blocks[get_block_index(bp)];
            } else {
                return &sector_not_loaded;
            }
        };
        auto exposed_without_hash = [&find_block_without_hash](int x, int y, int z) -> uint64_t {
            Block_pos p = {x, y, z};
            if (find_block_without_hash(p)->id == 0 || find_block_without_hash(p)->id == 1) {
                return 0ull;
            }
            uint64_t res = 0ull;
            for (int i = 0, dx, dy, dz; i < 6; ++i) {
                dx = FACES[i].x, dy = FACES[i].y, dz = FACES[i].z;
                // 同样的透明方块之间的面一个隐藏，一个显示
                if (y + dy < 0 || y + dy >= 256) {
                    res |= (1ull << i);
                } else if (transparent_blocks[find_block_without_hash(p)->id] && (!transparent_blocks[find_block_without_hash(p + FACES[i])->id])) {
                    res |= (1ull << i);
                } else if (transparent_blocks[find_block_without_hash(p + FACES[i])->id] && find_block_without_hash(p + FACES[i])->id != find_block_without_hash(p)->id) {
                    res |= (1ull << i);
                } else if (transparent_blocks[find_block_without_hash(p + FACES[i])->id] && find_block_without_hash(p + FACES[i])->id == find_block_without_hash(p)->id && (dx == 1 || dy == 1 || dz == 1)) {
                    res |= (1ull << i);
                }
            }
            return res;
        };
        auto get_tex_array_data_without_hash = [&find_block_without_hash](int x, int y, int z, uint32_t id, int face_index, std::array<float, 4 * 7> &res) {
            int img_idx = textures[id][face_index];
            std::array<std::array<float, 13>, 6> vertices;
            cube_vertices(x, y, z, 0.5, vertices);
            uint8_t light;
            Block_pos p = {x, y, z};
            for (uint32_t i = 0, j = 0, brightness; i != 4 * 7; i += 7, j += 3) {
                res[i] = vertices[face_index][j];
                res[i + 1] = vertices[face_index][j + 1];
                res[i + 2] = vertices[face_index][j + 2];
                res[i + 3] = uvs[j];
                res[i + 4] = uvs[j + 1];
                res[i + 5] = img_idx;
                if ((p + normals[(int)vertices[face_index][12]]).y == 256) {
                    light = 15 << 4;
                } else if (transparent_blocks[find_block_without_hash(p)->id] && (!transparent_blocks[find_block_without_hash(p + normals[(int)vertices[face_index][12]])->id])) {
                    light = find_block_without_hash(p)->light;
                } else {
                    light = find_block_without_hash(p + normals[(int)vertices[face_index][12]])->light;
                }
                brightness = std::max(light >> 4, light & 15);
                res[i + 6] = brightness * 7 + vertices[face_index][12];
            }
        };
        auto update_shown_without_hash = [&get_sector_without_hash, &find_block_without_hash, &get_tex_array_data_without_hash](int x, int y, int z, uint64_t shown) {
            if (shown == 0ull) {
                return;
            }
            Sector *sector = get_sector_without_hash({x, y, z});
            uint64_t mask;
            uint16_t id = find_block_without_hash({x, y, z})->id;
            int index = get_block_index(x, y, z);
            uint16_t change_width;
            change_width = std::bit_width(shown);
            sector->shown[index].second.resize(change_width);
            std::array<float, 4 * 7> vertex_texture_data;
            for (uint16_t i = 0; i != change_width; ++i) {
                mask = 1ull << i;
                if (shown & mask) {
                    get_tex_array_data_without_hash(x, y, z, id, i, vertex_texture_data);
                    sector->shown[index].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
                }
            }
            sector->shown[index].first = shown;
        };
        for (int x = 0; x < 16; ++x) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y <= sector->data[x * 16 + z]; ++y) {
                    update_shown_without_hash(dx + x, y, z + dy, exposed_without_hash(dx + x, y, z + dy));
                }
            }
        }
        auto update_shown_full_without_hash = [&get_sector_without_hash, &find_block_without_hash, &get_tex_array_data_without_hash](int x, int y, int z, uint64_t shown) {
            Sector* sector = get_sector_without_hash({x, y, z});
            uint64_t mask;
            uint16_t id = find_block_without_hash({x, y, z})->id;
            int index = get_block_index(x, y, z);
            uint16_t change_width;
            change_width = std::bit_width(shown ^ sector->shown[index].first);
            sector->shown[index].second.resize(std::max(std::bit_width(sector->shown[index].first), std::bit_width(shown)));
            std::array<float, 4 * 7> vertex_texture_data;
            for (uint16_t i = 0; i != change_width; ++i) {
                mask = 1ull << i;
                if ((shown & mask) != (sector->shown[index].first & mask)) {
                    if (shown & mask) {
                        get_tex_array_data_without_hash(x, y, z, id, i, vertex_texture_data);
                        sector->shown[index].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
                    } else {
                        sector->vertex_data_struct.erase(sector->shown[index].second[i]);
                    }
                }
            }
            sector->shown[index].first = shown;
            sector->shown[index].second.resize(std::bit_width(shown));
        };
        if (sector01 != nullptr) {
            for (int x = 0; x < 16; ++x) {
                for (int y = 0; y <= sector01->data[x * 16]; ++y) {
                    update_shown_full_without_hash(dx + x, y, 16 + dy, exposed_without_hash(dx + x, y, 16 + dy));
                }
            }
        }
        if (sector10 != nullptr) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y <= sector10->data[z]; ++y) {
                    update_shown_full_without_hash(dx + 16, y, z + dy, exposed_without_hash(dx + 16, y, z + dy));
                }
            }
        }
        if (sector0m != nullptr) {
            for (int x = 0; x < 16; ++x) {
                for (int y = 0; y <= sector0m->data[x * 16 + 15]; ++y) {
                    update_shown_full_without_hash(dx + x, y, -1 + dy, exposed_without_hash(dx + x, y, -1 + dy));
                }
            }
        }
        if (sectorm0 != nullptr) {
            for (int z = 0; z < 16; ++z) {
                for (int y = 0; y <= sectorm0->data[15 * 16 + z]; ++y) {
                    update_shown_full_without_hash(dx - 1, y, z + dy, exposed_without_hash(dx - 1, y, z + dy));
                }
            }
        }
        sector->vertex_data_struct_mutex.unlock();
        if (sector01 != nullptr) sector01->vertex_data_struct_mutex.unlock();
        if (sector10 != nullptr) sector10->vertex_data_struct_mutex.unlock();
        if (sector0m != nullptr) sector0m->vertex_data_struct_mutex.unlock();
        if (sectorm0 != nullptr) sectorm0->vertex_data_struct_mutex.unlock();
        if (sector11 != nullptr) sector11->vertex_data_struct_mutex.unlock();
        if (sectormm != nullptr) sectormm->vertex_data_struct_mutex.unlock();
        if (sector1m != nullptr) sector1m->vertex_data_struct_mutex.unlock();
        if (sectorm1 != nullptr) sectorm1->vertex_data_struct_mutex.unlock();
    }

    // 洞穴最大长度：64, 每次以玩家为圆心，simulate_distance +
    // 5为半径，在圆上每个区块（随机高度）生成洞穴。
    void generate_holes(float x, float y, float z) {
        float dx[64], dy[64], dz[64], size[64];
        int length = (hash2D(x, y, seed) + hash2D(y, z, seed) + hash2D(z, x, seed)) * 64 / 6 + 32;  // [32, 64]
        noise->GenUniformGrid2D(dx, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 3.0f, seed);
        noise->GenUniformGrid2D(dy, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 2.0f, seed + 1);
        noise->GenUniformGrid2D(dz, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 3.0f, seed + 2);
        noise->GenUniformGrid2D(size, ((int)x ^ (int)y ^ (int)z), 0, length, 1, 2.0f, 2.0f, seed + 3);
        std::tuple<float, float, float> tmp;
        float size_float;
        for (int i = 0, size_int, x_int, y_int, z_int, final_x, final_y, final_z; i < length; ++i) {
            tmp = normalize(dx[i], dy[i] * 0.5, dz[i]);
            x += std::get<0>(tmp), y += std::get<1>(tmp), z += std::get<2>(tmp);
            x_int = x, y_int = y, z_int = z;
            size_float = size[i] * 1.5 + 3.5;
            size_int = (int)size_float + 1;
            for (int bx = -size_int; bx <= size_int; ++bx)
                for (int by = -size_int; by <= size_int; ++by)
                    for (int bz = -size_int; bz <= size_int; ++bz)
                        if (bx * bx + by * by + bz * bz <= size_float * size_float) {
                            final_x = x_int + bx, final_y = y_int + by, final_z = z_int + bz;
                            if (final_y <= 0 || final_y >= 256)
                                break;
                            holes[get_sector(final_x, final_z)][get_block_index(final_x, final_y, final_z)] = true;
                        }
        }
    }

    std::vector<Block_pos> calc_light(int x, int y, int z) {
        // 返回要更新亮度的实体方块
        std::queue<Block_pos> bfs;
        bfs.push({x, y, z});
        uint8_t tmp_light;
        Block *neighbor;
        std::vector<Block_pos> updated;
        std::unordered_set<Block_pos, Block_pos_hash> vst;
        Block_pos now;
        uint8_t final_light;
        // 方块光照
        while (!bfs.empty()) {
            now = bfs.front();
            bfs.pop();
            tmp_light = 0u;
            for (int i = 0; i < 6; ++i) {
                neighbor = find_block(now + FACES[i]);
                tmp_light = std::max<uint8_t>(tmp_light, neighbor->light & 15);
            }
            final_light = std::max<uint8_t>(block_light[find_block(now)->id], tmp_light - std::min<uint8_t>(tmp_light, light_attenuation[find_block(now)->id]));
            if ((find_block(now)->light & 15) != final_light) {
                find_block(now)->light = final_light + (find_block(now)->light & 240);
                for (int i = 0; i < 6; ++i) {
                    neighbor = find_block(now + FACES[i]);
                    if (neighbor->id == 0) {
                        continue;
                    }
                    if (neighbor->id == 1) {
                        bfs.push(now + FACES[i]);
                    } else {
                        if (vst.count(now + FACES[i]) == 0) {
                            updated.push_back(now + FACES[i]);
                            vst.insert(now + FACES[i]);
                        }
                    }
                }
            }
        }
        bfs.push({x, y, z});
        // 天空光照
        while (!bfs.empty()) {
            now = bfs.front();
            bfs.pop();
            tmp_light = 0u;
            for (int i = 0; i < 6; ++i) {
                neighbor = find_block(now + FACES[i]);
                tmp_light = std::max<uint8_t>(tmp_light, neighbor->light & 240);
            }
            final_light = tmp_light - std::min<uint8_t>(tmp_light, 16 * light_attenuation[find_block(now)->id]);
            if (now.y == 255 || (find_block(now)->id == 1 && find_block(now + Block_pos{0, 1, 0})->id == 1 && (find_block(now + Block_pos{0, 1, 0})->light & 240) == 240)) {
                final_light = 240;
            }
            if ((find_block(now)->light & 240) != final_light) {
                find_block(now)->light = final_light + (find_block(now)->light & 15);
                for (int i = 0; i < 6; ++i) {
                    neighbor = find_block(now + FACES[i]);
                    if (neighbor->id == 0) {
                        continue;
                    }
                    if (transparent_blocks[neighbor->id]) {
                        bfs.push(now + FACES[i]);
                    }
                    if (neighbor->id != 1) {
                        if (vst.count(now + FACES[i]) == 0) {
                            updated.push_back(now + FACES[i]);
                            vst.insert(now + FACES[i]);
                        }
                    }
                }
            }
        }
        return updated;
    }

    inline std::vector<Block_pos> calc_light(Block_pos p) {
        return calc_light(p.x, p.y, p.z);
    }

    void calc_sector_light(int dx, int dy) {
        Sector_pos p = {dx, dy};
        // 这里不需要加锁，unordered_map.at操作是线程安全的，
        // 调用calc_sector_light时不会有别的线程访问这个区块(见process_sector_thread)
        std::unordered_set<Sector_pos, Sector_pos_hash>::iterator it;
        if ((it = full_light_calc_flag.find(p)) != full_light_calc_flag.end()) {
            full_light_calc_flag.erase(it);
            Sector *sector = world.at(p);
            dx *= 16, dy *= 16;
            std::vector<Block_pos> updated;
            for (int x = 0; x < 16; ++x) {
                for (int z = 0; z < 16; ++z) {
                    for (int y = 0; y <= sector->data[x * 16 + z]; ++y) {
                        updated = calc_light(x + dx, y, z + dy);
                        for (const Block_pos& i : updated) {
                            if (get_sector(i) != p) {
                                update_shown(i, exposed(i));
                                update_shown(i, exposed(i), true);
                            }
                        }
                    }
                }
            }
        }
    }

    void check_neighbors(int x, int y, int z) {
        Block_pos p = {x, y, z};
        for (int i = 0; i < 6; ++i) {
            update_shown(p + FACES[i], exposed(p + FACES[i]));
        }
    }

    void update_shown(int x, int y, int z, uint64_t shown, bool force_update = false) {
        // 如果force_update，会强制重新生成显示的面的顶点数据，用于光照更新，但此时要保证面的显示情况和之前相同
        Sector_pos sector_pos = get_sector(x, z);
        if (world.count(sector_pos) == 0)
            return;
        Sector *sector = world.at(sector_pos);
        uint64_t mask;
        int index = get_block_index(x, y, z);
        uint16_t change_width;
        change_width = std::bit_width(shown ^ sector->shown[index].first);
        sector->shown[index].second.resize(std::max(std::bit_width(sector->shown[index].first), std::bit_width(shown)));
        if (force_update) {
            change_width = std::bit_width(shown);
        }
        std::array<float, 4 * 7> vertex_texture_data;
        std::lock_guard<std::mutex> mtx(sector->vertex_data_struct_mutex);
        for (uint16_t i = 0; i != change_width; ++i) {
            mask = 1ull << i;
            if (force_update) {
                if (shown & mask) {
                    get_tex_array_data(x, y, z, i, vertex_texture_data);
                    sector->vertex_data_struct.change_data(sector->shown[index].second[i], vertex_texture_data);
                }
            } else if ((shown & mask) != (sector->shown[index].first & mask)) {
                if (shown & mask) {
                    get_tex_array_data(x, y, z, i, vertex_texture_data);
                    sector->shown[index].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
                } else {
                    sector->vertex_data_struct.erase(sector->shown[index].second[i]);
                }
            }
        }
        sector->shown[index].first = shown;
        sector->shown[index].second.resize(std::bit_width(shown));
    }

    inline void update_shown(const Block_pos &p, uint64_t shown, bool force_update = false) {
        update_shown(p.x, p.y, p.z, shown, force_update);
    }

    void get_tex_array_data(int x, int y, int z, int face_index, std::array<float, 4 * 7> &res) const {
        uint32_t id = find_block(x, y, z)->id;
        int img_idx = textures[id][face_index];
        std::array<std::array<float, 13>, 6> vertices;
        cube_vertices(x, y, z, 0.5, vertices);
        uint8_t light;
        Block_pos p = {x, y, z};
        for (uint32_t i = 0, j = 0, brightness; i != 4 * 7; i += 7, j += 3) {
            res[i] = vertices[face_index][j];
            res[i + 1] = vertices[face_index][j + 1];
            res[i + 2] = vertices[face_index][j + 2];
            res[i + 3] = uvs[j];
            res[i + 4] = uvs[j + 1];
            res[i + 5] = img_idx;
            if ((p + normals[(int)vertices[face_index][12]]).y == 256) {
                light = 15 << 4;
            } else if (transparent_blocks[find_block(p)->id] && (!transparent_blocks[find_block(p + normals[(int)vertices[face_index][12]])->id])) {
                light = find_block(p)->light;
            } else {
                light = find_block(p + normals[(int)vertices[face_index][12]])->light;
            }
            brightness = std::max(light >> 4, light & 15);
            res[i + 6] = brightness * 7 + vertices[face_index][12];
        }
    }

    uint64_t exposed(int x, int y, int z) const {
        if (find_block(x, y, z)->id == 0 || find_block(x, y, z)->id == 1) {
            return 0ull;
        }
        uint64_t res = 0ull;
        Block_pos p = {x, y, z};
        for (int i = 0, dx, dy, dz; i < 6; ++i) {
            dx = FACES[i].x, dy = FACES[i].y, dz = FACES[i].z;
            // 同样的透明方块之间的面一个隐藏，一个显示
            if (y + dy < 0 || y + dy >= 256) {
                res |= (1ull << i);
            } else if (transparent_blocks[find_block(p)->id] && (!transparent_blocks[find_block(p + FACES[i])->id])) {
                res |= (1ull << i);
            } else if (transparent_blocks[find_block(p + FACES[i])->id] && find_block(p + FACES[i])->id != find_block(p)->id) {
                res |= (1ull << i);
            } else if (transparent_blocks[find_block(p + FACES[i])->id] && find_block(p + FACES[i])->id == find_block(p)->id && (dx == 1 || dy == 1 || dz == 1)) {
                res |= (1ull << i);
            }
        }
        return res;
    }

    inline uint64_t exposed(const Block_pos &p) {
        return exposed(p.x, p.y, p.z);
    }

public:
    World(int seed_arg, int simulate_distance_arg) {
        simulate_distance_arg += 2;
        noise = FastNoise::NewFromEncodedNodeTree("DQkR@BSEMJBw@AEhDBAMAAMhCDA==");
        terrain_noise_generater = FastNoise::NewFromEncodedNodeTree("EQ@AJZDCQM@ABIQwQDAAD6QwQ=");
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
        for (int x = -simulate_distance; x <= simulate_distance; ++x) {
            for (int y = -simulate_distance; y <= simulate_distance; ++y) {
                bool flag = true;
                for (int i = 0, dx, dy; i < 8; ++i) {
                    dx = SECTOR_FACES[i].x, dy = SECTOR_FACES[i].y;
                    if (!simulate_sectors.count({x + dx, y + dy})) {
                        flag = false;
                        break;
                    }
                }
                if (flag) {
                    decorate_sectors.insert({x, y});
                }
            }
        }
        std::thread t(&World::init_sectors, this);
        t.detach();
    };

    ~World() {
        stop_all_thread();
        for (auto &pair : world)
            delete pair.second;
    }

    void set_position(double x, double y, double z) {
        std::lock_guard<std::recursive_mutex> lock_position(position_mutex);
        position = {x, y, z};
    }

    void add_operation(std::string op) {
        std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
        operations.push_back(op);
    }

    void add_block(int x, int y, int z, uint16_t id, bool auto_process = true, bool has_NBT = false, const std::map<std::string, std::string> &NBT = emptyNBT) {
        if (auto_process) {
            world_mutex.lock();
        }
        Block *block = find_block(x, y, z);
        if (block->id == 0 || block->id != 1)
            return;
        if (auto_process) {
            Pos position_local;
            {
                std::lock_guard<std::recursive_mutex> lock_position(position_mutex);
                position_local = position;
            }
            for (const entity_box &a : block_entity_boxes[id])
                for (const entity_box &b : entity_entity_boxes[0])
                    if (AABB(a, b, {(double)x, (double)y, (double)z}, position_local))
                        return;
        }
        block->id = id;
        block->light = (0 << 4) + block_light[id];
        if (world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)] < y) {
            world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)] = y;
        }
        if (has_NBT) {
            world.at(get_sector(x, z))->NBTs[{mod16(x), y, mod16(z)}] = NBT;
        }
        if (auto_process) {
            update_shown(x, y, z, exposed(x, y, z));
            check_neighbors(x, y, z);
            std::set<Sector_pos> updated_vbos;
            std::vector<Block_pos> updated_brightness_blocks = calc_light(x, y, z);
            for (const Block_pos &i : updated_brightness_blocks) {
                update_shown(i, exposed(i), true);
                updated_vbos.insert(get_sector(i.x, i.z));
            }
            if (transparent_blocks[id]) {
                update_shown(x, y, z, exposed(x, y, z), true);
            }
            for (int i = 0; i < 6; ++i) {
                std::vector<Block_pos> updated_brightness_blocks = calc_light(Block_pos{x, y, z} + FACES[i]);
                for (const Block_pos &i : updated_brightness_blocks) {
                    update_shown(i, exposed(i), true);
                    updated_vbos.insert(get_sector(i.x, i.z));
                }
            }
            world_mutex.unlock();
            updated_vbos.insert(get_sector(x, z));
            for (int i = 0, dx, dz; i < 6; ++i) {
                dx = FACES[i].x, dz = FACES[i].z;
                updated_vbos.insert(get_sector(x + dx, z + dz));
            }
            std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
            operations.push_back(std::format("block_update {} {} {}", x, y, z));
            for (const Sector_pos &i : updated_vbos) {
                operations.push_back(std::format("update_vbo_data {} {}", i.x, i.y));
            }
        }
    }

    void remove_block(const int x, const int y, const int z, bool auto_process = true) {
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        Block *block = find_block(x, y, z);
        if (block->id == 0 || block->id == 1)
            return;
        update_shown(x, y, z, 0ull);
        if (world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)] == y) {
            int maxy = y - 1;
            while (find_block(x, maxy, z)->id == 1) {
                --maxy;
            }
            world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)] = maxy;
        }
        std::unordered_map<Block_pos, std::map<std::string, std::string>, Block_pos_hash>::iterator it = world.at(get_sector(x, z))->NBTs.find({mod16(x), y, mod16(z)});
        if (it != world.at({get_sector(x, z)})->NBTs.end()) {
            world.at({get_sector(x, z)})->NBTs.erase(it);
        }
        (*block) = air;
        if (auto_process) {
            check_neighbors(x, y, z);
            std::set<Sector_pos> updated_vbos;
            std::vector<Block_pos> updated_brightness_blocks = calc_light(x, y, z);
            for (const Block_pos &i : updated_brightness_blocks) {
                update_shown(i, exposed(i), true);
                updated_vbos.insert(get_sector(i.x, i.z));
            }
            updated_vbos.insert(get_sector(x, z));
            for (int i = 0, dx, dz; i < 6; ++i) {
                dx = FACES[i].x, dz = FACES[i].z;
                updated_vbos.insert(get_sector(x + dx, z + dz));
            }
            std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
            operations.push_back(std::format("block_update {} {} {}", x, y, z));
            for (const Sector_pos &i : updated_vbos) {
                operations.push_back(std::format("update_vbo_data {} {}", i.x, i.y));
            }
        }
    }

    inline void lock_sector_vertex_data_struct_mutex(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        world.at({x, y})->vertex_data_struct_mutex.lock();
    }

    inline void unlock_sector_vertex_data_struct_mutex(int x, int y) {
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        world.at({x, y})->vertex_data_struct_mutex.unlock();
    }

    inline void start_process_sector_thread() {
        std::thread t(&World::process_sector_thread, this);
        t.detach();
    }

    inline std::string give_operation() {
        std::lock_guard<std::recursive_mutex> lock(operations_mutex);
        if (operations.empty())
            return "None";
        std::string res = operations.front();
        operations.pop_front();
        return res;
    }

    inline int get_block(int x, int y, int z) const {
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        return find_block(x, y, z)->id;
    }

    bool intersect(int entity, double x, double y, double z) const {
        /* 如果与方块碰撞，返回true */
        int ix = x, iy = y, iz = z;
        int nx, ny, nz;
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        for (int dx = -2; dx <= 2; ++dx) {
            for (int dy = -2; dy <= 3; ++dy) {
                for (int dz = -2; dz <= 2; ++dz) {
                    nx = ix + dx, ny = iy + dy, nz = iz + dz;
                    if (ny >= 256 || ny < 0)
                        continue;
                    for (entity_box i : entity_entity_boxes[entity])
                        for (entity_box j : block_entity_boxes[find_block(nx, ny, nz)->id])
                            if (AABB(i, j, {x, y, z}, {(double)nx, (double)ny, (double)nz}))
                                return true;
                }
            }
        }
        return false;
    }

    pybind11::tuple hit_test(double x, double y, double z, double dx, double dy, double dz, double max_distance) const {
        int m = 16;  // 精度
        int px = x, py = y, pz = z;
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        for (int i = 0, kx, ky, kz; i < max_distance * m; ++i) {
            kx = round(x), ky = round(y), kz = round(z);
            if (ky < 0 || ky >= 256) {
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

    inline pybind11::tuple get_sector_vbo_data_ptr(int x, int y) const {
        // 第一个是指针，第二个是长度
        return pybind11::make_tuple(reinterpret_cast<uintptr_t>(world.at({x, y})->vertex_data_struct.get_data_ptr(0)), world.at({x, y})->vertex_data_struct.size());
    }

    inline int get_brightness(int x, int y, int z) const {
        return find_block(x, y, z)->light;
    }

    inline int get_max_height(int x, int z) const {
        auto it = world.find(get_sector(x, z));
        if (it == world.end()) {
            return 0;
        } else {
            return it->second->data[mod16(x) * 16 + mod16(z)];
        }
    }
};

PYBIND11_MODULE(MCworld, m) {
    using namespace pybind11::literals;  // 引入 _a 字面量，让代码更可读
    pybind11::class_<World>(m, "World")
        .def(pybind11::init<int, int>())
        .def("start_process_sector_thread", &World::start_process_sector_thread, pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("give_operation", &World::give_operation)
        .def("get_block", &World::get_block)
        .def("add_block", &World::add_block, "x"_a, "y"_a, "z"_a, "id"_a, "auto_process"_a = true, "has_NBT"_a = false, pybind11::arg("NBT") = emptyNBT)
        .def("remove_block", &World::remove_block, "x"_a, "y"_a, "z"_a, "auto_process"_a = true)
        .def("intersect", &World::intersect)
        .def("hit_test", &World::hit_test)
        .def("set_position", &World::set_position)
        .def("add_operation", &World::add_operation)
        .def("get_sector_vbo_data_ptr", &World::get_sector_vbo_data_ptr)
        .def("lock_sector_vertex_data_struct_mutex", &World::lock_sector_vertex_data_struct_mutex)
        .def("unlock_sector_vertex_data_struct_mutex", &World::unlock_sector_vertex_data_struct_mutex)
        .def("get_brightness", &World::get_brightness)
        .def("get_max_height", &World::get_max_height);
}
