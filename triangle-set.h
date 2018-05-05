#pragma once

#include <algorithm>
#include <vector>
#include <map>
#include "vectormath.h"
#include "geometry.h"

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
