#define FASTNOISE_STATIC_LIB
#include <FastNoise/FastNoise.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered/unordered_flat_set.hpp>

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
    inline Sector_pos operator+(Sector_pos b) const {
        return {x + b.x, y + b.y};
    }
    inline bool operator==(Sector_pos b) const {
        return x == b.x && y == b.y;
    }
    inline bool operator<(Sector_pos b) const {
        return x != b.x ? x < b.x : y < b.y;
    }
};

struct Sector_pos_hash {
    size_t operator()(Sector_pos p) const {
        uint64_t h = (uint64_t)(uint32_t)p.x * 0x9E3779B97F4A7C15ull
        ^ (uint64_t)(uint32_t)p.y * 0xC2B2AE3D27D4EB4Full;
        h ^= h >> 29;
        h *= 0xBF58476D1CE4E5B9ull;
        h ^= h >> 32;
        return h;
    }
};

struct Block_pos {
    int x, y, z;
    inline Block_pos operator+(Block_pos b) const {
        return {x + b.x, y + b.y, z + b.z};
    }
    inline bool operator==(Block_pos b) const {
        return x == b.x && y == b.y && z == b.z;
    }
};

inline int mod16(int x) {
    return x & 15;
}

inline Sector_pos get_sector(int x, int y) {
    return {x >> 4, y >> 4};
}
inline int get_block_index(int x, int y, int z) {
    return (mod16(x) * 16 + mod16(z)) * 256 + y;
}

inline Sector_pos get_sector(Block_pos p) {
    return get_sector(p.x, p.z);
}
inline int get_block_index(Block_pos p) {
    return get_block_index(p.x, p.y, p.z);
}

struct Block_pos_hash {
    inline size_t operator()(Block_pos p) const {
        return get_block_index(p);
    }
};

