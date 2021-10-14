/*
   Copyright 2018 Jesse Barker.
   
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

#pragma once
#include <string>
#include <vector>
#include "vectormath.h"
#include "triangle-set.h"

//
// OBJ file format - https://en.wikipedia.org/wiki/Wavefront_.obj_file
//
// Quick summary: ASCII file describing a 3D mesh.  Each line describes
//                a single vertex attribute or face of a triangle.
//
//

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
    bool fill_triangle_set(triangle_set_ptr triangles);
};
