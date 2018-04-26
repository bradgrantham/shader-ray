// Not currently used, needed for textureLod in GLSL 1.4 or
//   texture2DLod in ESSL
// #extension GL_EXT_shader_texture_lod : enable
// #extension GL_ARB_shader_texture_lod : enable

precision highp float;

uniform mediump vec3 light_dir;
const mediump vec3 light_color = vec3(1.0, 1.0, 1.0);

uniform int which;

uniform mat4 object_matrix;
uniform mat4 object_inverse;
uniform mat4 object_normal_matrix;
uniform mat4 object_normal_inverse;

uniform int vertex_data_rows;
uniform highp sampler2D vertex_positions;
// uniform sampler2D vertex_colors;
uniform sampler2D vertex_normals;

uniform int group_data_rows;
uniform highp sampler2D group_boxmin;
uniform highp sampler2D group_boxmax;
uniform highp sampler2D group_objects;
uniform highp sampler2D group_hitmiss;

uniform highp vec3 right;
uniform highp vec3 up;

uniform highp float tree_root;

uniform sampler2D background;

uniform mediump vec3 specular_color;
uniform mediump vec3 diffuse_color;

in highp vec3 world_ray_origin;
in highp vec3 world_ray_direction;

struct ray {
    highp vec3 P;
    mediump vec3 D;
    mediump vec3 dPdx, dDdx;
    mediump vec3 dPdy, dDdy;
};

ray ray_transfer(in ray inray, highp float t, vec3 normal)
{
    ray outray;

    outray.P = inray.P + inray.D * t;
    outray.D = inray.D;

    float dtdx = -dot((inray.dPdx + t * inray.dDdx), normal) / dot(inray.D, normal);
    outray.dPdx = inray.dPdx + t * inray.dDdx + dtdx * inray.D;
    outray.dDdx = inray.dDdx;

    float dtdy = -dot((inray.dPdy + t * inray.dDdy), normal) / dot(inray.D, normal);
    outray.dPdy = inray.dPdy + t * inray.dDdy + dtdy * inray.D;
    outray.dDdy = inray.dDdy;

    return outray;
}

ray ray_reflect(in ray inray, in vec3 normal)
{
    ray outray;
    outray.D = reflect(inray.D, normal);
    outray.P = inray.P + normal * .0001; // XXX surface fudge

    // differentials; do this right
    outray.dPdx = inray.dPdx;
    outray.dPdy = inray.dPdy;
    outray.dDdx = inray.dDdx - 2 * dot(inray.dDdx, normal);
    outray.dDdy = inray.dDdy - 2 * dot(inray.dDdy, normal);

    return outray;
}

void ray_transform(in ray r, in mat4 matrix, in mat4 normal_matrix, out ray t)
{
    t.P = (matrix * vec4(r.P, 1.0)).xyz;
    t.D = (normal_matrix * vec4(r.D, 0.0)).xyz;
    t.dPdx = (matrix * vec4(r.dPdx, 0.0)).xyz;
    t.dPdy = (matrix * vec4(r.dPdy, 0.0)).xyz;
    t.dDdx = (matrix * vec4(r.dDdx, 0.0)).xyz;
    t.dDdy = (matrix * vec4(r.dDdy, 0.0)).xyz;
}

struct surface_hit
{
    highp float t;
    highp float which;
    mediump vec3 uvw;
};

const highp float infinitely_far = 10000000.0;

bool sphere_map_environment = false;

vec2 get_environment_map_coords(vec3 d)
{
    mediump vec2 sample = vec2(1.0 + atan(-d.z, d.x) / 3.14159265359 / 2.0, 1.0 - acos(d.y) / 3.141592);
    return sample;
}

const float pi = 3.14159265359;
const float tau = 2 * 3.14159265359;