struct Pos {
    double x, y, z;
    inline Pos operator+(Pos b) const {
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
    uint8_t data;   // 特定方块可以用来储存数据（如水流强度、红石强度）
};

constexpr Block air{1, (0 << 4) + 0, 0};
Block sector_not_loaded{0, (0 << 4) + 0, 0};

class GL_QUADS_vbo_data {
private:
    uint64_t _size;
    std::vector<uint64_t> unoccupied_ids;
    std::vector<float> data;

public:
    inline GL_QUADS_vbo_data() {
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

struct entity_box {
    double minx, maxx, miny, maxy, minz, maxz;
};

const std::vector<entity_box> entity_entity_boxes[] = {{entity_box{-0.3, 0.3, -0.5, 1.3, -0.3, 0.3}}};


constexpr Block_pos normals[7] = {{0, 0, 0}, {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
constexpr float uvs[] = {0.0f, 0.0f, -1.0f, 1.0f, 0.0f, -1.0f, 1.0f, 1.0f, -1.0f, 0.0f, 1.0f, -1.0f};  // -1.0用于和顶点三个数据对齐

inline bool AABB(const entity_box &a, const entity_box &b, Pos da, Pos db) {
    /* 如果没有碰撞返回False */
    return !(a.maxx + da.x < b.minx + db.x || b.maxx + db.x < a.minx + da.x || a.maxy + da.y < b.miny + db.y || b.maxy + db.y < a.miny + da.y || a.maxz + da.z < b.minz + db.z ||
             b.maxz + db.z < a.minz + da.z);
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

/*constexpr std::string_view biome_id[] = {
    "biome.minecraft.plain",
    "biome.minecraft.mountain",
    "biome.minecraft.forest",
    "biome.minecraft.highland",
};*/
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

struct block_update_entry {
    Block_pos p;
    uint16_t delay;
    uint16_t block;    // 入队时这次更新的方块，若处理时不是这个方块，跳过
};

template<class _Tp> struct Arg {
    bool has_value;
    _Tp value;
};

class World {
private:
    boost::unordered_flat_map<Sector_pos, Sector *, Sector_pos_hash> world;
    boost::unordered_flat_map<Sector_pos, std::vector<Block_pos>, Sector_pos_hash> full_light_calc_flag;
    int simulate_distance;
    std::vector<Sector_pos> simulate_sectors, decorate_sectors, generate_holes_sectors;
    std::atomic<double> position_x, position_y, position_z;
    boost::unordered_flat_set<Sector_pos, Sector_pos_hash> shown_sectors, generated_sectors, decorated_sectors, generated_holes, calced_light_sectors;
    boost::unordered_flat_map<Sector_pos, std::bitset<16 * 256 * 16>, Sector_pos_hash> holes;  // true即洞穴
    std::vector<block_update_entry> block_updates;
    FastNoise::SmartNode<> noise;
    FastNoise::SmartNode<> terrain_noise_generater;
    int seed;

    // 添加互斥锁，使用 recursive_mutex 允许同一个线程多次加锁
    mutable std::recursive_mutex world_mutex;       // world
    mutable std::recursive_mutex operations_mutex;  // operations
    mutable std::recursive_mutex block_updates_mutex;    // block_updates
    // 对于多线程数据竞争的问题，现在除了主线程，另外有一个 process_sector_thread
    // 的线程，这个线程处理的都是边缘的区块。 主线程只有python调用 add_block,
    // remove_block, get_block, hit_test, intersect 会触发对 world
    // 的访问，且只会与玩家所在的区块相关，并且这些函数内部都加锁了
    // unordered_map.at操作是线程安全的，所以除了 generate_sector 中 operator[],
    // 其他process_sector_thread 中调用的函数不需要加锁。

    std::atomic<int> running_threads;
    std::atomic<bool> exit_flag;

    void stop_all_thread();
    void process_sector_thread();
    void init_sectors();
    void generate_sector(int dx, int dy);
    bool has_tree(int x, int z, float tree_calc_value);
    void generate_tree(int sx, int sy);
    void check_exposed_blocks(int dx, int dy);
    // 洞穴最大长度：64, 每次以玩家为圆心，simulate_distance +
    // 5为半径，在圆上每个区块（随机高度）生成洞穴。
    void generate_holes(float x, float y, float z);
    std::vector<Block_pos> calc_light(int x, int y, int z);
    inline std::vector<Block_pos> calc_light(Block_pos p);
    void calc_sector_light(int dx, int dy);
    void update_shown(int x, int y, int z, uint64_t shown, bool force_update = false);
    inline void update_shown(Block_pos p, uint64_t shown, bool force_update = false);
    template<typename F> inline void get_tex_array_data(Block_pos p, int face_index, std::array<float, 4 * 7> &res, const F& find_block_func) const;
    inline void get_tex_array_data(Block_pos p, int face_index, std::array<float, 4 * 7> &res) const;
    template<typename F> uint64_t exposed(int x, int y, int z, const F& find_block_func) const;
    template<typename F> inline uint64_t exposed(Block_pos p, const F& find_block_func) const;
    inline uint64_t exposed(int x, int y, int z) const;
    inline uint64_t exposed(Block_pos p) const;

public:
    std::list<std::string> operations;  // 双链表，可以O(1)连接两个链表，在generate_sector中使用
    World(int seed_arg, int simulate_distance_arg);
    ~World();
    inline void update_block(Block_pos p, uint16_t delay=0);
    void process_block_update();
    inline Sector* get_sector_ptr(Sector_pos p) const;
    Block *find_block(int x, int y, int z) const;
    void check_neighbors(int x, int y, int z);
    inline Block *find_block(Block_pos p) const;
    inline void set_position(double x, double y, double z);
    void add_operation(std::string op);
    void add_block(int x, int y, int z, uint16_t id, bool auto_process = true, bool block_update = true,
                   const Arg<std::map<std::string, std::string>> &NBT = Arg{false, emptyNBT},
                   Arg<uint8_t> data_val = Arg{false, uint8_t(0)}
                   );
    void add_block_python(int x, int y, int z, uint16_t id);
    void remove_block(const int x, const int y, const int z, bool auto_process = true, bool block_update = true);
    inline void lock_sector_vertex_data_struct_mutex(int x, int y);
    inline void unlock_sector_vertex_data_struct_mutex(int x, int y);
    inline void start_process_sector_thread();
    inline std::string give_operation();
    inline uint16_t get_block(int x, int y, int z) const;
    bool intersect(int entity, double x, double y, double z) const;
    inline uint16_t get_intersect_block(double x, double y, double z) const;
    pybind11::tuple get_friction_and_resistance(int entity, double x, double y, double z) const;
    pybind11::tuple hit_test(double x, double y, double z, double dx, double dy, double dz, double max_distance) const;
    inline pybind11::tuple get_sector_vbo_data_ptr(int x, int y) const;
    inline int get_brightness(int x, int y, int z) const;
    inline int get_max_height(int x, int z) const;
};

// 方块常量定义
struct Block_type {
    std::string_view name;
    uint16_t id;
    uint8_t light_attenuation;    // 光线通过此方块的衰减
    uint8_t light;    // 方块本身的亮度（光源方块）
    std::vector<entity_box> entity_box;
    bool transparent;    // 是否透明
    std::array<uint16_t, 6> texture;
    bool (*update_func)(World*, Block_pos);    // 方块更新函数
    uint16_t schedule_delay;    // 被放置/邻居变化时，多久后触发 update_func
    int friction, resistance;    // 摩擦力和阻力
    uint8_t data_init_val;    // Block::data的初始值
};


bool empty_update_func(World*, Block_pos) {
    return false;
}
bool water_update_func(World* self, Block_pos p) {
    bool flag = false;
    uint8_t this_data = self->find_block(p)->data;
    if (this_data > 0u) {
        uint8_t origin_data = this_data;
        this_data = 10u;
        if (self->find_block(p + Block_pos{0, 1, 0})->id == 9) {
            this_data = 8u;
        } else {
            for (int i = 2; i < 6; ++i) {
                uint8_t from_data = (self->find_block(p + FACES[i])->data == 8u ? 0u : self->find_block(p + FACES[i])->data) + 1u;
                if (self->find_block(p + FACES[i])->id == 9 && from_data < 8u) {
                    this_data = std::min<uint8_t>(this_data, from_data);
                }
            }
        }
        if (this_data >= 9u) {
            self->remove_block(p.x, p.y, p.z, true, false);
            return true;
        }
        if (origin_data != 8u) {
            this_data = std::max(this_data, origin_data);    // 防止不合法的扩散
        }
        self->find_block(p)->data = this_data;
        if (origin_data != this_data) {
            flag = true;
            self->check_neighbors(p.x, p.y, p.z);
            // check_neighbors不会标记update_vbo_data，需要手动设置
            std::set<Sector_pos> updated_vbos;
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dz = -1; dz <= 1; ++dz) {
                    for (int dy = -1; dy <= 0; ++dy) {
                        Block_pos d{dx, dy, dz};
                        updated_vbos.insert(get_sector(p + d));
                    }
                }
            }
            for (const Sector_pos &i : updated_vbos) {
                self->operations.push_back(std::format("update_vbo_data {} {}", i.x, i.y));
            }
        }
    }
    // 向下传播
    if (self->find_block(p + FACES[1])->id == 1 || (self->find_block(p + FACES[1])->id == 9 && self->find_block(p + FACES[1])->data > 0u && self->find_block(p + FACES[1])->data != 8u)) {
        self->add_block(p.x, p.y - 1, p.z, 9, true, false, {false, emptyNBT}, Arg{true, uint8_t(8u)});
        flag = true;
    }
    // 向周围传播
    uint8_t to_data = (this_data == 8u ? 0u :this_data) + 1u;
    if (to_data < 8u && self->find_block(p + FACES[1])->id != 1 && self->find_block(p + FACES[1])->id != 9) {
        for (int i = 2; i < 6; ++i) {
            if (self->find_block(p + FACES[i])->id == 1 || (self->find_block(p + FACES[i])->id == 9 && (self->find_block(p + FACES[i])->data == 8u ? 0u : self->find_block(p + FACES[i])->data) > to_data)) {
                auto [dx, dy, dz] = FACES[i];
                self->add_block(p.x + dx, p.y + dy, p.z + dz, 9, true, false, {false, emptyNBT}, Arg{true, uint8_t(to_data)});
                flag = true;
            }
        }
    }
    return flag;
}

const Block_type block_types[] = {
    {"block.minecraft.sector_not_loaded", 0, 1, 0, {}, false, {0, 0, 0, 0, 0, 0}, &empty_update_func, 0, 2, 2, 0},
    {"block.minecraft.air", 1, 1, 0, {}, true,  {0, 0, 0, 0, 0, 0}, &empty_update_func, 0, 2, 2, 0},
    {"block.minecraft.grass_block", 2, 15, 0, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, false, {0, 2, 1, 1, 1, 1}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.dirt", 3, 15, 0, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, false, {2, 2, 2, 2, 2, 2}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.stone", 4, 15, 0, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, false, {4, 4, 4, 4, 4, 4}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.bedrock", 5, 15, 0, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, false, {3, 3, 3, 3, 3, 3}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.oak_log", 6, 15, 0, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, false, {5, 5, 6, 6, 6, 6}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.oak_leaves", 7, 3, 0, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, true, {7, 7, 7, 7, 7, 7}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.glowstone", 8, 15, 15, {entity_box{-0.5, 0.5, -0.5, 0.5, -0.5, 0.5}}, false, {8, 8, 8, 8, 8, 8}, &empty_update_func, 0, 15, 15, 0},
    {"block.minecraft.water", 9, 1, 0, {}, true, {9, 9, 9, 9, 9, 9}, &water_update_func, 4, 15, 30, 0},    // data为0时为水源，data>=7时不再传播，特别地，data=8时表示由上而下流下的水，可以传播8格但不是水源
};

struct hashless_functions {
    Sector_pos p;
    Sector *sector, *sector01, *sector10, *sector0m, *sectorm0, *sector11, *sectormm, *sector1m, *sectorm1;

    hashless_functions() {}

    hashless_functions(Sector_pos p, Sector *sector, Sector *sector01, Sector *sector10, Sector *sector0m, Sector *sectorm0, Sector *sector11, Sector *sectormm, Sector *sector1m, Sector *sectorm1):
    p(p), sector(sector), sector01(sector01), sector10(sector10), sector0m(sector0m), sectorm0(sectorm0), sector11(sector11), sectormm(sectormm), sector1m(sector1m), sectorm1(sectorm1) {}

    hashless_functions(World *self, Sector_pos p): p(p) {
        sector = self->get_sector_ptr(p);
        sector01 = self->get_sector_ptr(p + Sector_pos{ 0,  1});
        sector10 = self->get_sector_ptr(p + Sector_pos{ 1,  0});
        sector0m = self->get_sector_ptr(p + Sector_pos{ 0, -1});
        sectorm0 = self->get_sector_ptr(p + Sector_pos{-1,  0});
        sector11 = self->get_sector_ptr(p + Sector_pos{ 1,  1});
        sectormm = self->get_sector_ptr(p + Sector_pos{-1, -1});
        sector1m = self->get_sector_ptr(p + Sector_pos{ 1, -1});
        sectorm1 = self->get_sector_ptr(p + Sector_pos{-1,  1});
    }

    Sector *get_sector_ptr(Block_pos bp) const {
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

    Block *find_block(Block_pos bp) const {
        if (bp.y < 0 || bp.y >= 256) {
            return &sector_not_loaded;
        }
        Sector *sp = get_sector_ptr(bp);
        if (sp) {
            return &sp->blocks[get_block_index(bp)];
        } else {
            return &sector_not_loaded;
        }
    };

    inline Block* operator()(Block_pos p) const {
        return find_block(p);
    }
};

void World::stop_all_thread() {
    exit_flag = true;
    while (running_threads) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

Block *World::find_block(int x, int y, int z) const {
    if (y < 0 || y >= 256) {
        return &sector_not_loaded;
    }
    Sector_pos sector = get_sector(x, z);
    if (world.count(sector) == 0) {  // 区块没有加载
        return &sector_not_loaded;
    }
    return &(world.at(sector)->blocks[get_block_index(x, y, z)]);  // world[sector]不是const
}

inline Block *World::find_block(Block_pos p) const {
    return find_block(p.x, p.y, p.z);
}

void World::process_sector_thread() {
    Sector_pos player_sector, now;
    while (!exit_flag) {
        int x = position_x, z = position_z;
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
    --running_threads;
}

void World::init_sectors() {
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
    --running_threads;
}

void World::generate_sector(int dx, int dy) {
    Sector *sector = new Sector;
    Sector_pos p = {dx, dy};
    const std::bitset<16 * 256 * 16> &hole = holes[p];
    //                                整体高度        地形起伏         植被（树）密度      雨量
    // inline int32_t get_biome(float altitude, float fluctuate, float tree_density, float rain)
    std::array<float, 16 * 16> noise_tree_density, noise_rain;
    std::array<float, 18 * 18> noise_terrain, noise_altitude, noise_fluctuate;
    std::array<int, 18 * 18> height1;
    dx *= 16, dy *= 16;
    // float tmp;
    noise->GenUniformGrid2D(noise_altitude.data(), (dx - 1) * 0.05f, (dy - 1) * 0.05f, 18, 18, 0.05f, 0.05f, seed);
    noise->GenUniformGrid2D(noise_fluctuate.data(), (dx - 1) * 0.05f, (dy - 1) * 0.05f, 18, 18, 0.05f, 0.05f, seed + 1);
    terrain_noise_generater->GenUniformGrid2D(noise_terrain.data(), dx - 1, dy - 1, 18, 18, 1.0f, 1.0f, seed);    // 需要生成周边区块的数据以判断height2
    noise->GenUniformGrid2D(noise_tree_density.data(), dx * 0.5f, dy * 0.5f, 16, 16, 0.5f, 0.5f, seed + 2);
    noise->GenUniformGrid2D(noise_rain.data(), dx * 0.1f, dy * 0.1f, 16, 16, 0.1f, 0.1f, seed + 3);
    std::vector<Block_pos> &full_light_calc_blocks = full_light_calc_flag[p];
    // height1: 总体高度（石头+泥土），height2: 泥土和草方块的高度，石头高度为height1 - height2
    auto get_height1 = [](float x) -> float {
        if (x <= 0.5f) {
            return x * 30 + 64;    // 64 ~ 79
        } else if (x <= 0.8f) {
            return x * 120 + 19;    // 79 ~ 115
        } else {
            return x * 50 + 75;    // 115 ~ 125
        }
    };
    auto terrain_fluctuate_calc = [](float n) {
        if (n <= 0.3f) {
            return 0.1f;
        } else if (n <= 0.7f) {
            return n * 3.5f - 0.95f;
        } else {
            return 1.5f;
        }
    };
    for (int z = 0; z != 18; ++z) {
        for (int x = 0; x != 18; ++x) {
            height1[z * 18 + x] = get_height1((noise_terrain[z * 18 + x] + 1.0f) * 0.5f) * terrain_fluctuate_calc((noise_fluctuate[z * 18 + x] + 1.0f) * 0.5f) + noise_altitude[z * 18 + x] * 40;
        }
    }
    auto get_height2 = [&height1](int x, int z) {
        x += 1, z += 1;
        int craggedness = 0;    // 陡峭程度
        for (int i = 2; i < 6; ++i) {
            auto [dx, _, dz] = FACES[i];
            craggedness += abs(height1[(z + dz) * 18 + (x + dx)] - height1[z * 18 + x]);
        }
        return std::max<int>(6 - craggedness * 0.75f, 0);
    };
    for (int x = 0; x != 16; ++x) {
        for (int z = 0; z != 16; ++z) {
            int h1 = height1[(z + 1) * 18 + (x + 1)];
            int h2 = get_height2(x, z);
            sector->blocks[get_block_index(x, 0, z)] = {5, (0 << 4) + 0, 0};  // bedrock
            for (int y = 1; y <= h1 - h2; ++y) {
                sector->blocks[get_block_index(x, y, z)] = hole[get_block_index(x, y, z)] ? air : Block{4, (0 << 4) + 0, 0};  // stone
            }
            for (int y = h1 - h2 + 1; y < h1; ++y) {
                sector->blocks[get_block_index(x, y, z)] = hole[get_block_index(x, y, z)] ? air : Block{3, (0 << 4) + 0, 0};  // dirt
            }
            if (hole[get_block_index(x, h1, z)]) {
                full_light_calc_blocks.push_back({dx + x, h1, dy + z});
                sector->blocks[get_block_index(x, h1, z)] = air;
            } else if (h2) {    // 岩石裸露
                sector->blocks[get_block_index(x, h1, z)] = Block{2, (0 << 4) + 0, 0};    // grass_block
            }
            std::fill(sector->blocks.begin() + get_block_index(x, h1 + 1, z), sector->blocks.begin() + get_block_index(x, 255, z) + 1, Block{1, (15 << 4) + 0, 0});
            sector->data[x * 16 + z] = h1;
            sector->biome[x * 16 + z] = get_biome(noise_altitude[(z + 1) * 18 + (x + 1)], noise_fluctuate[(z + 1) * 18 + (x + 1)], noise_tree_density[z * 16 + x], noise_rain[z * 16 + x]);
        }
    }
    holes.erase(p);
    hashless_functions hf;
    {
        std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
        world[p] = sector;
        hf = hashless_functions(p, sector,
            world.find(p + Sector_pos{ 0,  1}) != world.end() ? world.at(p + Sector_pos{ 0,  1}) : nullptr,
            world.find(p + Sector_pos{ 1,  0}) != world.end() ? world.at(p + Sector_pos{ 1,  0}) : nullptr,
            world.find(p + Sector_pos{ 0, -1}) != world.end() ? world.at(p + Sector_pos{ 0, -1}) : nullptr,
            world.find(p + Sector_pos{-1,  0}) != world.end() ? world.at(p + Sector_pos{-1,  0}) : nullptr,
            nullptr, nullptr, nullptr, nullptr
        );
    }
    // 让矿洞中的光跨区块传播
    Block* block;
    if (hf.sector10) {
        for (int z = 0; z != 16; ++z) {
            for (int y = 0; y <= sector->data[15 * 16 + z]; ++y) {
                block = hf.find_block({16 + dx, y, z + dy});
                if (block_types[sector->blocks[(15 * 16 + z) * 256 + y].id].transparent && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                    full_light_calc_blocks.push_back({15 + dx, y, z + dy});
                }
            }
        }
    }
    if (hf.sector01) {
        for (int x = 0; x != 16; ++x) {
            for (int y = 0; y <= sector->data[x * 16 + 15]; ++y) {
                block = hf.find_block({x + dx, y, dy + 16});
                if (block_types[sector->blocks[(x * 16 + 15) * 256 + y].id].transparent && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                    full_light_calc_blocks.push_back({x + dx, y, dy + 15});
                }
            }
        }
    }
    if (hf.sectorm0) {
        for (int z = 0; z != 16; ++z) {
            for (int y = 0; y <= sector->data[z]; ++y) {
                block = hf.find_block({-1 + dx, y, z + dy});
                if (block_types[sector->blocks[z * 256 + y].id].transparent && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                    full_light_calc_blocks.push_back({dx, y, z + dy});
                }
            }
        }
    }
    if (hf.sector0m) {
        for (int x = 0; x != 16; ++x) {
            for (int y = 0; y <= sector->data[x * 16]; ++y) {
                block = hf.find_block({x + dx, y, dy - 1});
                if (block_types[sector->blocks[x * 16 * 256 + y].id].transparent && ((block->light & 15) >= 2 || (block->light & 240) >= 32)) {
                    full_light_calc_blocks.push_back({x + dx, y, dy});
                }
            }
        }
    }
}

bool World::has_tree(int x, int z, float tree_calc_value) {
    float hash = hash2D(x, z, seed);
    if (hash <= 1.0f - tree_calc_value) {
        return false;
    }
    for (int dx = -3; dx <= 3; ++dx) {
        for (int dz = -3; dz <= 3; ++dz) {
            if (dx == 0 && dz == 0) {
                continue;
            }
            if (dx * dx + dz * dz <= 9 && hash2D(x + dx, z + dz, seed) >= hash) {
                return false;
            }
        }
    }
    return true;
}

void World::generate_tree(int sx, int sy) {
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

template<typename F> inline uint64_t exposed_invisible_block(const F&, int, int, int) {
    return 0ull;
}

template<typename F> uint64_t exposed_solid_block(const F &find_block, int x, int y, int z) {
    uint64_t res = 0ull;
    Block_pos p = {x, y, z};
    Block *cur = find_block(p), *neighbor;
    for (int i = 0, dx, dy, dz; i < 6; ++i) {
        dx = FACES[i].x, dy = FACES[i].y, dz = FACES[i].z;
        neighbor = find_block(p + FACES[i]);
        // 同样的透明方块之间的面一个隐藏，一个显示
        if (y + dy < 0 || y + dy >= 256) {
            res |= (1ull << i);
        } else if (block_types[cur->id].transparent && (!block_types[neighbor->id].transparent)) {
            res |= (1ull << i);
        } else if (block_types[neighbor->id].transparent && neighbor->id != cur->id) {
            res |= (1ull << i);
        } else if (block_types[neighbor->id].transparent && neighbor->id == cur->id && (dx == 1 || dy == 1 || dz == 1)) {
            res |= (1ull << i);
        }
    }
    return res;
}

template<typename F> uint64_t exposed_water(const F &find_block, int x, int y, int z) {
    uint64_t res = 0ull;
    Block_pos p = {x, y, z};
    Block *neighbor;
    for (int i = 0, dy; i < 6; ++i) {
        dy = FACES[i].y;
        neighbor = find_block(p + FACES[i]);
        if (y + dy < 0 || y + dy >= 256) {
            res |= (1ull << i);
        } else if (neighbor->id == 1) {
            res |= (1ull << i);
        }
    }
    if (find_block(p)->data != 0 && find_block(p)->data != 8) {
        res |= 1ull;
    }
    return res;
}

template<typename F> uint64_t World::exposed(int x, int y, int z, const F& find_block_func) const {
    using fn_t = uint64_t(*)(const F&, int, int, int);
    static constexpr fn_t table[] = {
        &exposed_invisible_block<F>,    // 0
        &exposed_invisible_block<F>,    // 1
        &exposed_solid_block<F>,        // 2
        &exposed_solid_block<F>,        // 3
        &exposed_solid_block<F>,        // 4
        &exposed_solid_block<F>,        // 5
        &exposed_solid_block<F>,        // 6
        &exposed_solid_block<F>,        // 7
        &exposed_solid_block<F>,        // 8
        &exposed_water<F>,              // 9
    };
    return table[find_block_func({x, y, z})->id](find_block_func, x, y, z);
}

template<typename F> uint64_t World::exposed(Block_pos p, const F& find_block_func) const {
    return exposed(p.x, p.y, p.z, find_block_func);
}

inline uint64_t World::exposed(int x, int y, int z) const {
    return exposed(x, y, z, [this](const Block_pos &p) -> Block* { return this->find_block(p); });
}

inline uint64_t World::exposed(Block_pos p) const {
    return exposed(p.x, p.y, p.z);
}

void World::check_exposed_blocks(int dx, int dy) {
    Sector_pos p = {dx, dy};
    hashless_functions hf(this, p);
    dx *= 16, dy *= 16;
    auto update_shown_without_hash = [this, &hf](int x, int y, int z, uint64_t shown) {
        if (shown == 0ull) {
            return;
        }
        Sector *sector = hf.get_sector_ptr({x, y, z});
        uint64_t mask;
        int index = get_block_index(x, y, z);
        uint16_t change_width;
        change_width = std::bit_width(shown);
        sector->shown[index].second.resize(change_width);
        std::array<float, 4 * 7> vertex_texture_data;
        for (uint16_t i = 0; i != change_width; ++i) {
            mask = 1ull << i;
            if (shown & mask) {
                this->get_tex_array_data(Block_pos{x, y, z}, i, vertex_texture_data, hf);
                sector->shown[index].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
            }
        }
        sector->shown[index].first = shown;
    };
    hf.sector->vertex_data_struct_mutex.lock();
    if (hf.sector01) hf.sector01->vertex_data_struct_mutex.lock();
    if (hf.sector10) hf.sector10->vertex_data_struct_mutex.lock();
    if (hf.sector0m) hf.sector0m->vertex_data_struct_mutex.lock();
    if (hf.sectorm0) hf.sectorm0->vertex_data_struct_mutex.lock();
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y <= hf.sector->data[x * 16 + z]; ++y) {
                update_shown_without_hash(dx + x, y, z + dy, exposed(dx + x, y, z + dy, hf));
            }
        }
    }
    auto update_shown_full_without_hash = [this, &hf](int x, int y, int z, uint64_t shown) {
        Sector* sector = hf.get_sector_ptr({x, y, z});
        uint64_t mask;
        int index = get_block_index(x, y, z);
        uint16_t change_width;
        change_width = std::bit_width(shown ^ sector->shown[index].first);
        sector->shown[index].second.resize(std::max(std::bit_width(sector->shown[index].first), std::bit_width(shown)));
        std::array<float, 4 * 7> vertex_texture_data;
        for (uint16_t i = 0; i != change_width; ++i) {
            mask = 1ull << i;
            if ((shown & mask) != (sector->shown[index].first & mask)) {
                if (shown & mask) {
                    this->get_tex_array_data(Block_pos{x, y, z}, i, vertex_texture_data, hf);
                    sector->shown[index].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
                } else {
                    sector->vertex_data_struct.erase(sector->shown[index].second[i]);
                }
            }
        }
        sector->shown[index].first = shown;
        sector->shown[index].second.resize(std::bit_width(shown));
    };
    if (hf.sector11) hf.sector11->vertex_data_struct_mutex.lock();
    if (hf.sectormm) hf.sectormm->vertex_data_struct_mutex.lock();
    if (hf.sector1m) hf.sector1m->vertex_data_struct_mutex.lock();
    if (hf.sectorm1) hf.sectorm1->vertex_data_struct_mutex.lock();
    if (hf.sector01) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 0; y <= hf.sector01->data[x * 16]; ++y) {
                update_shown_full_without_hash(dx + x, y, 16 + dy, exposed(dx + x, y, 16 + dy, hf));
            }
        }
    }
    if (hf.sector10) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y <= hf.sector10->data[z]; ++y) {
                update_shown_full_without_hash(dx + 16, y, z + dy, exposed(dx + 16, y, z + dy, hf));
            }
        }
    }
    if (hf.sector0m) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 0; y <= hf.sector0m->data[x * 16 + 15]; ++y) {
                update_shown_full_without_hash(dx + x, y, -1 + dy, exposed(dx + x, y, -1 + dy, hf));
            }
        }
    }
    if (hf.sectorm0) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 0; y <= hf.sectorm0->data[15 * 16 + z]; ++y) {
                update_shown_full_without_hash(dx - 1, y, z + dy, exposed(dx - 1, y, z + dy, hf));
            }
        }
    }
    hf.sector->vertex_data_struct_mutex.unlock();
    if (hf.sector01) hf.sector01->vertex_data_struct_mutex.unlock();
    if (hf.sector10) hf.sector10->vertex_data_struct_mutex.unlock();
    if (hf.sector0m) hf.sector0m->vertex_data_struct_mutex.unlock();
    if (hf.sectorm0) hf.sectorm0->vertex_data_struct_mutex.unlock();
    if (hf.sector11) hf.sector11->vertex_data_struct_mutex.unlock();
    if (hf.sectormm) hf.sectormm->vertex_data_struct_mutex.unlock();
    if (hf.sector1m) hf.sector1m->vertex_data_struct_mutex.unlock();
    if (hf.sectorm1) hf.sectorm1->vertex_data_struct_mutex.unlock();
}

