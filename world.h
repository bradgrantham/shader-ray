#pragma once

#include <algorithm>
#include <vector>
#include <map>
#include "vectormath.h"
#include "geometry.h"
#include "triangle-set.h"

struct camera { /* Viewpoint specification. */
    float fov; /* Entire View angle, left to right. */
};

struct group
{
    vec3 D; /* split direction */

    box3d box;

    group *negative, *positive;
    group *dirhit[8], *dirmiss[8];

    triangle_set& triangles;
    int start;
    unsigned int count;

    group(triangle_set& triangles_, group *neg, group *pos, const vec3& direction, const box3d& box_);
    group(triangle_set& triangles_, int start_, unsigned int count_);
    ~group();

    int my_index;
};

struct world
{
    char *Title;

    int triangle_count;
    triangle_set triangles; // base triangles, only traced through "root"

    group *root;
    vec3 background;

    vec3 scene_center;
    float scene_extent;

    camera cam;
    int xsub, ysub;

    world() :
        Title(0),
        root(0)
    {};
    ~world()
    {
        delete root;
        delete[] Title;
    }

    float camera_matrix[16];
    float camera_normal_matrix[16];

    float object_matrix[16];
    float object_inverse[16];
    float object_normal_matrix[16];
    float object_normal_inverse[16];
};

world *load_world(const std::string& filename);
void trace_image(int width, int height, float aspect, unsigned char *image, const world* Wd, const vec3& light_dir);


struct scene_shader_data
{
    unsigned int vertex_count;
    unsigned int vertex_data_rows;
    float *vertex_positions; // array of float3 {x, y, z, x, y, z, x, y, z}
    float *vertex_colors; // array of float3 {r, g, b, r, g, b, r, g, b}
    float *vertex_normals; // array of float3: {x, y, z, x, y, z, x, y, z}

    int group_count;
    int group_data_rows;
    int tree_root;
    float *group_boxmin; // array of float3: {x, y, z}
    float *group_boxmax; // array of float3: {x, y, z}
    float *group_directions; // array of float3: {x, y, z}
    float *group_children; // array of float, [0]>=0x7fffffff  if leaf

    float *group_hitmiss; // array of int2
    // [0] = node to go to on hit
    // [1] = node to go to on miss
    // >= 0x7fffffff on terminate

    float *group_objects; // array of {start, count}, count==0 if not leaf

    scene_shader_data();
    ~scene_shader_data();
};

void get_shader_data(world *w, scene_shader_data &data, unsigned int data_texture_width);
