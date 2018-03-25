#include <cerrno>
#include <ctime>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <cassert>
#include <memory>
#include <limits>
#include <string>
#include <stdint.h>
#include <thread>
#include <sys/time.h>
#include "world.h"

range range_intersect_box(const box3d& box, const ray& theray)
{
    range r = range(-100000000.0, 100000000.0);

    float t0, t1;

    t0 = (box.boxmin.x - theray.o.x) / theray.d.x;
    t1 = (box.boxmax.x - theray.o.x) / theray.d.x;
    if(theray.d.x >= 0.0)
        r.intersect(range(t0, t1));
    else
        r.intersect(range(t1, t0));

    t0 = (box.boxmin.y - theray.o.y) / theray.d.y;
    t1 = (box.boxmax.y - theray.o.y) / theray.d.y;
    if(theray.d.y >= 0.0)
        r.intersect(range(t0, t1));
    else
        r.intersect(range(t1, t0));

    t0 = (box.boxmin.z - theray.o.z) / theray.d.z;
    t1 = (box.boxmax.z - theray.o.z) / theray.d.z;
    if(theray.d.z >= 0.0)
        r.intersect(range(t0, t1));
    else
        r.intersect(range(t1, t0));

    return r;
}

static ray transform_ray(const ray& r, const float matrix[16], const float normal_matrix[16])
{
    ray result;

    vec4 o1(r.o.x, r.o.y, r.o.z, 1.0);
    vec4 d1(r.d.x, r.d.y, r.d.z, 0.0);

    vec4 o2 = matrix * o1;
    vec4 d2 = normal_matrix * d1;

    // XXX ignoring w
    result.o = vec3(o2.x, o2.y, o2.z);
    result.d = vec3(d2.x, d2.y, d2.z);

    return result;
}

bool triangle::intersect(const ray& ray, const range& r, surface_hit* hit)
{
    vec3 e0 = v[1] - v[0];
    vec3 e1 = v[2] - v[0];

    vec3 P = cross(ray.d, e1);

    float det = dot(e0, P);

    const float epsilon = 0.0000001; // .000001 too small

    if(det > -epsilon && det < epsilon)
        return 0;

    float inv_det = 1.0f / det;

    vec3 T = ray.o - v[0];

    float u = dot(T, P) * inv_det;
    if(u < 0.0f || u > 1.0f)
        return false;

    vec3 Q = cross(T, e0);
    float v = dot(ray.d, Q) * inv_det;
    if(v < 0.0f || u + v > 1.0f)
        return false;

    float d = dot(e1, Q) * inv_det;
    if(d < r.t0 || d > r.t1 || d > hit->t)
        return false;

    hit->t = d;
    hit->uvw[0] = 1 - u - v;
    hit->uvw[1] = u;
    hit->uvw[2] = v;
// Could calculate these in shade
    hit->point = ray.o + ray.d * d;
    hit->normal = n[0] * hit->uvw[0] + n[1] * hit->uvw[1] + n[2] * hit->uvw[2];
    hit->color = c[0] * hit->uvw[0] + c[1] * hit->uvw[1] + c[2] * hit->uvw[2];

    return true;
}

group::group(triangle_set& triangles_, group *neg, group *pos, const vec3& direction, const box3d& box_) :
    D(direction),
    box(box_),
    negative(neg),
    positive(pos),
    triangles(triangles_),
    start(0),
    count(0)
{
}

group::group(triangle_set& triangles_, int start_, unsigned int count_) :
    negative(NULL),
    positive(NULL),
    triangles(triangles_),
    start(start_),
    count(count_)
{
    for(unsigned int i = 0; i < count; i++) {
        triangle t = triangles[start + i];
        box.add(t.v[0], t.v[1], t.v[2]);
    }
}

group::~group()
{
    delete negative;
    delete positive;
}