// 洞穴最大长度：64, 每次以玩家为圆心，simulate_distance +
// 5为半径，在圆上每个区块（随机高度）生成洞穴。
void World::generate_holes(float x, float y, float z) {
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
        for (int bx = -size_int; bx <= size_int; ++bx) {
            for (int by = -size_int; by <= size_int; ++by) {
                for (int bz = -size_int; bz <= size_int; ++bz) {
                    if (bx * bx + by * by + bz * bz <= size_float * size_float) {
                        final_x = x_int + bx, final_y = y_int + by, final_z = z_int + bz;
                        if (final_y <= 0 || final_y >= 256)
                            break;
                        holes[get_sector(final_x, final_z)][get_block_index(final_x, final_y, final_z)] = false;
                    }
                }
            }
        }
    }
}

std::vector<Block_pos> World::calc_light(int x, int y, int z) {
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
        final_light = std::max<uint8_t>(block_types[find_block(now)->id].light, tmp_light - std::min<uint8_t>(tmp_light, block_types[find_block(now)->id].light_attenuation));
        if ((find_block(now)->light & 15) != final_light) {
            find_block(now)->light = final_light + (find_block(now)->light & 240);
            for (int i = 0; i < 6; ++i) {
                neighbor = find_block(now + FACES[i]);
                if (neighbor->id == 0) {
                    continue;
                }
                if (block_types[neighbor->id].transparent) {
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
        final_light = tmp_light - std::min<uint8_t>(tmp_light, 16 * block_types[find_block(now)->id].light_attenuation);
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
                if (block_types[neighbor->id].transparent) {
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

inline std::vector<Block_pos> World::calc_light(Block_pos p) {
    return calc_light(p.x, p.y, p.z);
}

void World::calc_sector_light(int dx, int dy) {
    Sector_pos p = {dx, dy};
    // 这里不需要加锁，unordered_map.at操作是线程安全的，
    // 调用calc_sector_light时不会有别的线程访问这个区块(见process_sector_thread)
    const std::vector<Block_pos> &full_light_calc_blocks = full_light_calc_flag.at(p);
    std::vector<Block_pos> updated;
    for (const Block_pos &i : full_light_calc_blocks) {
        updated = calc_light(i);
        for (const Block_pos &j : updated) {
            if (get_sector(j) != p) {
                update_shown(j, exposed(j));
                update_shown(j, exposed(j), true);
            }
        }
    }
    full_light_calc_flag.erase(p);
}

void World::check_neighbors(int x, int y, int z) {
    Block_pos p = {x, y, z};
    for (int i = 0; i < 6; ++i) {
        update_shown(p + FACES[i], exposed(p + FACES[i]));
    }
    // if (find_block(p)->id == 9) {    // 不能用这个，remove_block中id已经不是9了
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 0; ++dy) {
                Block_pos d{dx, dy, dz};
                if (find_block(p + d)->id == 9) {
                    update_shown(p + d, exposed(p + d));
                    update_shown(p + d, exposed(p + d), true);
                }
            }
        }
    }
}

void World::update_shown(int x, int y, int z, uint64_t shown, bool force_update) {
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
                get_tex_array_data(Block_pos{x, y, z}, i, vertex_texture_data);
                sector->vertex_data_struct.change_data(sector->shown[index].second[i], vertex_texture_data);
            }
        } else if ((shown & mask) != (sector->shown[index].first & mask)) {
            if (shown & mask) {
                get_tex_array_data(Block_pos{x, y, z}, i, vertex_texture_data);
                sector->shown[index].second[i] = sector->vertex_data_struct.add(vertex_texture_data);
            } else {
                sector->vertex_data_struct.erase(sector->shown[index].second[i]);
            }
        }
    }
    sector->shown[index].first = shown;
    sector->shown[index].second.resize(std::bit_width(shown));
}

