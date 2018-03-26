#include "vectormath.h"
#include <algorithm>
#include <vector>
#include <map>

struct camera { /* Viewpoint specification. */
    float fov; /* Entire View angle, left to right. */
};

struct surface_hit
{
    float t;
    float uvw[3];
    vec3 normal;
    vec3 point;
    vec3 color;
};

struct range
{
    float t0, t1;
    range() :
        t0(-std::numeric_limits<float>::max()),
        t1(std::numeric_limits<float>::max())
    {}
    range(const range &r) :
        t0(r.t0),
        t1(r.t1)
    {}
    range(float t0_, float t1_) :
        t0(t0_),
        t1(t1_)
    {}
    range &intersect(const range &r2)
    {
        t0 = std::max(t0, r2.t0);
        t1 = std::min(t1, r2.t1);
        return *this;
    }
    operator bool() { return t0 < t1; }
};

struct vertex {
    vec3 v;
    vec3 c;
    vec3 n;
};

struct VertexComparator
{
    bool operator()(const vertex& v1, const vertex& v2) const
    {
        // grr, should be subscripted
        if(v1.v.x < v2.v.x) { return true; } else if(v1.v.x > v2.v.x) { return false; }
        if(v1.v.y < v2.v.y) { return true; } else if(v1.v.y > v2.v.y) { return false; }
        if(v1.v.z < v2.v.z) { return true; } else if(v1.v.z > v2.v.z) { return false; }

        if(v1.n.x < v2.n.x) { return true; } else if(v1.n.x > v2.n.x) { return false; }
        if(v1.n.y < v2.n.y) { return true; } else if(v1.n.y > v2.n.y) { return false; }
        if(v1.n.z < v2.n.z) { return true; } else if(v1.n.z > v2.n.z) { return false; }

        if(v1.c.x < v2.c.x) { return true; } else if(v1.c.x > v2.c.x) { return false; }
        if(v1.c.y < v2.c.y) { return true; } else if(v1.c.y > v2.c.y) { return false; }
        if(v1.c.z < v2.c.z) { return true; } else if(v1.c.z > v2.c.z) { return false; }

        return false;
    }
};

struct triangle
{
    vec3 v[3];
    vec3 c[3];
    vec3 n[3];

    bool intersect(const ray& ray, const range& r, surface_hit *hit);

    triangle()
    {
    }
    triangle(const vertex& v0, const vertex& v1, const vertex& v2)
    {
        v[0] = v0.v; v[1] = v1.v; v[2] = v2.v;
        c[0] = v0.c; c[1] = v1.c; c[2] = v2.c;
        n[0] = v0.n; n[1] = v1.n; n[2] = v2.n;
    }
    triangle(const vec3 v_[3], const vec3 c_[3], const vec3 n_[3])
    {
        for(int i = 0; i < 3; i++) {
            v[i] = v_[i];
            c[i] = c_[i];
            n[i] = n_[i];
        }
    }
    ~triangle() {}
};

struct indexed_triangle
{
    int i[3];
    box3d box;
    vec3 barycenter;
    indexed_triangle(int i0, int i1, int i2,
        const vertex& v0, const vertex& v1, const vertex& v2)
    {
        i[0] = i0;
        i[1] = i1;
        i[2] = i2;
        box.add(v0.v, v1.v, v2.v);
        barycenter = (v0.v + v1.v + v2.v) / 3.0;
    }
};

struct triangle_set
{
    std::vector<vertex> vertices;
    std::vector<indexed_triangle> triangles;
    box3d box;

    triangle operator[](int i)
    {
        int i0 = triangles[i].i[0];
        int i1 = triangles[i].i[1];
        int i2 = triangles[i].i[2];
        return triangle(vertices[i0], vertices[i1], vertices[i2]);
    }

    std::map<vertex, int, VertexComparator> vertex_map;      // only used during adding
    int add(const vertex &v0, const vertex& v1, const vertex& v2)
    {
        int i0 = find_vertex(v0);
        int i1 = find_vertex(v1);
        int i2 = find_vertex(v2);
        triangles.push_back(indexed_triangle(i0, i1, i2, v0, v1, v2));
        box.add(triangles[triangles.size() - 1].box);
        return triangles.size() - 1;
    }
private:
    int find_vertex(const vertex& v)
    {
        auto vx = vertex_map.find(v);

        if(vx == vertex_map.end()) {
            vertices.push_back(v);

            int index = vertices.size() - 1;
            vertex_map[v] = index;
            return index;

        } else {
            return vx->second;
        }
    }
public:
    void finish()
    {
        vertex_map.clear();
    }
    void swap(int i0, int i1)
    {
        std::swap(triangles[i0], triangles[i1]);
    }
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

    bool intersect(const ray& ray, const range& r, surface_hit *hit);

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

world *load_world(char *fname);
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