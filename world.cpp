/*
   Copyright 2018 Brad Grantham
   Modifications copyright 2018 Jesse Barker
   
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/

#include <memory>
#include <limits>
#include <string>
#include <thread>
#include <iostream>
#include <cerrno>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <cstdint>
#include "triangle-set.h"
#include "geometry.h"
#include "obj-support.h"
#include "trisrc-support.h"
#include "group.h"
#include "bvh.h"
#include "world.h"

struct scoped_FILE
{
    FILE *fp;
    scoped_FILE(FILE *fp_) : fp(fp_) {}
    ~scoped_FILE() {if(fp) fclose(fp);}
    operator FILE*() { return fp; }
};

world *load_world(const std::string& filename) // Get world and return pointer.
{
    std::auto_ptr<world> w(new world);
    w->triangles = std::make_shared<triangle_set>();

    int index = filename.find_last_of(".");
    std::string extension = filename.substr(index + 1);

    bool success = false;

    auto then = std::chrono::system_clock::now();

    if(extension == "trisrc") {

        scoped_FILE fp(fopen(filename.c_str(), "r"));
        if(fp == nullptr) {
            std::cerr << "Cannot open \"" << filename << "\" for input, errno " << errno << "\n";
            return nullptr;
        }

        success = ParseTriSrc(fp, w->triangles);

        if(!success) {
            fprintf(stderr, "Couldn't parse triangles from file.\n");
            return nullptr;
        }

    } else if(extension == "obj") {

        Obj obj;
        if (!obj.load_object_from_file(filename))
        {
            std::cerr << "Cannot open \"" << filename << "\" for input, errno " << errno << "\n";
            return nullptr;
        }
        success = obj.fill_triangle_set(w->triangles);

        if(!success) {
            fprintf(stderr, "Couldn't parse triangles from file.\n");
            return nullptr;
        }

    } else {

        std::cerr << "This program doesn't know how to load a file with extension " + extension << "\n";
        return nullptr;

    }

    auto now = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed = now - then;
    fprintf(stderr, "Parsing: %f seconds\n", elapsed.count());

    w->triangle_count = w->triangles->triangles.size();
    fprintf(stderr, "%d triangles.\n", w->triangle_count);
    fprintf(stderr, "%zd independent vertices.\n", w->triangles->vertices.size());
    fprintf(stderr, "%.2f vertices per triangle.\n", w->triangles->vertices.size() * 1.0 / w->triangle_count);

    then = std::chrono::system_clock::now();

    w->scene_center = w->triangles->box.center();

    float scene_extent_squared = 0;
    for(int i = 0; i < w->triangle_count; i++) {
        triangle t = w->triangles->get(i);
        for(int j = 0; j < 3; j++) {
            vec3 to_center = w->scene_center - t.v[j];
            float distance_squared = dot(to_center, to_center);
            scene_extent_squared = std::max(scene_extent_squared, distance_squared);
        }
    }
    w->scene_extent = sqrtf(scene_extent_squared) * 2;

    now = std::chrono::system_clock::now();
    elapsed = now - then;
    fprintf(stderr, "Finding scene center and extent: %f seconds\n", elapsed.count());

    then = std::chrono::system_clock::now();
    w->root = make_bvh(w->triangles, 0, w->triangle_count);

    now = std::chrono::system_clock::now();
    elapsed = now - then;

    fprintf(stderr, "BVH: %f seconds\n", elapsed.count());

    print_bvh_stats();

    return w.release();
}

int get_node_count(group *g)
{
    int count = 1;
    if(g->negative != nullptr) {
        count += get_node_count(g->negative) + get_node_count(g->positive);
    }
    return count;
}

void generate_group_indices(group *g, int starting, int *used_, int max, int rowsize)
{
    assert(starting < max);

    int mine;
    int used;

    if(g->negative != nullptr) {

        int neg_used;
        int pos_used;

        generate_group_indices(g->negative, starting, &neg_used, max, rowsize);
        assert(starting + neg_used <= max);

        mine = starting + neg_used;

        generate_group_indices(g->positive, mine + 1, &pos_used, max, rowsize);
        assert(mine + 1 + pos_used <= max);

        used = neg_used + 1 + pos_used;

    } else {

        mine = starting;
        used = 1;
    }

    assert(mine < max);
    // g->my_index = renumber_to_improve_bvh_coherence(mine, rowsize);
    g->my_index = mine;
    *used_ = used;
}

void store_group_data(group *g, scene_shader_data &data)
{
    int mine = g->my_index;

    data.group_boxmin[mine * 3 + 0] = g->box.boxmin.x;
    data.group_boxmin[mine * 3 + 1] = g->box.boxmin.y;
    data.group_boxmin[mine * 3 + 2] = g->box.boxmin.z;
    data.group_boxmax[mine * 3 + 0] = g->box.boxmax.x;
    data.group_boxmax[mine * 3 + 1] = g->box.boxmax.y;
    data.group_boxmax[mine * 3 + 2] = g->box.boxmax.z;

    if(g->negative != nullptr) {

        store_group_data(g->negative, data);
        store_group_data(g->positive, data);

        data.group_directions[mine * 3 + 0] = g->D.x;
        data.group_directions[mine * 3 + 1] = g->D.y;
        data.group_directions[mine * 3 + 2] = g->D.z;
        data.group_children[mine * 2 + 0] = g->negative->my_index;
        data.group_children[mine * 2 + 1] = g->positive->my_index;
        data.group_objects[mine * 2 + 0] = 0;
        data.group_objects[mine * 2 + 1] = 0;

    } else {

        data.group_children[mine * 2 + 0] = 0x7fffffff;
        data.group_children[mine * 2 + 1] = 0x7fffffff;
        data.group_objects[mine * 2 + 0] = g->start;
        data.group_objects[mine * 2 + 1] = g->count;
    }
}

namespace
{

const int xPosDir = 0x1;
const int yPosDir = 0x2;
const int zPosDir = 0x4;
const int hitmiss_directions_count = 8;

vec3 get_coded_dir(int dircode)
{
    float x = (dircode & xPosDir) ? 1.0 : -1.0;
    float y = (dircode & yPosDir) ? 1.0 : -1.0;
    float z = (dircode & zPosDir) ? 1.0 : -1.0;
    return vec3(x, y, z);
}

const int hitmiss_max_stack_size = 64;
const unsigned long hitmiss_stop_traversal = 0x7fffffffU;

void create_hitmiss(group *root, int dircode)
{
    vec3 dir = get_coded_dir(dircode);

    group *stack[hitmiss_max_stack_size];
    group *g = root;
    int stack_top = -1;

    while(g != nullptr) {

        group *miss;
        if(stack_top == -1) {
            miss = nullptr;
        } else {
            miss = stack[stack_top];
        }

        if(g->negative == nullptr) {

            g->dirhit[dircode] = miss;
            g->dirmiss[dircode] = miss;
            if(stack_top > -1) {
                g = stack[stack_top--];
            } else {
                g = nullptr;
            }

        } else {

            group* g1;
            group* g2;

            if(dot(dir, g->D) < 0) {
                g1 = g->positive;
                g2 = g->negative;
            } else {
                g1 = g->negative;
                g2 = g->positive;
            }

            g->dirhit[dircode] = g1;
            g->dirmiss[dircode] = miss;
            assert(stack_top < hitmiss_max_stack_size);
            stack[++stack_top] = g2;
            g = g1;
        }
    }
}

void store_hitmiss(group *g, scene_shader_data& data, int dircode, int base)
{
    data.group_hitmiss[(base + g->my_index) * 2 + 0] = g->dirhit[dircode] ? g->dirhit[dircode]->my_index : hitmiss_stop_traversal;
    data.group_hitmiss[(base + g->my_index) * 2 + 1] = g->dirmiss[dircode] ? g->dirmiss[dircode]->my_index : hitmiss_stop_traversal;
    if(g->negative != nullptr) {
        store_hitmiss(g->negative, data, dircode, base);
        store_hitmiss(g->positive, data, dircode, base);
    }
}

};

template <class T>
inline T round_up(T v, unsigned int r)
{
    return ((v + r - 1) / r) * r;
}

void get_shader_data(world *w, scene_shader_data &data, unsigned int data_texture_width)
{
    unsigned int size;
    auto then = std::chrono::system_clock::now();

    data.vertex_count = w->triangles->triangles.size() * 3;
    data.vertex_data_rows = (data.vertex_count + data_texture_width - 1) / data_texture_width;
    size = 3 * data_texture_width * data.vertex_data_rows;
    data.vertex_positions = new float[size];
    data.vertex_normals = new float[size];
    data.vertex_colors = new float[size];
    for(unsigned int i = 0; i < w->triangles->triangles.size(); i++) {
        const indexed_triangle& t = w->triangles->triangles[i];
        for(unsigned int j = 0; j < 3; j++) {
            const vertex& vtx = w->triangles->vertices[t.i[j]];
            vtx.v.store(data.vertex_positions, i * 3 + j);
            vtx.n.store(data.vertex_normals, i * 3 + j);
            vtx.c.store(data.vertex_colors, i * 3 + j);
        }
    }

    data.group_count = get_node_count(w->root);
    data.group_data_rows = (data.group_count + data_texture_width - 1) / data_texture_width;
    size = data_texture_width * data.group_data_rows;
    data.group_directions = new float[3 * size];
    data.group_boxmin = new float[3 * size];
    data.group_boxmax = new float[3 * size];
    data.group_children = new float[2 * size];
    data.group_objects = new float[2 * size];
    data.group_hitmiss = new float[8 * 2 * size];

    int used;
    generate_group_indices(w->root, 0, &used, data.group_count, data_texture_width);
    assert(used == data.group_count);
    data.tree_root = w->root->my_index;

    store_group_data(w->root, data);

    for(int i = 0; i < hitmiss_directions_count; i++) {
        create_hitmiss(w->root, i); 
    }
    auto now = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed = now - then;

    fprintf(stderr, "hitmiss: %f seconds\n", elapsed.count());

    for(int i = 0; i < hitmiss_directions_count; i++) {
        store_hitmiss(w->root, data, i, i * (data_texture_width * data.group_data_rows));
    }
}

scene_shader_data::scene_shader_data() :
    vertex_positions(nullptr),
    vertex_colors(nullptr),
    vertex_normals(nullptr),
    group_boxmin(nullptr),
    group_boxmax(nullptr),
    group_directions(nullptr),
    group_children(nullptr),
    group_hitmiss(nullptr),
    group_objects(nullptr)
{
}

scene_shader_data::~scene_shader_data()
{
    delete[] vertex_positions;
    delete[] vertex_colors;
    delete[] vertex_normals;
    delete[] group_directions;
    delete[] group_children;
    delete[] group_objects;
    delete[] group_boxmin;
    delete[] group_boxmax;
    delete[] group_hitmiss;
}