inline void World::update_shown(Block_pos p, uint64_t shown, bool force_update) {
    update_shown(p.x, p.y, p.z, shown, force_update);
}


template<typename F> void get_vertex_data_normal_block(const F& find_block, Block_pos p, int face_index, std::array<float, 4 * 7> &res) {
    uint16_t id = find_block(p)->id;
    int img_idx = block_types[id].texture[face_index];
    std::array<float, 13> vertices;
    auto [x, y, z] = p;
    constexpr float n = 0.5;
    switch (face_index) {
        case 0: vertices = {x - n, y + n, z - n, x - n, y + n, z + n, x + n, y + n, z + n, x + n, y + n, z - n, 3.0f}; break;    // top
        case 1: vertices = {x - n, y - n, z - n, x + n, y - n, z - n, x + n, y - n, z + n, x - n, y - n, z + n, 4.0f}; break;    // bottom
        case 2: vertices = {x - n, y - n, z - n, x - n, y - n, z + n, x - n, y + n, z + n, x - n, y + n, z - n, 2.0f}; break;    // left
        case 3: vertices = {x + n, y - n, z + n, x + n, y - n, z - n, x + n, y + n, z - n, x + n, y + n, z + n, 1.0f}; break;    // right
        case 4: vertices = {x - n, y - n, z + n, x + n, y - n, z + n, x + n, y + n, z + n, x - n, y + n, z + n, 5.0f}; break;    // front
        case 5: vertices = {x + n, y - n, z - n, x - n, y - n, z - n, x - n, y + n, z - n, x + n, y + n, z - n, 6.0f}; break;    // back
    }
    uint8_t light;
    for (uint32_t i = 0, j = 0, brightness; i != 4 * 7; i += 7, j += 3) {
        res[i] = vertices[j];
        res[i + 1] = vertices[j + 1];
        res[i + 2] = vertices[j + 2];
        res[i + 3] = uvs[j];
        res[i + 4] = uvs[j + 1];
        res[i + 5] = img_idx;
        Block_pos normal = normals[int(vertices[12] + 0.5f)];    // 这里加上0.5作为四舍五入
        if ((p + normal).y == 256) {
            light = 15 << 4;
        } else if (block_types[find_block(p)->id].transparent && (!block_types[find_block(p + normal)->id].transparent)) {
            light = find_block(p)->light;
        } else {
            light = find_block(p + normal)->light;
        }
        brightness = std::max(light >> 4, light & 15);
        res[i + 6] = brightness * 7 + vertices[12];
    }
}