vec3 sample_environment(in ray r, sampler2D sampler)
{
    // sample point
    mediump vec2 sample = vec2(1.0 + atan(-r.D.z, r.D.x) / tau, 1.0 - acos(r.D.y) / pi);
    // mediump vec2 sample = vec2(1.0 + atan(-r.D.z, r.D.x) / tau, 1.0 - r.D.y);

    // derivatives of texture coordinates with respect to image plane

    float dudx = (r.D.x * r.dDdx.z - r.D.z * r.dDdx.x) / (2.0 * pi * (r.D.x * r.D.x + r.D.z * r.D.z));
    float dudy = (r.D.x * r.dDdy.z - r.D.z * r.dDdy.x) / (2.0 * pi * (r.D.x * r.D.x + r.D.z * r.D.z));

    float dvdx = r.dDdx.y / (pi * sqrt(1.0 - r.D.y * r.D.y));
    float dvdy = r.dDdy.y / (pi * sqrt(1.0 - r.D.y * r.D.y));

    vec2 dpdx = vec2(dudx, dvdx);
    vec2 dpdy = vec2(dudy, dvdy);

    if(which == 1)
        return textureGrad(sampler, sample, dpdx, dpdy).rgb;
    else if(which == 2)
        return vec3((abs(dpdy) * vec2(1.0, 1.0)) * 100, 0.0);
    else 
        // return texture(sampler, sample).rgb;
        return textureGrad(sampler, sample, vec2(0, 0), vec2(0, 0)).rgb;
}

surface_hit surface_hit_init()
{
    return surface_hit(infinitely_far, -1.0, vec3(1, 0, 0));
}

void set_bad_hit(inout surface_hit hit, float r, float g, float b)
{
    hit.t = -1.0;
    hit.uvw = vec3(r, g, b);
}

struct range
{
    highp float t0, t1;
};

range make_range(highp float t0, highp float t1)
{
    range r;
    r.t0 = t0;
    r.t1 = t1;
    return r;
}

range range_full()
{
    return make_range(-100000000.0, 100000000.0);
}

range range_intersect(in range r1, in range r2)
{
    highp float t0 = max(r1.t0, r2.t0);
    highp float t1 = min(r1.t1, r2.t1);
    return make_range(t0, t1);
}

bool range_is_empty(in range r)
{
    return r.t0 >= r.t1;
}

const float NO_t = -1.0;

range range_intersect_box(in highp vec3 boxmin, in highp vec3 boxmax, in ray theray, in range prevr)
{
    highp float t0, t1;

    t0 = (boxmin.x - theray.P.x) / theray.D.x;
    t1 = (boxmax.x - theray.P.x) / theray.D.x;
    range r0 = range_intersect(prevr, (theray.D.x >= 0.0) ? make_range(t0, t1) : make_range(t1, t0));

    t0 = (boxmin.y - theray.P.y) / theray.D.y;
    t1 = (boxmax.y - theray.P.y) / theray.D.y;
    range r1 = range_intersect(r0, (theray.D.y >= 0.0) ? make_range(t0, t1) : make_range(t1, t0));

    t0 = (boxmin.z - theray.P.z) / theray.D.z;
    t1 = (boxmax.z - theray.P.z) / theray.D.z;
    range r2 = range_intersect(r1, (theray.D.z >= 0.0) ? make_range(t0, t1) : make_range(t1, t0));

    return r2;
}

struct group {
    bool is_branch;
    highp float start;
    highp float count;
    highp vec3 boxmin;
    highp vec3 boxmax;
    highp float hit_next;
    highp float miss_next;
};

const highp float sample_offset = .25; // 0.0; // 0.25;

mediump vec2 index_to_sample(highp int which, highp int width, highp int height)
{
    highp int j = which / width;
    highp int i = which - j * width;
    mediump vec2 sample = vec2((float(i) + sample_offset) / float(width), (float(j) + sample_offset) / float(height));
    return sample;
}

mediump vec2 index_to_sample(highp float which, highp int width, highp int height)
{
    highp float i = mod(which, float(width));
    highp float j = (which - i) / float(width);
    mediump vec2 sample = vec2((float(i) + sample_offset) / float(width), (float(j) + sample_offset) / float(height));
    return sample;
}

group get_group(highp float which, highp float hitmiss_offset)
{
    group g;

    mediump vec2 sample = index_to_sample(which, data_texture_width, group_data_rows);

    g.boxmin = texture(group_boxmin, sample).xyz;
    g.boxmax = texture(group_boxmax, sample).xyz;

    mediump vec2 sample2 = index_to_sample(which + hitmiss_offset, data_texture_width, group_data_rows * 8);
    highp vec2 group_next = texture(group_hitmiss, sample2).xy;
    g.hit_next = group_next.x;
    g.miss_next = group_next.y;

    g.is_branch = (g.hit_next != g.miss_next);

    if(!g.is_branch) {
        highp vec2 group_object = texture(group_objects, sample).xy;
        g.start = group_object.x;
        g.count = group_object.y;
    }

    return g;
}