bool tree_intersect_stack(group *root, const ray& ray, const range& r, surface_hit *hit)
{
    bool have_hit = false;
    group* stack_groups[25];
    int stack_top;

    range r2 = range_intersect_box(root->box, ray);
    r2.intersect(r);
    if(!r2 || (r2.t0 > hit->t))
        return false;

    stack_groups[0] = root;
    stack_top = 0;

    while(stack_top >= 0) {
        group *g = stack_groups[stack_top--];

        if(g->negative != NULL) {
            group *g1, *g2;

            if(dot(ray.d, g->D) > 0) {
                g1 = g->negative;
                g2 = g->positive;
            } else {
                g1 = g->positive;
                g2 = g->negative;
            }

            range r3 = range_intersect_box(g2->box, ray);
            r3.intersect(r);
            if(r3 && (r3.t0 < hit->t))
                stack_groups[++stack_top] = g2;

            r3 = range_intersect_box(g1->box, ray);
            r3.intersect(r);
            if(r3 && (r3.t0 < hit->t))
                stack_groups[++stack_top] = g1;

        } else {

            for(unsigned int i = 0; i < g->count; i++) {
                if(g->triangles[g->start + i].intersect(ray, r, hit))
                    have_hit = true;
            }
        }
    }

    return have_hit;
}

bool set_bad_hit(surface_hit *hit, float r, float g, float b)
{
    hit->color = vec3(r, g, b);
    hit->normal = vec3(0, 0, -1);
    hit->t = 1.0f;
    return true;
}

bool group::intersect(const ray& ray, const range &r, surface_hit* hit)
{
    return tree_intersect_stack(this, ray, r, hit);
}

static const bool cast_shadows = true;

vec3 trace(const ray& theray, const world* world, const vec3& light_dir)
{
    range r(0, std::numeric_limits<float>::max());
    surface_hit hit;
    hit.t = std::numeric_limits<float>::max();

    ray object_ray = transform_ray(theray, world->object_matrix, world->object_normal_matrix);
    bool have_hit = world->root->intersect(object_ray, r, &hit);
    if(!have_hit)
        return world->background;

    vec4 n1(hit.normal.x, hit.normal.y, hit.normal.z, 0.0), n2;
    vec4 p1(hit.point.x, hit.point.y, hit.point.z, 1.0), p2;
    n2 = world->object_normal_inverse * n1;
    p2 = world->object_inverse * p1;
    vec3 normal = vec3(n2.x, n2.y, n2.z);
    vec3 point = vec3(p2.x, p2.y, p2.z);

    if(dot(normal, theray.d) > 0)
        normal = normal * -1;

    float brightness;

    float shadowed = false;

    if(cast_shadows)
    {
        struct ray shadowray;
        shadowray.o = point + normal * .0001;
        shadowray.d = light_dir;
        surface_hit shadow_hit;
        shadow_hit.t = std::numeric_limits<float>::max();
        ray object_ray = transform_ray(shadowray, world->object_matrix, world->object_normal_matrix);
        shadowed = world->root->intersect(object_ray, range(0, std::numeric_limits<float>::max()), &shadow_hit);
    }

    if(shadowed) {

        brightness = .1;

    } else {

        brightness = dot(normal, light_dir);
        if(brightness < .1)
            brightness = .1;
        if(brightness > 1)
            brightness = 1;
    }

    return hit.color * brightness;
}

vec3 make_eye_ray(float u, float v, float aspect, float fov)
{
    vec3 eye;

    eye.x = tanf(fov) * (u - .5);
    eye.y = tanf(fov) * (v - .5) * aspect;
    eye.z = -1.0;

    return normalize(eye);
}

const int tile_width = 16, tile_height = 16;
std::mutex which_tile_lock;
int which_tile = 0;

int get_next_tile()
{
    std::lock_guard<std::mutex> locker(which_tile_lock);
    return which_tile++;
}