template<typename F> void get_vertex_data_water(const F& find_block, Block_pos p, int face_index, std::array<float, 4 * 7> &res) {
    int img_idx = block_types[9].texture[face_index];
    std::array<float, 13> vertices;
    auto [x, y, z] = p;
    constexpr float n = 0.5;
    auto get_power = [&find_block, p](const Block_pos &d) {
        Block* block = find_block(p + d);
        uint16_t up = find_block(p + d + Block_pos{0, 1, 0})->id;
        // 这里-1024的作用是如果水源上方有方块，强制设为一整个（若上面无方块，是15像素而不是16像素）
        return std::tuple<float, uint8_t>{(block->id == 9 ? (up == 0 || up == 1 || (block->data != 0 && block->data != 8) ? float(block->data == 8u ? 0u : block->data) : -1024.0f) : 0.0f), uint8_t(block->id == 9)};
    };
    auto [power00, iswater00] = get_power(Block_pos{ 0, 0, 0});
    auto [power01, iswater01] = get_power(Block_pos{ 0, 0, 1});
    auto [power10, iswater10] = get_power(Block_pos{ 1, 0, 0});
    auto [power0m, iswater0m] = get_power(Block_pos{ 0, 0,-1});
    auto [powerm0, iswaterm0] = get_power(Block_pos{-1, 0, 0});
    auto [power11, iswater11] = get_power(Block_pos{ 1, 0, 1});
    auto [powermm, iswatermm] = get_power(Block_pos{-1, 0,-1});
    auto [power1m, iswater1m] = get_power(Block_pos{ 1, 0,-1});
    auto [powerm1, iswaterm1] = get_power(Block_pos{-1, 0, 1});
    float airheight11 = std::max((power00 + power01 + power10 + power11) / float(iswater00 + iswater01 + iswater10 + iswater11) / 8.0f + 1.0f / 16.0f, 0.0f);
    float airheight1m = std::max((power00 + power0m + power10 + power1m) / float(iswater00 + iswater0m + iswater10 + iswater1m) / 8.0f + 1.0f / 16.0f, 0.0f);
    float airheightmm = std::max((power00 + power0m + powerm0 + powermm) / float(iswater00 + iswater0m + iswaterm0 + iswatermm) / 8.0f + 1.0f / 16.0f, 0.0f);
    float airheightm1 = std::max((power00 + power01 + powerm0 + powerm1) / float(iswater00 + iswater01 + iswaterm0 + iswaterm1) / 8.0f + 1.0f / 16.0f, 0.0f);
    switch (face_index) {
        case 0: vertices = {x - n, y + n - airheightmm, z - n, x - n, y + n - airheightm1, z + n, x + n, y + n - airheight11, z + n, x + n, y + n - airheight1m, z - n, 3.0f}; break;    // top
        case 1: vertices = {x - n, y - n, z - n, x + n, y - n, z - n, x + n, y - n, z + n, x - n, y - n, z + n, 4.0f}; break;    // bottom
        case 2: vertices = {x - n, y - n, z - n, x - n, y - n, z + n, x - n, y + n - airheightm1, z + n, x - n, y + n - airheightmm, z - n, 2.0f}; break;    // left
        case 3: vertices = {x + n, y - n, z + n, x + n, y - n, z - n, x + n, y + n - airheight1m, z - n, x + n, y + n - airheight11, z + n, 1.0f}; break;    // right
        case 4: vertices = {x - n, y - n, z + n, x + n, y - n, z + n, x + n, y + n - airheight11, z + n, x - n, y + n - airheightm1, z + n, 5.0f}; break;    // front
        case 5: vertices = {x + n, y - n, z - n, x - n, y - n, z - n, x - n, y + n - airheightmm, z - n, x + n, y + n - airheight1m, z - n, 6.0f}; break;    // back
    }
    uint8_t light;
    for (uint32_t i = 0, j = 0, brightness; i != 4 * 7; i += 7, j += 3) {
        res[i] = vertices[j];
        res[i + 1] = vertices[j + 1];
        res[i + 2] = vertices[j + 2];
        res[i + 3] = uvs[j];
        res[i + 4] = uvs[j + 1];
        res[i + 5] = img_idx;
        Block_pos normal = normals[int(vertices[12] + 0.5f)];    // 这里加上0.5作为四舍五入
        if ((p + normal).y == 256) {
            light = 15 << 4;
        } else if (block_types[find_block(p)->id].transparent && (!block_types[find_block(p + normal)->id].transparent)) {
            light = find_block(p)->light;
        } else {
            light = find_block(p + normal)->light;
        }
        brightness = std::max(light >> 4, light & 15);
        res[i + 6] = brightness * 7 + vertices[12];
    }
}

