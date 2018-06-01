#include <chrono>
#include <string>
#include <vector>
#include <limits>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cerrno>
#include <FreeImagePlus.h>

#define GLFW_INCLUDE_GLCOREARB
#include <GLFW/glfw3.h>

#include "world.h"

bool redraw_window = false;
bool stream_frames = false;

int gWindowWidth, gWindowHeight;
world *gWorld;
std::chrono::time_point<std::chrono::system_clock> prev_frame_time;
float zoom = 0.0f;
float object_rotation[4];
float light_rotation[4];
vec3 light_dir(0, 0, 0); // Set by update_light()
vec3 object_position(0, 0, 0);
int which = 0;

struct material {
    vec3 specular_color;
    bool metal;
};

// From Hoffman's notes from S2010
std::vector<material> materials = {
    {{1, .71, .29}, true}, // gold 
    {{.95, .95, 0.88}, true}, // silver 
    {{0.95, 0.64, 0.54}, true}, // copper  ...? Looks a little too pink
    {{0.56, 0.57, 0.58}, true}, // iron 
    {{0.91, 0.92, 0.92}, true}, // alum 
    // {{.02, .02, .02}, false}, // water - XXX refractive
    {{.03, .03, .03}, false}, // plastic / glass (low) 
    {{.05, .05, .05}, false}, // plastic high 
    // {{.08, .08, .08}, false}, // glass (high) / ruby - XXX refractive
    // {{.17, .17, .17}, false}, // diamond - XXX refractive
};
int which_material = 0;

std::vector<vec3> diffuse_colors = {
    { 1, 1, 1 }, // white
    { 1, .5, .5 }, // reddish
    { .25, 1, .25 }, // quite green
    { .5, .5, 1 }, // blueish
};
int which_diffuse_color = 0;

void drag_to_rotation(float dx, float dy, float rotation[4])
{
    float dist;

    /* XXX grantham 990825 - this "dist" doesn't make me confident. */
    /* but I put in the *10000 to decrease chance of underflow  (???) */
    dist = sqrt(dx * 10000 * dx * 10000 + dy * 10000 * dy * 10000) / 10000;
    /* dist = sqrt(dx * dx + dy * dy); */

    rotation[0] = M_PI * dist;
    rotation[1] = dy / dist;
    rotation[2] = dx / dist;
    rotation[3] = 0.0f;
}

void trackball_motion(float prevrotation[4], float dx, float dy, float newrotation[4])
{
    if(dx != 0 || dy != 0) {
        float rotation[4];
        drag_to_rotation(dx, dy, rotation);
        rotation_mult_rotation(prevrotation, rotation, newrotation);
    }
}

void create_camera_matrix(const vec3& viewpoint, float matrix[16], float normal_matrix[16])
{
    mat4_make_identity(matrix);

    float viewpoint_matrix[16];
    // This is the reverse of what you'd expect for OpenGL because
    // its used to transform the ray from eye space into world
    // space, as opposed to transforming the object from world into
    // eye space.
    mat4_make_translation(viewpoint.x, viewpoint.y, viewpoint.z, viewpoint_matrix);
    mat4_mult(viewpoint_matrix, matrix, matrix);

    mat4_invert(matrix, normal_matrix);
    mat4_transpose(normal_matrix, normal_matrix);
    normal_matrix[3] = 0.0;
    normal_matrix[7] = 0.0;
    normal_matrix[11] = 0.0;
}

void create_object_matrix(const vec3& center, const float rotation[4], const vec3& position, float matrix[16], float inverse[16], float normal[16], float normal_inverse[16])
{
    // This is the reverse of what you'd expect for OpenGL because
    // its used to transform the ray from world space into object
    // space, as opposed to transforming the object from object into
    // world space.
    mat4_make_rotation(rotation[0], rotation[1], rotation[2], rotation[3], matrix);
    float m2[16];
    mat4_make_translation(center.x + position.x, center.y + position.y, center.z + position.z, m2);
    mat4_mult(matrix, m2, matrix);

    mat4_invert(matrix, inverse);
    mat4_transpose(matrix, normal);
    mat4_invert(normal, normal);
    normal[3] = 0.0;
    normal[7] = 0.0;
    normal[11] = 0.0;
    mat4_transpose(matrix, normal_inverse);
    normal_inverse[3] = 0.0;
    normal_inverse[7] = 0.0;
    normal_inverse[11] = 0.0;
}

void update_light()
{
    float light_matrix[16];
    float light_normal[16];
    float light_transpose[16];

    mat4_make_rotation(light_rotation[0], light_rotation[1], light_rotation[2], light_rotation[3], light_matrix);
    mat4_transpose(light_matrix, light_transpose);
    mat4_invert(light_transpose, light_normal);
    light_normal[3] = 0.0;
    light_normal[7] = 0.0;
    light_normal[11] = 0.0;

    vec4 l1(0, 0, 1, 0), l2;
    l2 = light_normal * l1;
    light_dir.x = l2.x;
    light_dir.y = l2.y;
    light_dir.z = l2.z;
}

