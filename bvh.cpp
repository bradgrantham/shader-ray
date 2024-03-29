/*
   Copyright 2018 Brad Grantham.
   
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

#include <chrono>
#include <vector>
#include <map>
#include <cstdlib>
#include "bvh.h"

// unnamed namespace for file scope
namespace
{

// Number of shapes at which BVH creation just makes a leaf
unsigned int bvh_leaf_max = 10; // Empirically chosen on Intel embedded on MBP 13 late 2013

// No BVH nodes will be created below this depth; overrides bvh_leaf_max
// Could set to 19 in order to fit in 20 bits for a 1024x1024 texture
int bvh_max_depth = 30;

// Total shapes processed so far during make_bvh recursion
int total_shapes_processed = 0;

// Last time total_shapes_processed was printed
std::chrono::time_point<std::chrono::system_clock> previous_total_shapes_print;

// http://en.cppreference.com/w/cpp/language/value_initialization means "int" value in map element is initialized to 0
std::map<int, int> bvh_node_count_by_level;

// Lump all leaves this size or bigger together for stats
const int bvh_leaf_max_size_for_stats = 64;

// Number of leaves the max size or bigger
int bvh_leaf_count_ge_max_size = 0;

// Histogram of leaf size
std::map<int, int> bvh_leaf_count_by_size;

// Number of nodes and then leaves created
int bvh_node_count = 0;
int bvh_leaf_count = 0;

// Surface area heuristic constants for traversal and intersection
float sah_ctrav = 1;
float sah_cisec = 4;    // A guess.

void initialize_runtime_parameters() __attribute__((constructor));
void initialize_runtime_parameters()
{
    if(getenv("BVH_MAX_DEPTH") != 0) {
        bvh_max_depth = atoi(getenv("BVH_MAX_DEPTH"));
        fprintf(stderr, "BVH max depth set to %d\n", bvh_max_depth);
    }
    if(getenv("BVH_LEAF_MAX") != 0) {
        bvh_leaf_max = atoi(getenv("BVH_LEAF_MAX"));
        fprintf(stderr, "BVH max objects per leaf set to %d\n", bvh_leaf_max);
    }
    if(getenv("SAH_CTRAV") != 0) {
        sah_ctrav = atof(getenv("SAH_CTRAV"));
        fprintf(stderr, "SAH cost of traversal set to %f\n", sah_ctrav);
    }
    if(getenv("SAH_CISEC") != 0) {
        sah_cisec = atof(getenv("SAH_CISEC"));
        fprintf(stderr, "SAH cost of intersection set to %f\n", sah_cisec);
    }
}

}; // unnamed namespace for file scope

void print_bvh_stats()
{
    fprintf(stderr, "%d bvh nodes\n", bvh_node_count);
    fprintf(stderr, "%d of those are leaves\n", bvh_leaf_count);

    for(auto& b : bvh_node_count_by_level) {
        fprintf(stderr, "bvh level %2d: %6d nodes\n", b.first, b.second);
    }

    for(auto& b : bvh_leaf_count_by_size) {
        fprintf(stderr, "%2d shapes in %6d leaves\n", b.first, b.second);
    }

    if(bvh_leaf_count_ge_max_size > 0) {
        fprintf(stderr, "%d or more objects in %6d leaves\n", bvh_leaf_max_size_for_stats, bvh_leaf_count_ge_max_size);
    }
}

float surface_area(const vec3& boxdim)
{
    return 2 * (boxdim.x * boxdim.y + boxdim.x * boxdim.z + boxdim.y * boxdim.z);
}

// From Wald's thesis: http://www.sci.utah.edu/~wald/PhD/wald_phd.pdf
float sah(int tri)
{
    return sah_ctrav + sah_cisec * tri;
}

float sah(const vec3& boxdim, const vec3& lboxdim, int ltri, const vec3& rboxdim, int rtri)
{

    float area = surface_area(boxdim);
    float larea = surface_area(lboxdim);
    float rarea = surface_area(rboxdim);

    return sah_ctrav + sah_cisec * (larea / area * ltri + rarea / area * rtri);
}

group *make_leaf(triangle_set_ptr triangles, int start, int count, int level)
{
    total_shapes_processed += count;
    group* g = new group(triangles, start, count);
    if(count >= bvh_leaf_max_size_for_stats) {
        bvh_leaf_count_ge_max_size++;
    } else {
        bvh_leaf_count_by_size[count]++;
    }
    bvh_leaf_count++;
    bvh_node_count_by_level[level]++;
    bvh_node_count++;
    return g;
}

struct split_bin
{
    box3d box;
    int count;
    box3d rightbox;
    int in_and_right;
    split_bin() :
        count(0)
    {}
};

int get_bin_from_triangle(const indexed_triangle& t, const box3d& box, int dimension, int bin_count)
{
    float start, stop, x;

    if(dimension == 0) {
        start = box.boxmin.x;
        stop = box.boxmax.x;
        x = t.barycenter.x;
    } else if(dimension == 1) {
        start = box.boxmin.y;
        stop = box.boxmax.y;
        x = t.barycenter.y;
    } else {
        start = box.boxmin.z;
        stop = box.boxmax.z;
        x = t.barycenter.z;
    }

    int y = floor((x - start) * (bin_count) / (stop - start));
    int bin = std::min(bin_count - 1, std::max(0, y));

    return bin;
}

vec3 get_bin_split(int i, const box3d& box, int dimension, int bin_count)
{
    float start, stop;

    if(dimension == 0) {
        start = box.boxmin.x;
        stop = box.boxmax.x;
    } else if(dimension == 1) {
        start = box.boxmin.y;
        stop = box.boxmax.y;
    } else {
        start = box.boxmin.z;
        stop = box.boxmax.z;
    }

    float x = start + i * (stop - start) / (bin_count);

    if(dimension == 0) {
        return vec3(x, 0, 0);
    } else if(dimension == 1) {
        return vec3(0, x, 0);
    } else {
        return vec3(0, 0, x);
    }
}

float get_best_split(const box3d& box, int dimension, std::vector<indexed_triangle>& triangles, int start, int count, vec3& split, float to_beat)
{
    const int max_bin_count = 40;
    int bin_count = std::min(max_bin_count, count * 2);
    split_bin bins[max_bin_count];

    // go through triangles, store in bins
    for(int i = 0; i < count; i++) {
        int bin = get_bin_from_triangle(triangles[start + i], box, dimension, bin_count);
        bins[bin].box.add(triangles[start + i].box);
        bins[bin].count ++;
    }

    // go from front to back, accumulate and store "right box" and count of right tris
    box3d rightbox;
    int rtri = 0;
    for(int i = bin_count - 1; i > -1; i--) {
        split_bin& b = bins[i];
        rightbox.add(b.box);
        rtri += b.count;
        b.rightbox = rightbox;
        b.in_and_right = rtri;
        if(false)
             printf("bin %d: in_and_right = %d\n", i, rtri);
    }

    // go from back to front, accumulate left box, left = count-right,
    // set best_heuristic
    float best_heuristic = to_beat;
    box3d leftbox;

    leftbox.add(bins[0].box);

    for(int i = 1; i < bin_count; i++) {
        split_bin& b = bins[i];

        int rtri = b.in_and_right;
        int ltri = count - rtri;
        if((rtri != 0) && (ltri != 0)) {
            float heuristic = sah(box.dim(), leftbox.dim(), ltri, b.rightbox.dim(), rtri);
            if(heuristic < best_heuristic) {
                best_heuristic = heuristic;
                split = get_bin_split(i, box, dimension, bin_count);
            }
        }

        leftbox.add(b.box);
    }
    return best_heuristic;
}

void partition(std::vector<indexed_triangle>& triangles, int start, unsigned int count, const vec3& split_plane, const vec3& split_plane_normal, int* startA, int* countA, int* startB, int *countB)
{
    int s1 = start - 1;
    int s2 = start + count;

    do {
        // from start to s1, not including s1, is negative
        // from s2 to start + count - 1 is positive
        do {
            s1 += 1;
        } while((s1 < s2) && dot(triangles[s1].barycenter - split_plane, split_plane_normal) < 0);

        // If there wasn't a positive triangle before s2, done.
        if(s1 >= s2)
            break;

        // s1 is now location of lowest positive triangle 

        do {
            s2 -= 1;
        } while((s1 < s2) && dot(triangles[s2].barycenter - split_plane, split_plane_normal) >= 0);


        // If there wasn't a negative triangle between s1 and s2, done
        if(s1 >= s2) {
            break;
        }

        // s2 is now location of highest negative triangle 
        std::swap(triangles[s1], triangles[s2]);
    } while(true);

    // s1 is the first of the positive triangles
    *startA = start;
    *countA = s1 - *startA;
    *startB = s1;
    *countB = start + count - s1;
}

group* make_bvh(triangle_set_ptr triangles, int start, unsigned int count, int level)
{
    if(level == 0) {
        previous_total_shapes_print = std::chrono::system_clock::now();
    }
    auto now = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed = now - previous_total_shapes_print;
    if(elapsed.count() > 1.0) {
        fprintf(stderr, "total shapes processed = %d\n", total_shapes_processed);
        previous_total_shapes_print = std::chrono::system_clock::now();
    }

    if((level >= bvh_max_depth) || count <= bvh_leaf_max) {
        return make_leaf(triangles, start, count, level);
    }

    // find bounding box
    box3d vertexbox;
    box3d barycenterbox;
    for(unsigned int i = 0; i < count; i++) {
        vertexbox.add(triangles->triangles[start + i].box);
        barycenterbox.add(triangles->triangles[start + i].barycenter);
    }

    vec3 baryboxdim = barycenterbox.dim();

    float best_heuristic = sah(count);
    vec3 split_plane_normal;
    vec3 split_plane;

    if(baryboxdim.x > baryboxdim.y && baryboxdim.x > baryboxdim.z) {
        split_plane_normal = vec3(1, 0, 0);
        best_heuristic = get_best_split(vertexbox, 0, triangles->triangles, start, count, split_plane, best_heuristic);
    } else if(baryboxdim.y > baryboxdim.z) {
        split_plane_normal = vec3(0, 1, 0);
        best_heuristic = get_best_split(vertexbox, 1, triangles->triangles, start, count, split_plane, best_heuristic);
    } else {
        split_plane_normal = vec3(0, 0, 1);
        best_heuristic = get_best_split(vertexbox, 2, triangles->triangles, start, count, split_plane, best_heuristic);
    }

    if(best_heuristic >= sah(count)) {
        fprintf(stderr, "Large leaf node (no good split) at %d, %u triangles, total %d\n", level, count, total_shapes_processed);
        return make_leaf(triangles, start, count, level);
    }

    int startA, countA;
    int startB, countB;

    partition(triangles->triangles, start, count, split_plane, split_plane_normal, &startA, &countA, &startB, &countB);

    group *g;

    if(countA > 0 && countB > 0) {

        // construct children
        group *g1 = make_bvh(triangles, startA, countA, level + 1);
        g = new group(triangles, g1, nullptr, split_plane_normal, vertexbox);
        group *g2 = make_bvh(triangles, startB, countB, level + 1);
        g->positive = g2;
        bvh_node_count_by_level[level]++;
        bvh_node_count++;

    } else {

        fprintf(stderr, "Large leaf node (all one side) at %d, %u triangles, total %d\n", level, count, total_shapes_processed);
        g = make_leaf(triangles, start, count, level);
    }

    return g;
}