template<typename F> inline void World::get_tex_array_data(Block_pos p, int face_index, std::array<float, 4 * 7> &res, const F& find_block_func) const {
    using fn_t = void(*)(const F&, Block_pos, int, std::array<float, 4 * 7>&);
    static constexpr fn_t table[] = {
        &get_vertex_data_normal_block<F>,    // 0
        &get_vertex_data_normal_block<F>,    // 1
        &get_vertex_data_normal_block<F>,    // 2
        &get_vertex_data_normal_block<F>,    // 3
        &get_vertex_data_normal_block<F>,    // 4
        &get_vertex_data_normal_block<F>,    // 5
        &get_vertex_data_normal_block<F>,    // 6
        &get_vertex_data_normal_block<F>,    // 7
        &get_vertex_data_normal_block<F>,    // 8
        &get_vertex_data_water<F>,           // 9
    };
    table[find_block_func(p)->id](find_block_func, p, face_index, res);
}

inline void World::get_tex_array_data(Block_pos p, int face_index, std::array<float, 4 * 7> &res) const {
    get_tex_array_data(p, face_index, res, [this](const Block_pos &p) -> Block* { return this->find_block(p); });
}


inline void World::update_block(Block_pos p, uint16_t delay) {
    std::lock_guard<std::recursive_mutex> lock_block_updates(block_updates_mutex);
    block_updates.push_back({p, delay, find_block(p)->id});
}