void update_view_params(world *world, float zoom)
{
    vec3 offset;

    offset.x = 0;
    offset.y = 0;
    offset.z = zoom;

    create_camera_matrix(offset, world->camera_matrix, world->camera_normal_matrix);

    create_object_matrix(world->scene_center, object_rotation, object_position, world->object_matrix, world->object_inverse, world->object_normal_matrix, world->object_normal_inverse);
}

static void check_opengl(const char *filename, int line)
{
    int glerr;

    if((glerr = glGetError()) != GL_NO_ERROR) {
        printf("GL Error: %04X at %s:%d\n", glerr, filename, line);
    }
}

bool gPrintShaderErrorInfo = true;
bool gPrintShaderLog = true;

static bool CheckShaderCompile(GLuint shader, const std::string& shader_name)
{
    int status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if(status == GL_TRUE) {
        return true;
    }

    if(gPrintShaderLog) {
        int length;
        fprintf(stderr, "%s compile failure.\n", shader_name.c_str());
        fprintf(stderr, "shader text:\n");
        glGetShaderiv(shader, GL_SHADER_SOURCE_LENGTH, &length);
        char source[length];
        glGetShaderSource(shader, length, nullptr, source);
        fprintf(stderr, "%s\n", source);
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);

        if (length > 0) {
            char log[length];
            glGetShaderInfoLog(shader, length, nullptr, log);
            fprintf(stderr, "\nshader error log:\n%s\n", log);
        }

    }
    return false;
}

static bool CheckProgramLink(GLuint program)
{
    int status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if(status == GL_TRUE) {
        return true;
    }

    if(gPrintShaderLog) {
        int log_length;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);

        if (log_length > 0) {
            char log[log_length];
            glGetProgramInfoLog(program, log_length, nullptr, log);
            fprintf(stderr, "program error log: %s\n",log);
        }
    }

    return false;
}


template <class T>
inline T round_up(T v, unsigned int r)
{
    return ((v + r - 1) / r) * r;
}

static char *load_text(FILE *fp)
{
    char *text = nullptr;
    char line[1024];
    int newsize;

    text = strdup("");
    while(fgets(line, 1023, fp) != nullptr) {
        newsize = round_up(strlen(text) + strlen(line) + 1, 65536);
        text = (char *)realloc(text,  newsize);
        if(text == nullptr) {
            fprintf(stderr, "loadShader: Couldn't realloc program string to"
                " add more characters\n");
            exit(EXIT_FAILURE);
        }
        strcat(text, line);
    }
    
    return text;
}

GLint pos_attrib = 0, texcoord_attrib = 1;

static char *gRayTracingFragmentShaderText;
static char *gRayTracingVertexShaderText;

GLuint vert_buffer;
GLuint texcoord_buffer;
GLuint screenquad_vao;

struct raytracer_gl_binding
{
    GLint group_objects_uniform;
    GLint group_hitmiss_uniform;
    GLint group_directions_uniform;
    GLint group_boxmax_uniform;
    GLint group_boxmin_uniform;
    GLuint group_objects_texture;
    GLuint group_hitmiss_texture;
    GLuint group_directions_texture;
    GLuint group_boxmin_texture;
    GLuint group_boxmax_texture;
    GLint group_data_rows_uniform;

    GLint vertex_positions_uniform;
    GLint vertex_colors_uniform;
    GLint vertex_normals_uniform;
    GLuint vertex_positions_texture;
    GLuint vertex_colors_texture;
    GLuint vertex_normals_texture;
    GLint vertex_data_rows_uniform;

    GLint background_texture_uniform;
    GLuint background_texture;

    GLint which_uniform;
    GLint tree_root_uniform;

    GLint modelview_uniform;
    GLint camera_matrix_uniform;
    GLint camera_normal_matrix_uniform;
    GLint object_matrix_uniform;
    GLint object_inverse_uniform;
    GLint object_normal_matrix_uniform;
    GLint object_normal_inverse_uniform;

    GLint image_plane_width_uniform;
    GLint aspect_uniform;

    GLint right_uniform;
    GLint up_uniform;

    GLint light_dir_uniform;

    GLint specular_color_uniform;
    GLint diffuse_color_uniform;

    GLuint vertex_shader;
    GLuint fragment_shader;
    GLuint program;
};

const unsigned int data_texture_width = 2048;
scene_shader_data scene_data;
raytracer_gl_binding raytracer_gl;

struct float2Dimage
{
    int width;
    int height;
    float *pixels;
    float2Dimage(int width_, int height_) :
        width(width_),
        height(height_),
        pixels(new float[3 * width_ * height_])
    {
    }
};

float2Dimage *background_image;
float2Dimage *background_convolved;

int new_data_texture()
{
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    check_opengl(__FILE__, __LINE__);
    return tex;
}