void trace_image_worker(int me, int width, int height, float aspect, unsigned char *image, const world* world, const vec3& light_dir)
{
    int tile_columns = (width + tile_width - 1) / tile_width;
    int tile_rows = (height + tile_height - 1) / tile_height;
    while(true) {
        int which = get_next_tile();
        int tile_row = which / tile_columns;
        if(tile_row >= tile_rows)
            return;
        int tile_column = which - tile_columns * tile_row;

        int x = tile_row * tile_width;
        int y = tile_column * tile_height;

        int w = std::min(tile_width, width - x);
        int h = std::min(tile_height, height - y);

        for(int yloop = y; yloop < y + h; yloop++) {
            unsigned char *row = image + (height - yloop - 1) * (((width * 3) + 3) & ~3);
            for(int xloop = x; xloop < x + w; xloop++) {
                float u = (xloop + .5) / width;
                float v = (yloop + .5) / height;

                ray eye_ray;
                eye_ray.d = make_eye_ray(u, v, aspect, world->cam.fov);
                eye_ray.o = vec3(0, 0, 0);

                ray world_ray = transform_ray(eye_ray, world->camera_matrix, world->camera_normal_matrix);

                vec3 color = trace(world_ray, world, light_dir);

                unsigned char *pixel = row + xloop * 3;
                pixel[0] = std::max(0.0f, std::min(color.x, 1.0f)) * 255;
                pixel[1] = std::max(0.0f, std::min(color.y, 1.0f)) * 255;
                pixel[2] = std::max(0.0f, std::min(color.z, 1.0f)) * 255;
            }
        }
    }
}

void trace_image(int width, int height, float aspect, unsigned char *image, const world* world, const vec3& light_dir)
{
    int thread_count = 8;
    std::vector<std::thread> threads;

    which_tile = 0;
    for(int i = 0; i < thread_count; ++i){
        threads.push_back(std::thread([=](){trace_image_worker(i, width, height, aspect, image, world, light_dir);}));
    }

    for(auto& thread : threads){
        thread.join();
    }
}

static int bvh_max_depth = 30; // Could set to 19 in order to fit in 20 bits for a 1024x1024 texture
static unsigned int bvh_leaf_max = 10; // Empirically chosen on Intel embedded on MBP 13 late 2013

static float sah_ctrav = 1;
static float sah_cisec = 4; // ??

bool try_all_three = false;

float geometryScaleFactor = 1.0f;
bool correctFileColorGamma = true;

static void initialize_runtime_parameters() __attribute__((constructor));
static void initialize_runtime_parameters()
{
    if(getenv("TRY_ALL_THREE") != 0) {
        try_all_three = true;
        fprintf(stderr, "Will try all three axes for BVH split\n");
    }
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
    if(getenv("COLORS_ARE_LINEAR") != 0) {
        correctFileColorGamma = false;
        fprintf(stderr, "file colors are linear\n");
    }
    if(getenv("GEOMETRY_SCALE") != 0) {
        geometryScaleFactor = atof(getenv("GEOMETRY_SCALE"));
        fprintf(stderr, "geometry scale set to %f\n", geometryScaleFactor);
    }
}