void World::process_block_update() {
    std::vector<block_update_entry> now;
    for (size_t i = 0; i < block_updates.size(); ++i) {
        auto [p, delay, block] = block_updates[i];
        if (block == find_block(p)->id) {
            if (delay) {
                now.push_back({p, (uint16_t)(delay - 1), block});
            } else {
                if (block_types[find_block(p)->id].update_func(this, p)) {
                    for (int j = 0; j < 6; ++j) {
                        now.push_back({p + FACES[j], block_types[find_block(p + FACES[j])->id].schedule_delay, find_block(p + FACES[j])->id});
                    }
                }
            }
        }
    }
    block_updates = now;
}

World::World(int seed_arg, int simulate_distance_arg) {
    running_threads = 0;
    exit_flag = false;
    noise = FastNoise::NewFromEncodedNodeTree("DQkR@BSEMJBw@AEhDBAMAAMhCDA==");
    terrain_noise_generater = FastNoise::NewFromEncodedNodeTree("EQ@APpDCQ0AB@CkH@BSEMMAwAAyEIE");
    seed = seed_arg;
    simulate_distance = simulate_distance_arg + 2;
    for (int x = -simulate_distance; x <= simulate_distance; ++x)
        for (int y = -simulate_distance; y <= simulate_distance; ++y)
            if (x * x + y * y <= simulate_distance * simulate_distance)
                simulate_sectors.push_back({x, y});
    for (int x = -simulate_distance - 5; x <= simulate_distance + 5; ++x)
        for (int y = -simulate_distance - 5; y <= simulate_distance + 5; ++y)
            if (x * x + y * y <= (simulate_distance + 5) * (simulate_distance + 5))
                generate_holes_sectors.push_back({x, y});
    std::unordered_map<Sector_pos, int, Sector_pos_hash> cnt;
    for (Sector_pos i : simulate_sectors) {
        for (int j = 0; j < 8; ++j) {
            ++cnt[i + SECTOR_FACES[j]];
        }
    }
    for (const auto &pair : cnt) {
        if (pair.second == 8) {
            decorate_sectors.push_back(pair.first);
        }
    }
    world.reserve(simulate_sectors.size() * 2);
    holes.reserve(generate_holes_sectors.size() * 2);
    ++running_threads;
    std::thread t(&World::init_sectors, this);
    t.detach();
};

World::~World() {
    stop_all_thread();
    for (auto &pair : world)
        delete pair.second;
}

inline Sector* World::get_sector_ptr(Sector_pos p) const {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    return world.count(p) ? world.at(p) : nullptr;
}

inline void World::set_position(double x, double y, double z) {
    position_x = x, position_y = y, position_z = z;
}

void World::add_operation(std::string op) {
    std::lock_guard<std::recursive_mutex> lock_operations(operations_mutex);
    operations.push_back(op);
}

void World::add_block(int x, int y, int z, uint16_t id, bool auto_process, bool block_update, const Arg<std::map<std::string, std::string>> &NBT, Arg<uint8_t> data_val) {
    if (auto_process) {
        world_mutex.lock();
    }
    Block *block = find_block(x, y, z);
    if (block->id == 0) goto ret;
    if (block->id != 1) {
        if (auto_process) {
            remove_block(x, y, z, false, false);
        } else {
            goto ret;
        }
    }
    block->id = id;
    block->light = (0 << 4) + block_types[id].light;
    block->data = data_val.has_value ? data_val.value : block_types[id].data_init_val;
    if (world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)] < y) {
        world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)] = y;
    }
    if (NBT.has_value) {
        world.at(get_sector(x, z))->NBTs[{mod16(x), y, mod16(z)}] = NBT.value;
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
        update_shown(x, y, z, exposed(x, y, z), true);
        for (int i = 0; i < 6; ++i) {
            updated_brightness_blocks = calc_light(Block_pos{x, y, z} + FACES[i]);
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
        if (block_update) {
            update_block({x, y, z}, block_types[id].schedule_delay);
            for (int i = 0; i < 6; ++i) {
                update_block(Block_pos{x, y, z} + FACES[i], block_types[find_block(x, y, z)->id].schedule_delay);
            }
        }
        for (const Sector_pos &i : updated_vbos) {
            operations.push_back(std::format("update_vbo_data {} {}", i.x, i.y));
        }
        return;
    }
    ret:
    if (auto_process) {
        world_mutex.unlock();
    }
    return;
}