void load_scene_data(world *w, raytracer_gl_binding &binding)
{
    const char *filename;
    if(getenv("SHADER") != nullptr) {
        filename = getenv("SHADER");
    } else {
        filename = "raytracer.es.fs";
    }

    FILE *fp = fopen(filename, "r");
    if(fp == nullptr) {
        fprintf(stderr, "couldn't open raytracer.fs\n");
        exit(EXIT_FAILURE);
    }
    gRayTracingFragmentShaderText = load_text(fp);
    fclose(fp);

    fp = fopen("raytracer.vs", "r");
    if(fp == nullptr) {
        fprintf(stderr, "couldn't open raytracer.vs\n");
        exit(EXIT_FAILURE);
    }
    gRayTracingVertexShaderText = load_text(fp);
    fclose(fp);

    get_shader_data(w, scene_data, data_texture_width);

#if 0
    if(true) {
        printf("memory use by scene data\n");
        printf("%f megabytes in triangle vertices\n", scene_data.triangle_count * sizeof(float) * 9 / 1000000.0);
        printf("%f megabytes in triangle colors\n", scene_data.triangle_count * sizeof(unsigned char) * 9 / 1000000.0);
        printf("%f megabytes in triangle normals\n", scene_data.triangle_count * sizeof(unsigned short) * 9 / 1000000.0);
        printf("%d groups\n", scene_data.group_count);
        printf("%f megabytes in group bounds\n", scene_data.group_count * sizeof(float) * 6 / 1000000.0);
        printf("%f megabytes in group children\n", scene_data.group_count * sizeof(int) * 2 / 1000000.0);
        printf("%f megabytes in group hitmiss\n", scene_data.group_count * sizeof(int) * 2 / 1000000.0);
        printf("total %f megabytes\n", (scene_data.triangle_count * (sizeof(unsigned char) + sizeof(unsigned short) + sizeof(float)) * 9 + scene_data.group_count * (sizeof(float) * 6 + sizeof(int) * 2 + sizeof(int) * 2)) / 1000000.0);
    }
#endif

    char version[512];
    char preamble[512];
    char *strings[3];
    sprintf(version, "#version 140\n");
    sprintf(preamble, "const int data_texture_width = %u;\n", data_texture_width);

    strings[0] = version;
    strings[1] = preamble;
    strings[2] = gRayTracingFragmentShaderText;

    raytracer_gl.fragment_shader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(raytracer_gl.fragment_shader, 3, strings, nullptr);
    glCompileShader(raytracer_gl.fragment_shader);
    if(!CheckShaderCompile(raytracer_gl.fragment_shader, "ray tracer fragment shader")) {
        exit(1);
    }

    strings[0] = version;
    strings[1] = preamble;
    strings[2] = gRayTracingVertexShaderText;
    raytracer_gl.vertex_shader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(raytracer_gl.vertex_shader, 3, strings, nullptr);
    glCompileShader(raytracer_gl.vertex_shader);
    if(!CheckShaderCompile(raytracer_gl.vertex_shader, "ray tracer vertex shader")) {
        exit(1);
    }

    raytracer_gl.program = glCreateProgram();
    glAttachShader(raytracer_gl.program, raytracer_gl.vertex_shader);
    glAttachShader(raytracer_gl.program, raytracer_gl.fragment_shader);
    glBindAttribLocation(raytracer_gl.program, pos_attrib, "pos");
    glBindAttribLocation(raytracer_gl.program, texcoord_attrib, "vtex");
    glLinkProgram(raytracer_gl.program);
    if(!CheckProgramLink(raytracer_gl.program)) {
        exit(1);
    }

    glUseProgram(raytracer_gl.program);
    check_opengl(__FILE__, __LINE__);

    raytracer_gl.specular_color_uniform = glGetUniformLocation(raytracer_gl.program, "specular_color");
    raytracer_gl.diffuse_color_uniform = glGetUniformLocation(raytracer_gl.program, "diffuse_color");
    raytracer_gl.light_dir_uniform = glGetUniformLocation(raytracer_gl.program, "light_dir");
    raytracer_gl.modelview_uniform = glGetUniformLocation(raytracer_gl.program, "modelview");

    raytracer_gl.vertex_data_rows_uniform = glGetUniformLocation(raytracer_gl.program, "vertex_data_rows");
    raytracer_gl.vertex_positions_uniform = glGetUniformLocation(raytracer_gl.program, "vertex_positions");
    raytracer_gl.vertex_normals_uniform = glGetUniformLocation(raytracer_gl.program, "vertex_normals");
    raytracer_gl.vertex_colors_uniform = glGetUniformLocation(raytracer_gl.program, "vertex_colors");

    raytracer_gl.group_data_rows_uniform = glGetUniformLocation(raytracer_gl.program, "group_data_rows");
    raytracer_gl.group_objects_uniform = glGetUniformLocation(raytracer_gl.program, "group_objects");
    raytracer_gl.group_hitmiss_uniform = glGetUniformLocation(raytracer_gl.program, "group_hitmiss");
    raytracer_gl.group_directions_uniform = glGetUniformLocation(raytracer_gl.program, "group_directions");
    raytracer_gl.group_boxmin_uniform = glGetUniformLocation(raytracer_gl.program, "group_boxmin");
    raytracer_gl.group_boxmax_uniform = glGetUniformLocation(raytracer_gl.program, "group_boxmax");
    raytracer_gl.background_texture_uniform = glGetUniformLocation(raytracer_gl.program, "background");

    raytracer_gl.which_uniform = glGetUniformLocation(raytracer_gl.program, "which");
    raytracer_gl.tree_root_uniform = glGetUniformLocation(raytracer_gl.program, "tree_root");
    raytracer_gl.camera_matrix_uniform = glGetUniformLocation(raytracer_gl.program, "camera_matrix");
    raytracer_gl.camera_normal_matrix_uniform = glGetUniformLocation(raytracer_gl.program, "camera_normal_matrix");
    raytracer_gl.object_matrix_uniform = glGetUniformLocation(raytracer_gl.program, "object_matrix");
    raytracer_gl.object_inverse_uniform = glGetUniformLocation(raytracer_gl.program, "object_inverse");
    raytracer_gl.object_normal_matrix_uniform = glGetUniformLocation(raytracer_gl.program, "object_normal_matrix");
    raytracer_gl.object_normal_inverse_uniform = glGetUniformLocation(raytracer_gl.program, "object_normal_inverse");
    raytracer_gl.image_plane_width_uniform = glGetUniformLocation(raytracer_gl.program, "image_plane_width");
    raytracer_gl.aspect_uniform = glGetUniformLocation(raytracer_gl.program, "aspect");
    raytracer_gl.right_uniform = glGetUniformLocation(raytracer_gl.program, "right");
    raytracer_gl.up_uniform = glGetUniformLocation(raytracer_gl.program, "up");
    check_opengl(__FILE__, __LINE__);

    raytracer_gl.vertex_positions_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, data_texture_width, scene_data.vertex_data_rows, 0, GL_RGB, GL_FLOAT, scene_data.vertex_positions);

    raytracer_gl.vertex_normals_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, data_texture_width, scene_data.vertex_data_rows, 0, GL_RGB, GL_FLOAT, scene_data.vertex_normals);

    raytracer_gl.vertex_colors_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, data_texture_width, scene_data.vertex_data_rows, 0, GL_RGB, GL_FLOAT, scene_data.vertex_colors);

    raytracer_gl.group_objects_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, data_texture_width, scene_data.group_data_rows, 0, GL_RG, GL_FLOAT, scene_data.group_objects);
    check_opengl(__FILE__, __LINE__);

    raytracer_gl.group_hitmiss_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, data_texture_width, scene_data.group_data_rows * 8, 0, GL_RG, GL_FLOAT, scene_data.group_hitmiss);
    check_opengl(__FILE__, __LINE__);

    raytracer_gl.group_directions_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, data_texture_width, scene_data.group_data_rows, 0, GL_RGB, GL_FLOAT, scene_data.group_directions);
    check_opengl(__FILE__, __LINE__);

    raytracer_gl.group_boxmin_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, data_texture_width, scene_data.group_data_rows, 0, GL_RGB, GL_FLOAT, scene_data.group_boxmin);
    check_opengl(__FILE__, __LINE__);

    raytracer_gl.group_boxmax_texture = new_data_texture();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, data_texture_width, scene_data.group_data_rows, 0, GL_RGB, GL_FLOAT, scene_data.group_boxmax);
    check_opengl(__FILE__, __LINE__);

    glGenTextures(1, &raytracer_gl.background_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.background_texture);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    //glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    // glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST_MIPMAP_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, 0x84FE /* GL_TEXTURE_MAX_ANISOTROPY_EXT */, 4.0);
    check_opengl(__FILE__, __LINE__);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, background_image->width, background_image->height, 0, GL_RGB, GL_FLOAT, background_image->pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    check_opengl(__FILE__, __LINE__);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void init_screenquad_geometry(void)
{
    float verts[4][4];
    float texcoords[4][2];

    verts[0][0] = -1;
    verts[0][1] = -1;
    verts[0][2] = 0;
    verts[0][3] = 1;
    verts[1][0] = 1; // gWindowWidth;
    verts[1][1] = -1;
    verts[1][2] = 0;
    verts[1][3] = 1;
    verts[2][0] = -1;
    verts[2][1] = 1; // gWindowHeight;
    verts[2][2] = 0;
    verts[2][3] = 1;
    verts[3][0] = 1; // gWindowWidth;
    verts[3][1] = 1; // gWindowHeight;
    verts[3][2] = 0;
    verts[3][3] = 1;

    texcoords[0][0] = 0;
    texcoords[0][1] = 1;
    texcoords[1][0] = 1;
    texcoords[1][1] = 1;
    texcoords[2][0] = 0;
    texcoords[2][1] = 0;
    texcoords[3][0] = 1;
    texcoords[3][1] = 0;

    glGenVertexArrays(1, &screenquad_vao);
    glBindVertexArray(screenquad_vao);
    glGenBuffers(1, &vert_buffer);
    glGenBuffers(1, &texcoord_buffer);

    check_opengl(__FILE__, __LINE__);

    glBindBuffer(GL_ARRAY_BUFFER, vert_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glVertexAttribPointer(pos_attrib, 4, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(pos_attrib);

    glBindBuffer(GL_ARRAY_BUFFER, texcoord_buffer);
    glBufferData(GL_ARRAY_BUFFER, sizeof(texcoords), texcoords, GL_STATIC_DRAW);
    glVertexAttribPointer(texcoord_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(texcoord_attrib);
}

void init()
{
    if(false) {
        printf("GL_RENDERER: \"%s\"\n", glGetString(GL_RENDERER));
        printf("GL_VERSION: \"%s\"\n", glGetString(GL_VERSION));
        printf("GL_VENDOR: \"%s\"\n", glGetString(GL_VENDOR));
        GLint num_extensions;
        glGetIntegerv(GL_NUM_EXTENSIONS, &num_extensions);
        printf("GL_EXTENSIONS: \n");
        for(int i = 0; i < num_extensions; i++) {
            printf("    \"%s\"\n", glGetStringi(GL_EXTENSIONS, i));
        }

        GLint max_tex;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
        printf("max texture size: %d\n", max_tex);
        glGetIntegerv(GL_MAX_RECTANGLE_TEXTURE_SIZE, &max_tex);
        printf("max texture rectangle size: %d\n", max_tex);
        GLfloat max_anisotropy;
        glGetFloatv(0x84FF /* GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT */, &max_anisotropy);
        printf("max anisotropy: %f\n", max_anisotropy);
    }

    init_screenquad_geometry();
    load_scene_data(gWorld, raytracer_gl);
}

void DrawFrame(GLFWwindow *window)
{
    glClearColor(1, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glUseProgram(raytracer_gl.program);
    check_opengl(__FILE__, __LINE__);

    int which_texture = 0;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.vertex_positions_texture);
    glUniform1i(raytracer_gl.vertex_positions_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.vertex_colors_texture);
    glUniform1i(raytracer_gl.vertex_colors_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.vertex_normals_texture);
    glUniform1i(raytracer_gl.vertex_normals_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.group_objects_texture);
    glUniform1i(raytracer_gl.group_objects_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.group_hitmiss_texture);
    glUniform1i(raytracer_gl.group_hitmiss_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.group_directions_texture);
    glUniform1i(raytracer_gl.group_directions_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.group_boxmin_texture);
    glUniform1i(raytracer_gl.group_boxmin_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.group_boxmax_texture);
    glUniform1i(raytracer_gl.group_boxmax_uniform, which_texture);
    which_texture++;

    glActiveTexture(GL_TEXTURE0 + which_texture);
    glBindTexture(GL_TEXTURE_2D, raytracer_gl.background_texture);
    glUniform1i(raytracer_gl.background_texture_uniform, which_texture);
    which_texture++;

    check_opengl(__FILE__, __LINE__);

    glUniform1i(raytracer_gl.which_uniform, which);
    glUniform1f(raytracer_gl.tree_root_uniform, scene_data.tree_root);

    glUniform1i(raytracer_gl.vertex_data_rows_uniform, scene_data.vertex_data_rows);
    glUniform1i(raytracer_gl.group_data_rows_uniform, scene_data.group_data_rows);

    glUniformMatrix4fv(raytracer_gl.camera_matrix_uniform, 1, GL_FALSE, gWorld->camera_matrix);
    glUniformMatrix4fv(raytracer_gl.camera_normal_matrix_uniform, 1, GL_FALSE, gWorld->camera_normal_matrix);
    glUniformMatrix4fv(raytracer_gl.object_matrix_uniform, 1, GL_FALSE, gWorld->object_matrix);
    glUniformMatrix4fv(raytracer_gl.object_inverse_uniform, 1, GL_FALSE, gWorld->object_inverse);
    glUniformMatrix4fv(raytracer_gl.object_normal_matrix_uniform, 1, GL_FALSE, gWorld->object_normal_matrix);
    glUniformMatrix4fv(raytracer_gl.object_normal_inverse_uniform, 1, GL_FALSE, gWorld->object_normal_inverse);

    // Since I always have trouble with this:
    // If tan(theta) yields (y / x), then tan() gives the
    // intersection at (x = 1) of the line at angle theta from X
    // axis.
    // So if the "full" field of view is "fov", and we consider
    // the view direction to be the X axis,
    // then theta is actually (fov / 2), thus tan(fov / 2) will
    // only be the units from the view axis to the left or right
    // side of the field of view.
    // So the "full" X width has to be 2 * tan(fov / 2).

    float image_plane_width = 2 * tanf(gWorld->cam.fov / 2.0);
    float aspect = gWindowHeight / (1.0f * gWindowWidth);
    glUniform1f(raytracer_gl.image_plane_width_uniform, image_plane_width);
    glUniform1f(raytracer_gl.aspect_uniform, aspect);

    vec4 d(image_plane_width / gWindowWidth, 0, 0, 0.0);
    vec4 right_vector = gWorld->camera_normal_matrix * d;
    glUniform3fv(raytracer_gl.right_uniform, 1, (GLfloat*)&right_vector);

    d = vec4(0, image_plane_width * aspect / gWindowHeight, 0, 0.0);
    vec4 up_vector = gWorld->camera_normal_matrix * d;
    glUniform3fv(raytracer_gl.up_uniform, 1, (GLfloat*)&up_vector);

    glBindBuffer(GL_ARRAY_BUFFER, vert_buffer);
    glVertexAttribPointer(pos_attrib, 4, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(pos_attrib);

    glBindBuffer(GL_ARRAY_BUFFER, texcoord_buffer);
    glVertexAttribPointer(texcoord_attrib, 2, GL_FLOAT, GL_FALSE, 0, 0);
    glEnableVertexAttribArray(texcoord_attrib);

    glUniformMatrix4fv(raytracer_gl.modelview_uniform, 1, GL_FALSE, mat4_identity);

    glUniform3fv(raytracer_gl.light_dir_uniform, 1, (GLfloat*)&light_dir);


    material& mtl = materials[which_material];
    glUniform3fv(raytracer_gl.specular_color_uniform, 1, (GLfloat*)&mtl.specular_color);
    if(mtl.metal) {
        glUniform3f(raytracer_gl.diffuse_color_uniform, 0, 0, 0);
    } else {
        glUniform3fv(raytracer_gl.diffuse_color_uniform, 1, (GLfloat*)&diffuse_colors[which_diffuse_color]);
    }

    glBindVertexArray(screenquad_vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    check_opengl(__FILE__, __LINE__);

    redraw_window = true;
    check_opengl(__FILE__, __LINE__);

    auto now = std::chrono::system_clock::now();
    std::chrono::duration<float> elapsed = now - prev_frame_time;
    if(0) printf("fps: %f (estimated %f millis)\n", 1 / elapsed.count(), elapsed.count() * 1000);
    prev_frame_time = now;
}

static void ErrorCallback(int error, const char* description)
{
    fprintf(stderr, "GLFW: %s\n", description);
}

enum {
    MOVE_OBJECT,
    MOVE_LIGHT,
} motion_target = MOVE_OBJECT;

/* snapshot the whole frontbuffer.  Return -1 on error, 0 on success. */
int screenshot(const char *colorName, const char *alphaName)
{
    static unsigned char *pixels;
    static size_t pixelsSize;
    GLint viewport[4];
    int i;
    FILE *fp;
    GLint prevReadBuf;

    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glGetIntegerv(GL_READ_BUFFER, &prevReadBuf);
    glReadBuffer(GL_FRONT);

    glGetIntegerv(GL_VIEWPORT, viewport);
    if(pixelsSize < (unsigned int)(viewport[2] * viewport[3] * 3)) {
        pixelsSize = (unsigned int)(viewport[2] * viewport[3] * 3);
        pixels = (unsigned char *)realloc(pixels, pixelsSize);
        if(pixels == nullptr) {
            fprintf(stderr, "snapshot: couldn't allocate %zd bytes for"
                " screenshot.\n", pixelsSize);
            return -1;
        }
    }

        /* color ---------------------------------------- */
    if(colorName != nullptr) {
        if((fp = fopen(colorName, "wb")) == nullptr) {
            fprintf(stderr, "snapshot: couldn't open \"%s\".\n", colorName);
            return -1;
        }
        glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3],
            GL_RGB, GL_UNSIGNED_BYTE, pixels);
        fprintf(fp, "P6 %d %d 255\n", viewport[2], viewport[3]);
        for(i = viewport[3] - 1; i >= 0; i--) {
            fwrite(pixels + viewport[2] * 3 * i, 3, viewport[2], fp);
        }
        fclose(fp);
    }

        /* alpha ---------------------------------------- */
    if(alphaName != nullptr) {
        if((fp = fopen(alphaName, "wb")) == nullptr) {
            fprintf(stderr, "snapshot: couldn't open \"%s\".\n", alphaName);
            return -1;
        }
        glReadPixels(viewport[0], viewport[1], viewport[2], viewport[3],
            GL_ALPHA, GL_UNSIGNED_BYTE, pixels);
        fprintf(fp, "P5 %d %d 255\n", viewport[2], viewport[3]);
        for(i = viewport[3] - 1; i >= 0; i--) {
            fwrite(pixels + viewport[2] * i, 1, viewport[2], fp);
        }
        fclose(fp);
    }

    glReadBuffer(prevReadBuf);

    return 0;
}

bool do_benchmark_run = false;

void KeyCallback(GLFWwindow *window, int key, int scancode, int action, int mods)
{
    if(action == GLFW_PRESS) {
        switch(key) {
            case '[':
                gWorld->cam.fov /= 1.05;
                printf("fov = %f\n", gWorld->cam.fov);
                redraw_window = true;
                break;

            case ']':
                gWorld->cam.fov *= 1.05;
                printf("fov = %f\n", gWorld->cam.fov);
                redraw_window = true;
                break;

            case ',':
                which--;
                printf("which = %d\n", which);
                redraw_window = true;
                break;

            case '.':
                which++;
                printf("which = %d\n", which);
                redraw_window = true;
                break;

            case 'Q': case '\033':
                glfwSetWindowShouldClose(window, GL_TRUE);
                break;

            case 'O':
                motion_target = MOVE_OBJECT;
                break;

            case 'L':
                motion_target = MOVE_LIGHT;
                break;

            case 'B':
                do_benchmark_run = true;
                redraw_window = true;
                break;

            case 'S':
                screenshot("color.ppm", nullptr);
                break;

            case 'P':
                // XXX - print camera and object matrices
                printf("XXX - print camera and object matrices here\n");
                break;

            case 'D':
                which_diffuse_color = (which_diffuse_color + 1) % diffuse_colors.size();
                redraw_window = true;
                break;

            case 'M':
                which_material = (which_material + 1) % materials.size();
                redraw_window = true;
                break;
        }
    }
}

static int ox, oy;
static int button_pressed = -1;
static bool shift_pressed = false;

static void ButtonCallback(GLFWwindow *window, int b, int action, int mods)
{
    double x, y;
    glfwGetCursorPos(window, &x, &y);

    if(b == GLFW_MOUSE_BUTTON_1 && action == GLFW_PRESS) {
        button_pressed = 1;
        shift_pressed = mods & GLFW_MOD_SHIFT;
        ox = x;
        oy = y;
        redraw_window = true;
    } else {
        button_pressed = -1;
    }
}

static bool gMotionReported = false;

static void MotionCallback(GLFWwindow *window, double x, double y)
{
    // glfw/glfw#103
    // If no motion has been reported yet, we catch the first motion
    // reported and store the current location
    if(!gMotionReported) {
        gMotionReported = true;
        ox = x;
        oy = y;
    }

    double dx, dy;

    dx = x - ox;
    dy = y - oy;

    ox = x;
    oy = y;

    if(button_pressed == 1) {

        if(shift_pressed) {
            zoom *= exp(log(5.0) / gWindowHeight / 2 * -dy);
        } else {
            if(motion_target == MOVE_OBJECT) {
                // XXX reverse of OpenGL
                trackball_motion(object_rotation, -(dx / (float)gWindowWidth), -(dy / (float)gWindowHeight), object_rotation);
            } else {
                trackball_motion(light_rotation, (dx / (float)gWindowWidth), (dy / (float)gWindowHeight), light_rotation);
            }
        }
        update_view_params(gWorld, zoom);
        update_light();
        redraw_window = true;
    }

    ox = x;
    oy = y;
}

#if 0
static void ScrollCallback(GLFWwindow *window, double dx, double dy)
{
    if(motion_target == MOVE_OBJECT) {
        trackball_motion(object_rotation, (dx / (float)gWindowWidth), (dy / (float)gWindowHeight), object_rotation);
    } else {
        trackball_motion(light_rotation, (dx / (float)gWindowWidth), (dy / (float)gWindowHeight), light_rotation);
    }
    update_view_params(gWorld, zoom);
    update_light();
    redraw_window = true;
}
#endif

void ResizeCallback(GLFWwindow *window, int x, int y)
{
    check_opengl(__FILE__, __LINE__);
    glfwGetFramebufferSize(window, &gWindowWidth, &gWindowHeight);

    glViewport(0, 0, gWindowWidth, gWindowHeight);

    check_opengl(__FILE__, __LINE__);
    redraw_window = true;
}

void usage(char *progname)
{
    fprintf(stderr, "usage: %s inputfilename backgroundcolorspec\n", progname);
    fprintf(stderr, "background color can be floats as \"r, g, b\", or hex as \"rrggbb\", or the\n");
    fprintf(stderr, "name of a spheremap texture file.\n");
}

typedef unsigned long long usec_t;

int main(int argc, char *argv[])
{
    GLFWwindow* window;

    glfwSetErrorCallback(ErrorCallback);

    if(!glfwInit()) {
        exit(EXIT_FAILURE);
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE); 

    window = glfwCreateWindow(512, 512, "ray1 interactive program", nullptr, nullptr);
    glfwGetFramebufferSize(window, &gWindowWidth, &gWindowHeight);
    if (!window) {
        glfwTerminate();
        fprintf(stdout, "Couldn't open main window\n");
        exit(EXIT_FAILURE);
    }

    glfwMakeContextCurrent(window);
    
    glfwSetKeyCallback(window, KeyCallback);
    glfwSetMouseButtonCallback(window, ButtonCallback);
    glfwSetCursorPosCallback(window, MotionCallback);
    // glfwSetScrollCallback(window, ScrollCallback);
    glfwSetFramebufferSizeCallback(window, ResizeCallback);
    glfwSetWindowRefreshCallback(window, DrawFrame);

    if(argc < 3) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if((!strcmp(argv[1], "-h")) || (!strcmp(argv[1], "--help"))) {
        usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    if((gWorld = load_world(argv[1])) == nullptr) {
        fprintf(stderr, "Cannot set up world.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stderr, "loaded\n");

    unsigned int rx, gx, bx;
    float rf, gf, bf;
    if(sscanf(argv[2], "%f, %f, %f", &rf, &gf, &bf) == 3) {
        background_image = new float2Dimage(1, 1);
        background_image->pixels[0] = rf;
        background_image->pixels[1] = gf;
        background_image->pixels[2] = bf;
    } else if(strcmp(argv[2], "grid") == 0) {
        const int width = 2048;
        const int height = width / 2;
        const int tilesize = 8;
        const int barsize = 1;
        background_image = new float2Dimage(width, height);
        for(int j = 0; j < height; j++) {
            for(int i = 0; i < width; i++) {
                float *pixel = background_image->pixels + 3 * (width * j + i);
                bool grid = ((i % tilesize) < barsize) || ((j % tilesize) < barsize);
                if(grid) {
                    pixel[0] = 1.0;
                    pixel[1] = 1.0;
                    pixel[2] = 1.0;
                } else { 
                    pixel[0] = 0.0;
                    pixel[1] = 0.0;
                    pixel[2] = 0.0;
                }
            }
        }
    } else if(sscanf(argv[2], "%2x%2x%2x", &rx, &gx, &bx) == 3) {
        background_image = new float2Dimage(1, 1);
        background_image->pixels[0] = rx / 255.0;
        background_image->pixels[1] = gx / 255.0;
        background_image->pixels[2] = bx / 255.0;
    } else {
        fipImage image;
        bool success;

        if (!(success = image.load(argv[2]))) {

            fprintf(stderr, "Failed to load image from %s\n", argv[2]);
            exit(EXIT_FAILURE);

        } else {

            background_image = new float2Dimage(image.getWidth(), image.getHeight());

            if (image.getImageType() == FIT_RGBF) {

                for(int j = 0; j < background_image->height; j++) {
                    const float *src =
                        reinterpret_cast<float*>(image.getScanLine(j));
                    memcpy(background_image->pixels + j * background_image->width * 3, src, background_image->width * sizeof(float) * 3);
                }

            } else if (image.getImageType() == FIT_BITMAP){

                for(int j = 0; j < background_image->height; j++) {
                    for(int i = 0; i < background_image->width; i++) {
                        RGBQUAD src;
                        image.getPixelColor(i, j, &src);
                        float *dst = background_image->pixels + (j * background_image->width + i) * 3;
                        dst[0] = src.rgbRed / 255.0;
                        dst[1] = src.rgbGreen / 255.0;
                        dst[2] = src.rgbBlue / 255.0;
                    }
                }

            } else {

                fprintf(stderr, "Unhandled FIP image type\n");
                exit(EXIT_FAILURE);
            }

        }
    }

    gWorld->xsub = gWorld->ysub = 1;
    gWorld->cam.fov = to_radians(40.0);
    zoom = gWorld->scene_extent / 2 / sinf(gWorld->cam.fov / 2);

    // 20 degrees around an axis halfway between +X and -Y
    light_rotation[0] = to_radians(-20.0);
    light_rotation[1] = .707;
    light_rotation[2] = -.707;
    light_rotation[3] = 0;

    update_view_params(gWorld, zoom);
    update_light();

    init();
    prev_frame_time = std::chrono::system_clock::now();

    redraw_window = true;
    while (!glfwWindowShouldClose(window)) {

        if(do_benchmark_run) {
            const int frame_count = 100;

            float frame_durations[frame_count];
            float frame_min = 1e6;
            float frame_max = 0;


            for(int i = 0; i < frame_count; i++) {
                auto then = std::chrono::system_clock::now();
                DrawFrame(window);
                // glFinish();
                glfwSwapBuffers(window);
                auto now = std::chrono::system_clock::now();
                std::chrono::duration<float> elapsed = now - then;
                frame_durations[i] = elapsed.count();
                frame_min = std::min(elapsed.count(), frame_min);
                frame_max = std::max(elapsed.count(), frame_max);
            }
            glfwSwapBuffers(window);

            printf("%d frames:\n", frame_count);
            const int bucket_count = 10;
            for(int i = 0; i < bucket_count; i++) {
                float duration_range = frame_max - frame_min;
                float bucket_start = frame_min + (duration_range) * i / bucket_count;
                float bucket_end = frame_min + (duration_range) * (i + 1) / bucket_count;
                int count = 0;
                for(int j = 0; j < frame_count; j++) {
                    if(frame_durations[j] >= bucket_start && frame_durations[j] < bucket_end) {
                        count++;
                    }
                }
                printf("%.2f to %.2f ms, %.2f fps : %d\n", bucket_start * 1000.0, bucket_end * 1000.0, 1 / ((bucket_start + bucket_end) / 2.0), count);
            }
            do_benchmark_run = false;
        } else if(redraw_window) {
            DrawFrame(window);
            glfwSwapBuffers(window);
            redraw_window = false;
        }

        if(stream_frames) {
            glfwPollEvents();
        } else {
            glfwWaitEvents();
        }
    }

    glfwTerminate();

    exit(EXIT_SUCCESS);
}
