#pragma once
#include <string>
#include <vector>
#include "vectormath.h"

//
// OBJ file format - https://en.wikipedia.org/wiki/Wavefront_.obj_file
//
// Quick summary: ASCII file describing a 3D mesh.  Each line describes
//                a single vertex attribute or face of a triangle.
//
//
struct triangle_set;
class Obj
{
    struct Face
    {
        // Strictly speaking these should be signed as face descriptions
        // can be relative (negative).
        struct VertexIndex
        {
            unsigned int v;
            unsigned int vn;
            unsigned int vt;
        };
        unsigned int which_attribs;
        std::vector<VertexIndex> indices;
    };

    void get_attrib(const std::string& description, vec3& v);
    void get_face(const std::string& description, Face& f);
    void face_get_index(const std::string& tuple, unsigned int& which_attribs,
                        unsigned int& v, unsigned int& vn, unsigned int& vt);
    void compute_normals();

    std::vector<vec3> positions;
    std::vector<vec3> normals;
    std::vector<vec3> texcoords;
    std::vector<Face> faces;

    // There may be more of these (certainly more tokens are defined
    // for OBJ files in general), but this should get us going and
    // we can emit a notification when we hit an undefined type.
    static const std::string object_description;
    static const std::string vertex_description;
    static const std::string normal_description;
    static const std::string texcoord_description;
    static const std::string face_description;

    static const unsigned int FACE_ATTRIB_NONE;
    static const unsigned int FACE_ATTRIB_POSITION;
    static const unsigned int FACE_ATTRIB_NORMAL;
    static const unsigned int FACE_ATTRIB_TEXCOORD;

public:
    Obj();
    ~Obj();

    bool load_object_from_file(const std::string& filename);
    bool fill_triangle_set(triangle_set& triangles);
};