range group_bounds_intersect(in group g, in ray theray, in range prevr)
{
    return range_intersect_box(g.boxmin, g.boxmax, theray, prevr);
}

/*
vec3 triangle_interpolate_color(highp float which, in vec3 uvw)
{
    mediump vec3 c0 = texture(vertex_colors, index_to_sample(which * 3.0 + 0.0, data_texture_width, vertex_data_rows)).xyz;
    mediump vec3 c1 = texture(vertex_colors, index_to_sample(which * 3.0 + 1.0, data_texture_width, vertex_data_rows)).xyz;
    mediump vec3 c2 = texture(vertex_colors, index_to_sample(which * 3.0 + 2.0, data_texture_width, vertex_data_rows)).xyz;

    return c0 * uvw.x + c1 * uvw.y + c2 * uvw.z;
}
*/

vec3 triangle_interpolate_normal(highp float which, in vec3 uvw)
{
    mediump vec3 n0 = texture(vertex_normals, index_to_sample(which * 3.0 + 0.0, data_texture_width, vertex_data_rows)).xyz;
    mediump vec3 n1 = texture(vertex_normals, index_to_sample(which * 3.0 + 1.0, data_texture_width, vertex_data_rows)).xyz;
    mediump vec3 n2 = texture(vertex_normals, index_to_sample(which * 3.0 + 2.0, data_texture_width, vertex_data_rows)).xyz;

    return n0 * uvw.x + n1 * uvw.y + n2 * uvw.z;
}

void triangle_intersect(highp float which, in ray theray, in range r, inout surface_hit hit)
{
    highp vec3 v0, v1, v2;
    v0 = texture(vertex_positions, index_to_sample(which * 3.0 + 0.0, data_texture_width, vertex_data_rows)).xyz;
    v1 = texture(vertex_positions, index_to_sample(which * 3.0 + 1.0, data_texture_width, vertex_data_rows)).xyz;
    v2 = texture(vertex_positions, index_to_sample(which * 3.0 + 2.0, data_texture_width, vertex_data_rows)).xyz;

    highp vec3 e0 = v1 - v0;
    highp vec3 e1 = v0 - v2;
    highp vec3 e2 = v2 - v1;

    highp vec3 M = cross(e1, theray.D);

    highp float det = dot(e0, M);

    const highp float epsilon = 0.0000001; // .000001 from M-T paper too large for bunny
    if(det > -epsilon && det < epsilon)
        return;

    float inv_det = 1.0 / det;

    // Do this in a somewhat different order than M-T in order to early out
    // if previous intersection is closer than this intersection
    highp vec3 T = theray.P - v0;
    highp vec3 Q = cross(T, e0);
    highp float d = -dot(e1, Q) * inv_det;
    if(d > hit.t)
        return;
    if(d < r.t0 || d > r.t1)
        return;

    mediump float u = dot(T, M) * inv_det;
    if(u < 0.0 || u > 1.0)
        return;

    mediump float v = dot(theray.D, Q) * inv_det;
    if(v < 0.0 || u + v > 1.0)
        return;

    hit.which = which;
    hit.t = d;
    hit.uvw[0] = 1.0 - u - v;
    hit.uvw[1] = u;
    hit.uvw[2] = v;

#if 0
    e0 = v1 - v0;
    e1 = v2 - v1;
    highp vec3 e1 = v0 - v1;

    hit.duvwdx[0] = (-(dot(e0, cross(r.dDdx, e1)) * dot((-v0 + r.P), cross(r.D, e1))) + ( dot(e0, cross(r.D, e1)) * (dot((-v0 + r.P), cross(r.dDdx, e1)) + dot(r.dPdx, cross(r.D, e1))))) / pow(dot(e0 . cross(r.D, e1)), 2);
    hit.duvwdx[1] = (-(dot(e1, cross(r.dDdx, e2)) * dot((-v1 + r.P), cross(r.D, e2))) + ( dot(e1, cross(r.D, e2)) * (dot((-v1 + r.P), cross(r.dDdx, e2)) + dot(r.dPdx, cross(r.D, e2))))) / pow(dot(e1 . cross(r.D, e2)), 2);
    hit.duvwdx[2] = (-(dot(e2, cross(r.dDdx, e0)) * dot((-v2 + r.P), cross(r.D, e0))) + ( dot(e2, cross(r.D, e0)) * (dot((-v2 + r.P), cross(r.dDdx, e0)) + dot(r.dPdx, cross(r.D, e0))))) / pow(dot(e2 . cross(r.D, e0)), 2);

    hit.duvwdy[0] = (-(dot(e0, cross(r.dDdy, e1)) * dot((-v0 + r.P), cross(r.D, e1))) + ( dot(e0, cross(r.D, e1)) * (dot((-v0 + r.P), cross(r.dDdy, e1)) + dot(r.dPdy, cross(r.D, e1))))) / pow(dot(e0 . cross(r.D, e1)), 2);
    hit.duvwdy[1] = (-(dot(e1, cross(r.dDdy, e2)) * dot((-v1 + r.P), cross(r.D, e2))) + ( dot(e1, cross(r.D, e2)) * (dot((-v1 + r.P), cross(r.dDdy, e2)) + dot(r.dPdy, cross(r.D, e2))))) / pow(dot(e1 . cross(r.D, e2)), 2);
    hit.duvwdy[2] = (-(dot(e2, cross(r.dDdy, e0)) * dot((-v2 + r.P), cross(r.D, e0))) + ( dot(e2, cross(r.D, e0)) * (dot((-v2 + r.P), cross(r.dDdy, e0)) + dot(r.dPdy, cross(r.D, e0))))) / pow(dot(e2 . cross(r.D, e0)), 2);
#endif
}

