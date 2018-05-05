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
#include "world.h"

static int bvh_max_depth = 30; // Could set to 19 in order to fit in 20 bits for a 1024x1024 texture
static unsigned int bvh_leaf_max = 10; // Empirically chosen on Intel embedded on MBP 13 late 2013

static float sah_ctrav = 1;
static float sah_cisec = 4; // ??

static void initialize_runtime_parameters() __attribute__((constructor));
static void initialize_runtime_parameters()
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

int total_treed = 0;
std::chrono::time_point<std::chrono::system_clock> previous;
int bvh_level_counts[64];
int bvh_leaf_size_counts[64];
int bvh_node_count = 0;
int bvh_leaf_count = 0;

void print_tree_stats()
{
    fprintf(stderr, "%d bvh nodes\n", bvh_node_count);
    fprintf(stderr, "%d of those are leaves\n", bvh_leaf_count);
    for(int i = 0; i < bvh_max_depth + 1; i++) {
        fprintf(stderr, "bvh level %2d: %6d nodes\n", i, bvh_level_counts[i]);
    }
    int largest_leaf_count = 63;
    while((largest_leaf_count > 0) && (bvh_leaf_size_counts[largest_leaf_count]) == 0) {
        largest_leaf_count--;
    }

    for(int i = 0; i <= largest_leaf_count; i++) {
        fprintf(stderr, "%2d objects in %6d leaves\n", i, bvh_leaf_size_counts[i]);
    }
    if(bvh_leaf_size_counts[63] > 0) {
        fprintf(stderr, "63 or more objects in %6d leaves\n", bvh_leaf_size_counts[63]);
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

group *make_leaf(triangle_set& triangles, int start, int count, int level)
{
    total_treed += count;
    group* g = new group(triangles, start, count);
    bvh_leaf_size_counts[std::min(63, count)]++;
    bvh_leaf_count++;
    bvh_level_counts[level]++;
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

group* make_tree(triangle_set& triangles, int start, unsigned int count, int level = 0)
{
    if(level == 0) {
        previous = std::chrono::system_clock::now();
    }
    auto now = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed = now - previous;
    if(elapsed.count() > 1.0) {
        fprintf(stderr, "total treed = %d\n", total_treed);
        previous = std::chrono::system_clock::now();
    }

    if((level >= bvh_max_depth) || count <= bvh_leaf_max) {
        return make_leaf(triangles, start, count, level);
    }

    // find bounding box
    box3d vertexbox;
    box3d barycenterbox;
    for(unsigned int i = 0; i < count; i++) {
        vertexbox.add(triangles.triangles[start + i].box);
        barycenterbox.add(triangles.triangles[start + i].barycenter);
    }

    vec3 baryboxdim = barycenterbox.dim();

    float best_heuristic = sah(count);
    vec3 split_plane_normal;
    vec3 split_plane;

    if(baryboxdim.x > baryboxdim.y && baryboxdim.x > baryboxdim.z) {
        split_plane_normal = vec3(1, 0, 0);
        best_heuristic = get_best_split(vertexbox, 0, triangles.triangles, start, count, split_plane, best_heuristic);
    } else if(baryboxdim.y > baryboxdim.z) {
        split_plane_normal = vec3(0, 1, 0);
        best_heuristic = get_best_split(vertexbox, 1, triangles.triangles, start, count, split_plane, best_heuristic);
    } else {
        split_plane_normal = vec3(0, 0, 1);
        best_heuristic = get_best_split(vertexbox, 2, triangles.triangles, start, count, split_plane, best_heuristic);
    }

    if(best_heuristic >= sah(count)) {
        fprintf(stderr, "Large leaf node (no good split) at %d, %u triangles, total %d\n", level, count, total_treed);
        return make_leaf(triangles, start, count, level);
    }

    int startA, countA;
    int startB, countB;

    partition(triangles.triangles, start, count, split_plane, split_plane_normal, &startA, &countA, &startB, &countB);

    group *g;

    if(countA > 0 && countB > 0) {

        // construct children
        group *g1 = make_tree(triangles, startA, countA, level + 1);
        g = new group(triangles, g1, nullptr, split_plane_normal, vertexbox);
        group *g2 = make_tree(triangles, startB, countB, level + 1);
        g->positive = g2;
        bvh_level_counts[level]++;
        bvh_node_count++;

    } else {

        fprintf(stderr, "Large leaf node (all one side) at %d, %u triangles, total %d\n", level, count, total_treed);
        g = make_leaf(triangles, start, count, level);
    }

    return g;
}

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

    w->background.set(.2, .2, .2);

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

    w->triangle_count = w->triangles.triangles.size();
    fprintf(stderr, "%d triangles.\n", w->triangle_count);
    fprintf(stderr, "%zd independent vertices.\n", w->triangles.vertices.size());
    fprintf(stderr, "%.2f vertices per triangle.\n", w->triangles.vertices.size() * 1.0 / w->triangle_count);

    then = std::chrono::system_clock::now();

    w->scene_center = w->triangles.box.center();

    float scene_extent_squared = 0;
    for(int i = 0; i < w->triangle_count; i++) {
        triangle t = w->triangles[i];
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
    w->root = make_tree(w->triangles, 0, w->triangle_count);

    now = std::chrono::system_clock::now();
    elapsed = now - then;

    fprintf(stderr, "BVH: %f seconds\n", elapsed.count());

    print_tree_stats();

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

const int xPosDir = 0x1;
const int yPosDir = 0x2;
const int zPosDir = 0x4;

vec3 get_coded_dir(int dircode)
{
    float x = (dircode & xPosDir) ? 1.0 : -1.0;
    float y = (dircode & yPosDir) ? 1.0 : -1.0;
    float z = (dircode & zPosDir) ? 1.0 : -1.0;
    return vec3(x, y, z);
}

void create_hitmiss(group *root, int dircode)
{
    vec3 dir = get_coded_dir(dircode);

    group *stack[48];
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
            stack[++stack_top] = g2;
            g = g1;
        }
    }
}

void store_hitmiss(group *g, scene_shader_data& data, int dircode, int base)
{
    data.group_hitmiss[(base + g->my_index) * 2 + 0] = g->dirhit[dircode] ? g->dirhit[dircode]->my_index : 0x7fffffffU;
    data.group_hitmiss[(base + g->my_index) * 2 + 1] = g->dirmiss[dircode] ? g->dirmiss[dircode]->my_index : 0x7fffffffU;
    if(g->negative != nullptr) {
        store_hitmiss(g->negative, data, dircode, base);
        store_hitmiss(g->positive, data, dircode, base);
    }
}

template <class T>
inline T round_up(T v, unsigned int r)
{
    return ((v + r - 1) / r) * r;
}

void get_shader_data(world *w, scene_shader_data &data, unsigned int data_texture_width)
{
    unsigned int size;
    auto then = std::chrono::system_clock::now();

    data.vertex_count = w->triangles.triangles.size() * 3;
    data.vertex_data_rows = (data.vertex_count + data_texture_width - 1) / data_texture_width;
    size = 3 * data_texture_width * data.vertex_data_rows;
    data.vertex_positions = new float[size];
    data.vertex_normals = new float[size];
    data.vertex_colors = new float[size];
    for(unsigned int i = 0; i < w->triangles.triangles.size(); i++) {
        const indexed_triangle& t = w->triangles.triangles[i];
        for(unsigned int j = 0; j < 3; j++) {
            const vertex& vtx = w->triangles.vertices[t.i[j]];
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

    for(int i = 0; i < 8; i++) {
        create_hitmiss(w->root, i); 
    }
    auto now = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed = now - then;

    fprintf(stderr, "hitmiss: %f seconds\n", elapsed.count());

    for(int i = 0; i < 8; i++) {
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