void World::add_block_python(int x, int y, int z, uint16_t id) {
    Pos position_local = {position_x, position_y, position_z};
    for (const entity_box &a : block_types[id].entity_box) {
        for (const entity_box &b : entity_entity_boxes[0]) {
            if (AABB(a, b, {(double)x, (double)y, (double)z}, position_local)) {
                return;
            }
        }
    }
    add_block(x, y, z, id);
}

void World::remove_block(const int x, const int y, const int z, bool auto_process, bool block_update) {
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
        if (block_update) {
            for (int i = 0; i < 6; ++i) {
                update_block(Block_pos{x, y, z} + FACES[i], block_types[find_block(Block_pos{x, y, z} + FACES[i])->id].schedule_delay);
            }
        }
        for (const Sector_pos &i : updated_vbos) {
            operations.push_back(std::format("update_vbo_data {} {}", i.x, i.y));
        }
    }
}

inline void World::lock_sector_vertex_data_struct_mutex(int x, int y) {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    world.at({x, y})->vertex_data_struct_mutex.lock();
}

inline void World::unlock_sector_vertex_data_struct_mutex(int x, int y) {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    world.at({x, y})->vertex_data_struct_mutex.unlock();
}

inline void World::start_process_sector_thread() {
    ++running_threads;
    std::thread t(&World::process_sector_thread, this);
    t.detach();
}

inline std::string World::give_operation() {
    std::lock_guard<std::recursive_mutex> lock(operations_mutex);
    if (operations.empty())
        return "None";
    std::string res = operations.front();
    operations.pop_front();
    return res;
}

inline uint16_t World::get_block(int x, int y, int z) const {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    return find_block(x, y, z)->id;
}

bool World::intersect(int entity, double x, double y, double z) const {
    /* 如果与方块碰撞，返回true，第二个参数是所有碰撞的方块的id */
    int ix = x, iy = y, iz = z;
    int nx, ny, nz;
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 3; ++dy) {
            for (int dz = -2; dz <= 2; ++dz) {
                nx = ix + dx, ny = iy + dy, nz = iz + dz;
                if (ny >= 256 || ny < 0) {
                    continue;
                }
                for (entity_box i : entity_entity_boxes[entity]) {
                    for (entity_box j : block_types[find_block(nx, ny, nz)->id].entity_box) {
                        if (AABB(i, j, {x, y, z}, {(double)nx, (double)ny, (double)nz})) {
                            return true;
                        }
                    }
                }
            }
        }
    }
    return false;
}

inline uint16_t World::get_intersect_block(double x, double y, double z) const {
    /* 返回碰撞的方块的id */
    return get_block(round(x), round(y), round(z));
}

pybind11::tuple World::get_friction_and_resistance(int entity, double x, double y, double z) const {
    /* 获取最大摩擦和阻力系数 */
    int ix = x, iy = y, iz = z;
    int nx, ny, nz;
    int ans_friction = 0, ans_resistance = 0;
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    entity_box j = {-0.5, 0.5, -0.5, 0.5, -0.5, 0.5};
    for (int dx = -2; dx <= 2; ++dx) {
        for (int dy = -2; dy <= 3; ++dy) {
            for (int dz = -2; dz <= 2; ++dz) {
                nx = ix + dx, ny = iy + dy, nz = iz + dz;
                if (ny >= 256 || ny < 0)
                    continue;
                for (entity_box i : entity_entity_boxes[entity]) {
                    i.maxx += 0.01, i.minx -= 0.01;
                    i.maxy += 0.01, i.miny -= 0.01;
                    i.maxz += 0.01, i.minz -= 0.01;
                    if (AABB(i, j, {x, y, z}, {(double)nx, (double)ny, (double)nz})) {
                        uint16_t id = find_block(nx, ny, nz)->id;
                        ans_friction = std::max(ans_friction, block_types[id].friction);
                        ans_resistance = std::max(ans_resistance, block_types[id].resistance);
                        break;
                    }
                }
            }
        }
    }
    return pybind11::make_tuple(ans_friction, ans_resistance);
}

pybind11::tuple World::hit_test(double x, double y, double z, double dx, double dy, double dz, double max_distance) const {
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

inline pybind11::tuple World::get_sector_vbo_data_ptr(int x, int y) const {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    // 第一个是指针，第二个是长度
    return pybind11::make_tuple(reinterpret_cast<uintptr_t>(world.at({x, y})->vertex_data_struct.get_data_ptr(0)), world.at({x, y})->vertex_data_struct.size());
}

inline int World::get_brightness(int x, int y, int z) const {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    return find_block(x, y, z)->light;
}

inline int World::get_max_height(int x, int z) const {
    std::lock_guard<std::recursive_mutex> lock_world(world_mutex);
    if (world.count(get_sector(x, z))) {
        return world.at(get_sector(x, z))->data[mod16(x) * 16 + mod16(z)];
    }
    return 0;
}

PYBIND11_MODULE(MCworld, m) {
    using namespace pybind11::literals;  // 引入 _a 字面量，让代码更可读
    pybind11::class_<World>(m, "World")
        .def(pybind11::init<int, int>(), pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("start_process_sector_thread",                 &World::start_process_sector_thread, pybind11::call_guard<pybind11::gil_scoped_release>())
        .def("give_operation",                              &World::give_operation)
        .def("get_block",                                   &World::get_block)
        .def("add_block",                                   &World::add_block_python)
        .def("remove_block",                                &World::remove_block, "x"_a, "y"_a, "z"_a, "auto_process"_a = true, "block_update"_a = true)
        .def("intersect",                                   &World::intersect)
        .def("get_intersect_block",                         &World::get_intersect_block)
        .def("get_friction_and_resistance",                 &World::get_friction_and_resistance)
        .def("hit_test",                                    &World::hit_test)
        .def("set_position",                                &World::set_position)
        .def("add_operation",                               &World::add_operation)
        .def("get_sector_vbo_data_ptr",                     &World::get_sector_vbo_data_ptr)
        .def("lock_sector_vertex_data_struct_mutex",        &World::lock_sector_vertex_data_struct_mutex)
        .def("unlock_sector_vertex_data_struct_mutex",      &World::unlock_sector_vertex_data_struct_mutex)
        .def("get_brightness",                              &World::get_brightness)
        .def("get_max_height",                              &World::get_max_height)
        .def("process_block_update",                        &World::process_block_update);
}