void shade(in surface_hit hit, in ray theray, out mediump vec3 normal, out highp vec3 point, out vec3 color)
{
    if(hit.which < 0.0) {
        color = vec3(1, 0, 0);
        normal = vec3(0, 0, -1);
        point = vec3(0, 0, 0);
    } else {

        point = theray.P + theray.D * hit.t; // barycentric interpolate?
        normal = triangle_interpolate_normal(hit.which, hit.uvw);
        // Could look up in texture here...
        color = vec3(1.0, 1.0, 1.0);
    }
}

/* limit to 200 for Windows Chrome < 30 */
#define CONSTANT_LENGTH_LOOPS
const highp int max_bvh_iterations = 400; // 400 is just a little too few for the models with more triangles
const highp float max_leaf_tests = 10.0;

const highp float terminator = 16777215.0;

void group_intersect(highp float root, in ray theray, in range prevr, inout surface_hit hit)
{
    highp float g = root;
    highp float xd = (theray.D.x > 0.0) ? 1.0 : 0.0;
    highp float yd = (theray.D.y > 0.0) ? 2.0 : 0.0;
    highp float zd = (theray.D.z > 0.0) ? 4.0 : 0.0;
    highp float offset = (xd + yd + zd) * float(group_data_rows) * float(data_texture_width);

#ifdef CONSTANT_LENGTH_LOOPS
    for(highp int i = 0; i < max_bvh_iterations; i++) {
#else
    while(g < terminator) {
#endif
        group gg = get_group(g, offset);

        range r = group_bounds_intersect(gg, theray, prevr);

        if((!range_is_empty(r)) && (r.t0 < hit.t)) {
            if(!gg.is_branch) {
                //  have a max objects in leaf, but limit on BVH depth
                //  takes precedence so could be fat leaves at max
                //  depth, need to carefully only make web scenes with
                //  forcibly fewer objects at leaf
#ifdef CONSTANT_LENGTH_LOOPS
                for(highp float j = 0.0; j < max_leaf_tests; j++) {
                    if(j >= gg.count)
                        break;
#else
                for(highp float j = 0.0; j < gg.count; j++) {
#endif
                    triangle_intersect(gg.start + j, theray, r, hit);
                }
            }
            g = gg.hit_next;
        } else {
            g = gg.miss_next;
        }

#ifdef CONSTANT_LENGTH_LOOPS
        if(g >= terminator)
            return;
        if(i == max_bvh_iterations - 1)
            set_bad_hit(hit, 1.0, 0.0, 0.0);
    }
#else
    }
#endif
}

const bool cast_shadows = true;

vec3 approximate_diffuse(in vec3 view, in vec3 point, in vec3 normal)
{
    mediump float lcos = max(0.0, dot(normal, light_dir));
    vec3 light_diffuse = light_color * lcos;
    vec3 ambient = vec3(.0, .0, .0); // vec3(.05, .05, .05);
    vec3 diffuse = ambient;

    if(cast_shadows) {
        surface_hit shadow_hit = surface_hit_init();

        ray world_shadowray;
        ray object_shadowray;

        world_shadowray.P = point;
        world_shadowray.D = light_dir;
        ray_transform(world_shadowray, object_matrix, object_normal_matrix, object_shadowray);
        group_intersect(tree_root, object_shadowray, make_range(0.0, 100000000.0), shadow_hit);
        if(shadow_hit.t >= infinitely_far) {
            diffuse += light_diffuse;
        }
    } else {
        diffuse += light_diffuse;
    }

    return diffuse;
}

highp vec3 f_schlick_lh(in vec3 cspec, in vec3 l, in vec3 h)
{
    return cspec + (vec3(1.0, 1.0, 1.0) - cspec) * pow(1.0 - dot(l, h), 5.0);
}

highp vec3 f_schlick_vr(in vec3 cspec, in vec3 v, in vec3 r)
{
    return cspec + (vec3(1.0, 1.0, 1.0) - cspec) * pow(dot(v, r) * .5 + .5, 5.0);
}

int intersect_and_shade(in ray worldray, out vec3 object_diffuse, out vec3 object_specular, out vec3 normal, out ray reflected)
{
    surface_hit shading = surface_hit_init();

    ray objectray;
    ray_transform(worldray, object_matrix, object_normal_matrix, objectray);

    group_intersect(tree_root, objectray, make_range(0.0, 100000000.0), shading);

    if(shading.t >= infinitely_far)
        return 0;

    if(shading.t == -1.0) {
        object_diffuse = shading.uvw;
        object_specular = vec3(0);
        return 2;
    }

    vec3 object_color;
    highp vec3 object_normal, object_point;
    shade(shading, objectray, object_normal, object_point, object_color);

    highp vec3 world_normal;
    world_normal = (object_normal_inverse * vec4(object_normal, 0.0)).xyz;

    if(dot(world_normal, worldray.D) > 0.0)
        world_normal *= -1.0;

    ray transferred = ray_transfer(worldray, shading.t, world_normal);

    reflected = ray_reflect(transferred, world_normal);

    object_specular = f_schlick_vr(specular_color, worldray.D, reflected.D);
    object_diffuse = diffuse_color * object_color;
    normal = world_normal;
    return 1;
}

const bool use_filmic = true;
const bool do_tonemap = true;

float filmic(float c) 
{
    float x = max(0.0, c - 0.004);
    return (x * (6.2 * x + 0.5)) / (x * (6.2 * x + 1.7) + 0.06);
}

vec3 tonemap_and_gamma(in vec3 color)
{
    if(use_filmic) {

        return vec3(filmic(color.r), filmic(color.g), filmic(color.b));

    } else {

        vec3 tonemapped = vec3(color.r / (color.r + 1.0), color.g / (color.g + 1.0), color.b / (color.b + 1.0));

        vec3 withgamma = vec3( pow(tonemapped.r, 1.0 / 2.63),
            pow(tonemapped.g, 1.0 / 2.63), pow(tonemapped.b, 1.0 / 2.63));

        return withgamma;
    }
}

const int bounce_count = 3;

vec3 trace(in ray worldray)
{
    vec3 accumulated = vec3(0, 0, 0);
    vec3 modulation = vec3(1, 1, 1);
    for(int i = 0; i < bounce_count; i++) {
        ray reflected;
        vec3 object_diffuse;
        vec3 object_specular;
        vec3 normal;

        int hit_something = intersect_and_shade(worldray, object_diffuse, object_specular, normal, reflected);
        if(hit_something == 0)
            break;
        if(hit_something == 2)
            return object_diffuse;

        if(object_diffuse.x > 0.0 && object_diffuse.y > 0.0 && object_diffuse.z > 0.0) {
            vec3 diffuse_irradiance =
                approximate_diffuse(worldray.D, reflected.P, normal);

            accumulated += modulation * object_diffuse * diffuse_irradiance;
        }
        modulation *= object_specular;

        worldray = reflected;
    }
    vec3 background_color = sample_environment(worldray, background);
    return accumulated + modulation * background_color;
}

vec4 green = vec4(0.0, 1.0, 0.0, 1.0);
vec4 red = vec4(1.0, 0.0, 0.0, 1.0);
vec4 blue = vec4(0.0, 0.0, 1.0, 1.0);
vec4 white = vec4(1.0, 1.0, 1.0, 1.0);
vec4 black = vec4(0.0, 0.0, 0.0, 1.0);

vec4 screenspace_binary(highp int v)
{
    if(gl_FragCoord.y < 512.0 - 32.0)
        v = 0;
    else if(gl_FragCoord.y < 512.0 - 16.0)
        v = 0x5555;
    int bit = int(512.0 - gl_FragCoord.x) / 16;

    highp int div_by_2_bit = v;
    for(int i = 0; i < 32; i++) {
        if(i < bit)
            div_by_2_bit /= 2;
    }

    highp int div_by_2_bitp1 = (div_by_2_bit / 2) * 2;

    highp int masked = div_by_2_bitp1 - div_by_2_bit;
    return (masked != 0) ? white : black;
}

out vec4 fragment_color;

void main()
{
    vec3 result = vec3(0.0, 0.0, 0.0);

    ray worldray;
    worldray.P = world_ray_origin;
    worldray.D = normalize(world_ray_direction);

    vec3 d = worldray.D;
    worldray.dPdx = vec3(0.0, 0.0, 0.0);
    worldray.dDdx = (dot(d, d) * right - dot(d, right) * d) / pow(dot(d, d), 1.5);
    worldray.dPdy = vec3(0.0, 0.0, 0.0);
    worldray.dDdy = (dot(d, d) * up - dot(d, up) * d) / pow(dot(d, d), 1.5);

    if(which == 4) {
    }

    if(false) {
        // from C++, float image_plane_width = 2 * tanf(gWorld->cam.fov / 2.0);
        // with a 1024 pixel width, and 0.698132 fov degrees: 40
        // 2 * tan(fov / 2) = 0.72794
        // change in X for one pixel should be .00071087890625000000
        // (and same for Y with aspect 1)
        // so to get half red, I should draw (right * 703)
        // fragment_color = vec4(right * 703.0, 1.0);
        fragment_color = vec4(worldray.dDdx * 703.0, 1.0);
        return;
    }

    if(which == 3) { // XXX debug
        vec2 me_left = get_environment_map_coords(d - worldray.dDdx / 2.0);
        vec2 me_right = get_environment_map_coords(d + worldray.dDdx / 2.0);
        vec2 me_below = get_environment_map_coords(d - worldray.dDdy / 2.0);
        vec2 me_above = get_environment_map_coords(d + worldray.dDdy / 2.0);
        fragment_color = vec4((abs(me_above - me_below) * vec2(1.0, 1.0)) * 100, 0.0, 1.0);
        return;
    }

    result = trace(worldray);

    if(which == 5) {
        // XXX reference image
        result = vec3(0, 0, 0);
        const int blarg = 5;
        for(int i = 0; i < blarg; i++) {
            for(int j = 0; j < blarg; j++) {
                float u = (i / float(blarg) - .5);
                float v = (j / float(blarg) - .5);
                worldray.P = world_ray_origin;
                worldray.D = normalize(world_ray_direction + u * .2 * right + v * .2 * up);
                vec3 d = worldray.D;
                worldray.dPdx = vec3(0.0, 0.0, 0.0);
                worldray.dDdx = (dot(d, d) * right - dot(d, right) * d) / pow(dot(d, d), 1.5);
                worldray.dPdy = vec3(0.0, 0.0, 0.0);
                worldray.dDdy = (dot(d, d) * up - dot(d, up) * d) / pow(dot(d, d), 1.5);
                result += trace(worldray);
            }
        }
        result /= blarg * blarg;
    }

    if(do_tonemap)
        fragment_color = vec4(tonemap_and_gamma(result), 1.0);
    else
        fragment_color = vec4(result, 1.0);

    // fragment_color = vec4(worldray.dDdy * 1000.0, 1.0);
}
