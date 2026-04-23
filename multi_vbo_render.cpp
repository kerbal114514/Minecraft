#include <pybind11/pybind11.h>
#include <GL/glew.h>
#include <GL/gl.h>
#include <set>
#include <map>
using namespace std;

void init()
{
    glewInit();
}

unordered_set<unsigned int> vao_ids;
unordered_map<unsigned int, unsigned int> vbo_size;

void add_vao_id(unsigned int x)
{
    vao_ids.insert(x);
}

void delete_vao_id(unsigned int x)
{
    vao_ids.erase(x);
}

void set_vbo_size(unsigned int id, unsigned int size)
{
    vbo_size[id] = size;
}

void draw()
{
    for (unsigned int i : vao_ids)
    {
        glBindVertexArray(i);
        glDrawArrays(GL_QUADS, 0, vbo_size[i]);
    }
    glBindVertexArray(0);
}

PYBIND11_MODULE(multi_vbo_render, m)
{
    m.def("init", &init);
    m.def("add_vao_id", &add_vao_id);
    m.def("delete_vao_id", &delete_vao_id);
    m.def("set_vbo_size", &set_vbo_size);
    m.def("draw", &draw);
}