int total_treed = 0;
time_t previous;
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
    while((largest_leaf_count > 0) && (bvh_leaf_size_counts[largest_leaf_count]) == 0)
        largest_leaf_count--;

    for(int i = 0; i <= largest_leaf_count; i++) {
        fprintf(stderr, "%2d objects in %6d leaves\n", i, bvh_leaf_size_counts[i]);
    }
    if(bvh_leaf_size_counts[63] > 0)
        fprintf(stderr, "63 or more objects in %6d leaves\n", bvh_leaf_size_counts[63]);
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
        if(s1 >= s2)
            break;

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
        previous = time(NULL);
    }
    if(time(NULL) > previous) {
        fprintf(stderr, "total treed = %d\n", total_treed);
        previous = time(NULL);
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
        g = new group(triangles, g1, NULL, split_plane_normal, vertexbox);
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

#if 1

const int DirectionPosX = 0;
const int DirectionNegX = 1;
const int DirectionPosY = 2;
const int DirectionNegY = 3;
const int DirectionPosZ = 4;
const int DirectionNegZ = 5;

struct beam {
    beam(const box3d& box, int d)
    {};
};

struct directional_grid {
    box3d box;
    int width;
    int height;
    int depth;
    group **cells;
    directional_grid(const box3d& box_, int width, int height, int depth) :
        box(box_),
        cells(new group*[width * height * depth * 6])
    {}
    // These two should be references to pointers and references to const pointers.
    group* get(int i, int j, int k, int d)
    {
        return cells[(i + j * width + k * width * height) * 6 + d];
    }
    void set(int i, int j, int k, int d, group *g)
    {
        cells[(i + j * width + k * width * height) * 6 + d] = g;
    }
    box3d get_cell_box(int i, int j, int k)
    {
        vec3 boxdim = box.dim();
        vec3 boxmin, boxmax;
        boxmin.x = box.boxmin.x + boxdim.x * i / width;
        boxmin.y = box.boxmin.y + boxdim.y * j / height;
        boxmin.z = box.boxmin.z + boxdim.z * k / depth;
        boxmax.x = box.boxmin.x + boxdim.x * (i + 1) / width;
        boxmax.y = box.boxmin.y + boxdim.y * (j + 1) / height;
        boxmax.z = box.boxmin.z + boxdim.z * (k + 1) / depth;
        return box3d(boxmin, boxmax);
    }
};

group *lowest_common_parent(group *root, const beam&b)
{
    return root;
}

void build_bvh_startgrid(group *root, int width, int height, int depth, directional_grid *grid)
{
    for(int k = 0; k < depth; k++)
        for(int j = 0; j < height; j++)
            for(int i = 0; i < width; i++)
                for(int d = 0; d < 6; d++) {
                    box3d box = grid->get_cell_box(i, j, k);
                    beam b(box, d);
                    grid->set(i, j, k, d, lowest_common_parent(root, b));
                }
}

#endif

struct scoped_FILE
{
    FILE *fp;
    scoped_FILE(FILE *fp_) : fp(fp_) {}
    ~scoped_FILE() {if(fp) fclose(fp);}
    operator FILE*() { return fp; }
};

const float screengamma = 2.63;

bool ParseTriSrc(FILE *fp, triangle_set& triangles)
{
    char texture_name[512];
    char tag_name[512];
    float specular_color[4];
    float shininess;

    while(fscanf(fp,"\"%[^\"]\"", texture_name) == 1) {
        if(strcmp(texture_name, "*") == 0)
            texture_name[0] = '\0';

	if(fscanf(fp,"%s ", tag_name) != 1) {
	    fprintf(stderr, "couldn't read tag name\n");
	    return false;
	}

	if(fscanf(fp,"%g %g %g %g %g ", &specular_color[0], &specular_color[1],
	    &specular_color[2], &specular_color[3], &shininess) != 5) {
	    fprintf(stderr, "couldn't read specular properties\n");
	    return false;
	}

	if(shininess > 0 && shininess < 1) {
	    // shininess is not exponent - what is it?
	    shininess *= 10;
	}

        float v[3][3];
        float n[3][3];
        float c[3][4];
        float t[3][2];
        for(int i = 0; i < 3; i++) {

	    if(fscanf(fp,"%g %g %g %g %g %g %g %g %g %g %g %g ",
	        &v[i][0], &v[i][1], &v[i][2],
	        &n[i][0], &n[i][1], &n[i][2],
	        &c[i][0], &c[i][1], &c[i][2], &c[i][3],
	        &t[i][0], &t[i][1]) != 12) {

		fprintf(stderr, "couldn't read Vertex\n");
		return false;
	    }
        }

        //MATERIAL mtl(texture_name, specular_color, shininess);

        vertex vtx[3];
        for(int i = 0; i < 3; i++) {
            vtx[i].v = vec3(v[i][0], v[i][1], v[i][2]) * geometryScaleFactor;
            if(correctFileColorGamma)
                vtx[i].c.set(pow(c[i][0], screengamma), pow(c[i][1], screengamma), pow(c[i][2], screengamma));
            else
                vtx[i].c.set(c[i][0], c[i][1], c[i][2]);
            vtx[i].n.set(n[i][0], n[i][1], n[i][2]);
            vtx[i].n = normalize(vtx[i].n);
        }
        triangles.add(vtx[0], vtx[1], vtx[2]);
    }
    return true;
}

world *load_world(char *fname) // Get world and return pointer.
{
    timeval t1, t2;
    std::auto_ptr<world> w(new world);

    scoped_FILE fp(fopen(fname, "r"));
    if(fp == NULL) {
        fprintf(stderr, "Cannot open file %s for input.\nE#%d\n", fname, errno);
        return NULL;
    }
    w->background.set(.2, .2, .2);

    gettimeofday(&t1, NULL);
    bool success = ParseTriSrc(fp, w->triangles);
    if(!success) {
        fprintf(stderr, "Couldn't parse triangles from file.\n");
        return NULL;
    }
    gettimeofday(&t2, NULL);
    long long micros = (t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec;
    fprintf(stderr, "Parsing: %f seconds\n", micros / 1000000.0);

    w->triangle_count = w->triangles.triangles.size();
    fprintf(stderr, "%d triangles.\n", w->triangle_count);
    fprintf(stderr, "%zd independent vertices.\n", w->triangles.vertices.size());
    fprintf(stderr, "%.2f vertices per triangle.\n", w->triangles.vertices.size() * 1.0 / w->triangle_count);

    gettimeofday(&t1, NULL);

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

    gettimeofday(&t2, NULL);
    micros = (t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec;
    fprintf(stderr, "Finding scene center and extent: %f seconds\n", micros / 1000000.0);

    gettimeofday(&t1, NULL);
    w->root = make_tree(w->triangles, 0, w->triangle_count);
    gettimeofday(&t2, NULL);

    micros = (t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec;
    fprintf(stderr, "BVH: %f seconds\n", micros / 1000000.0);

    print_tree_stats();

    return w.release();
}

int get_node_count(group *g)
{
    int count = 1;
    if(g->negative != NULL) {
        count += get_node_count(g->negative) + get_node_count(g->positive);
    }
    return count;
}

void generate_group_indices(group *g, int starting, int *used_, int max, int rowsize)
{
    assert(starting < max);

    int mine;
    int used;

    if(g->negative != NULL) {

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

    if(g->negative != NULL) {

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

    while(g != NULL) {

        group *miss;
        if(stack_top == -1)
            miss = NULL;
        else
            miss = stack[stack_top];

        if(g->negative == NULL) {

            g->dirhit[dircode] = miss;
            g->dirmiss[dircode] = miss;
            if(stack_top > -1)
                g = stack[stack_top--];
            else
                g = NULL;

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
    if(g->negative != NULL) {
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
    timeval t1, t2;
    gettimeofday(&t1, NULL);

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

    gettimeofday(&t1, NULL);
    for(int i = 0; i < 8; i++) {
        create_hitmiss(w->root, i); 
    }
    gettimeofday(&t2, NULL);

    long long micros = (t2.tv_sec - t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec;
    fprintf(stderr, "hitmiss: %f seconds\n", micros / 1000000.0);

    for(int i = 0; i < 8; i++) {
        store_hitmiss(w->root, data, i, i * (data_texture_width * data.group_data_rows));
    }
}

scene_shader_data::scene_shader_data() :
    vertex_positions(NULL),
    vertex_colors(NULL),
    vertex_normals(NULL),
    group_boxmin(NULL),
    group_boxmax(NULL),
    group_directions(NULL),
    group_children(NULL),
    group_objects(NULL)
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
}
