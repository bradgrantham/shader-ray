/*
   Copyright 2018 Jesse Barker.
   Modifications copyright 2018 Brad Grantham
   
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

#include "obj-support.h"
#include "triangle-set.h"
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <sstream>

// Parsing helpers
namespace
{

template<typename T>
static T
fromString(const std::string& asString)
{
    std::stringstream ss(asString);
    T retVal = T();
    ss >> retVal;
    return retVal;
}

template<typename T>
static std::string
toString(const T t)
{
    std::stringstream ss;
    ss << t;
    return ss.str();
}

void split_tuple_exact(const std::string& tuple, char delimiter, std::vector<std::string>& elements)
{
    std::stringstream ss(tuple);
    std::string item;
    while (std::getline(ss, item, delimiter))
    {
        elements.push_back(item);
    }
}

void split_tuple_fuzzy(const std::string& tuple, char delimiter, std::vector<std::string>& elements)
{
    // For a fuzzy split, we want to add whitespace to the base delimiter
    std::string fuzzyDelimiter(" ");
    fuzzyDelimiter += delimiter;

    std::string::size_type startPos(0);
    std::string item(tuple);
    std::string::size_type endPos = item.find_first_of(fuzzyDelimiter);
    while (endPos != std::string::npos)
    {
        // We found at least one instance of something from the fuzzy delimiter
        // cut/paste the initial item and find the next one (if any)
        elements.push_back(std::string(item, startPos, endPos - startPos));
        std::string::size_type nextPos = item.find_first_not_of(fuzzyDelimiter, endPos);
        item = item.erase(startPos, nextPos - startPos);
        endPos = item.find_first_of(fuzzyDelimiter);
    }

    // As long as 'tuple' had at least one element in it, 'item' will contain
    // the final one at this point.
    elements.push_back(item);
}

} // Anonymous namespace

const std::string Obj::object_description("o");
const std::string Obj::vertex_description("v");
const std::string Obj::normal_description("vn");
const std::string Obj::texcoord_description("vt");
const std::string Obj::face_description("f");

const unsigned int Obj::FACE_ATTRIB_NONE = 0;
const unsigned int Obj::FACE_ATTRIB_POSITION = 0x1;
const unsigned int Obj::FACE_ATTRIB_NORMAL = 0x2;
const unsigned int Obj::FACE_ATTRIB_TEXCOORD = 0x4;

Obj::Obj()
{
}

Obj::~Obj()
{
}

void Obj::compute_normals()
{
    normals.resize(positions.size(), vec3(0.0));

    for (auto & face : faces)
    {
        const unsigned int idx0 = 0;
        unsigned int idx1 = 1;
        unsigned int idx2 = 2;
        Face::VertexIndex& vi0 = face.indices[idx0];
        const unsigned int numTris = face.indices.size() - 2;
        for (unsigned int t = 0; t < numTris; ++t, ++idx1, ++idx2)
        {
            Face::VertexIndex& vi1 = face.indices[idx1];
            Face::VertexIndex& vi2 = face.indices[idx2];

            // Compute the area-weighted face normal for this face
            vec3& v0 = positions[vi0.v];
            vec3& v1 = positions[vi1.v];
            vec3& v2 = positions[vi2.v];
            vec3 fn = cross(v1 - v0, v2 - v0);

            // Accumulate the per-vertex contribution of the normal
            face.which_attribs |= FACE_ATTRIB_NORMAL;
            vi0.vn = vi0.v;
            vi1.vn = vi1.v;
            vi2.vn = vi2.v;

            vec3& vn0 = normals[vi0.vn];
            vec3& vn1 = normals[vi1.vn];
            vec3& vn2 = normals[vi2.vn];

            vn0 = vn0 + fn;
            vn1 = vn1 + fn;
            vn2 = vn2 + fn;
        }
    }

    for (auto & normal : normals)
    {
        normal = normalize(normal);
    }
}

void Obj::get_attrib(const std::string& description, vec3& v)
{
    // Vertex attributes are whitespace separated, so use fuzzy split
    std::vector<std::string> elements;
    split_tuple_fuzzy(description, ' ', elements);

    unsigned int numElements = elements.size();
    switch (numElements)
    {
        case 3:
            v.z = fromString<float>(elements[2]);
        case 2:
            v.y = fromString<float>(elements[1]);
        case 1:
            v.x = fromString<float>(elements[0]);
            break;
        default:
            std::cerr << "Trying to handle tuple with " << numElements
                << " elements!" << std::endl;
            break;
    }
}

void Obj::face_get_index(const std::string& tuple, unsigned int& which_attribs,
    unsigned int& v, unsigned int& vn, unsigned int& vt)
{
    // Attribute indices within a face description require no spaces around
    // the '/', so use an exact split
    std::vector<std::string> elements;
    split_tuple_exact(tuple, '/', elements);

    which_attribs = FACE_ATTRIB_NONE;
    if (elements.empty())
    {
        return;
    }

    // However we end up stashing face information, it is important to account
    // for the fact that OBJ models have a base 1 (rather than base 0) index
    // enumeration, so each index needs to have 1 subtracted.
    which_attribs |= FACE_ATTRIB_POSITION;
    v = fromString<unsigned int>(elements[0]) - 1;

    unsigned int numElements = elements.size();

    if (numElements > 1 && !elements[1].empty())
    {
        which_attribs |= FACE_ATTRIB_TEXCOORD;
        vt = fromString<unsigned int>(elements[1]) - 1;
    }

    if (numElements > 2 && !elements[2].empty())
    {
        which_attribs |= FACE_ATTRIB_NORMAL;
        vn = fromString<unsigned int>(elements[2]) - 1;
    }
}

void Obj::get_face(const std::string& description, Face& f)
{
    // Tuples of indices are whitespace separated, so use fuzzy split
    // (e.g., "f v/vt/vn v/vt/vn v/vt/vn ...")
    std::vector<std::string> elements;
    split_tuple_fuzzy(description, ' ', elements);

    unsigned int which_attribs(FACE_ATTRIB_NONE);
    const unsigned int numElements = elements.size();
    f.indices.reserve(numElements);
    for (unsigned int i = 0; i < numElements; i++)
    {
        Face::VertexIndex vi = {0};
        face_get_index(elements[i], which_attribs, vi.v, vi.vn, vi.vt);
        f.indices.push_back(vi);
    }

    f.which_attribs = which_attribs;
}

bool Obj::load_object_from_file(const std::string& filename)
{
    // Open the file, get an input stream
    std::ifstream inputFile(filename);
    if (!inputFile.is_open())
    {
        // Complain about open fail
        return false;
    }

    // Scan in the source lines
    std::vector<std::string> sourceLines;
    std::string curLine;
    while (getline(inputFile, curLine))
    {
        sourceLines.push_back(curLine);
    }

    // Process the input source and generate lists of attributes and faces
    for (auto & lineIt : sourceLines)
    {
        const std::string& curDesc = lineIt;
        if (curDesc.empty() || curDesc[0] == '#')
        {
            // Skip blank lines and comments
            continue;
        }

        // Find what sort of description we're looking at on the current line
        // We are currently ignoring everything other than vertex attributes
        // and face descriptions.
        std::string::size_type startPos(0);
        std::string::size_type spacePos = curDesc.find(" ", startPos);
        std::string::size_type numChars(std::string::npos);
        std::string description;
        if (spacePos != std::string::npos)
        {
            // Absorb any whitespace between the description type and the data
            std::string::size_type descPos = curDesc.find_first_not_of(' ', spacePos);
            description = std::string(curDesc, descPos);
            numChars = spacePos - startPos;
        }
        const std::string descriptionType(curDesc, startPos, numChars);

        if (descriptionType == object_description)
        {
            std::cout << "Found object '" << description << "'" << std::endl;
        }
        else if (descriptionType == vertex_description)
        {
            vec3 p(0);
            get_attrib(description, p);
            positions.push_back(p);
        }
        else if (descriptionType == normal_description)
        {
            vec3 n(0);
            get_attrib(description, n);
            normals.push_back(n);
        }
        else if (descriptionType == texcoord_description)
        {
            vec3 t(0);
            get_attrib(description, t);
            texcoords.push_back(t);
        }
        else if (descriptionType == face_description)
        {
            Face f = {0};
            get_face(description, f);
            faces.push_back(f);
        }
    }

    std::cout << "Got " << faces.size() << " face descriptions" << std::endl;
    std::cout << "Got " << positions.size() << " vertex descriptions" << std::endl;
    if (!normals.empty())
    {
        std::cout << "Got " << normals.size() << " normal descriptions" << std::endl;
    }
    else
    {
        std::cout << "Generating normals..." << std::endl;
        compute_normals();
    }

    if (!texcoords.empty())
    {
        std::cout << "Got " << texcoords.size() << "texcoord descriptions" << std::endl;
    }

    return true;
}

bool Obj::fill_triangle_set(triangle_set_ptr triangles)
{
    // Convert from face-vertex mesh to list of triangles
    for (auto & face : faces)
    {
        const unsigned int idx0 = 0;
        unsigned int idx1 = 1;
        unsigned int idx2 = 2;
        const Face::VertexIndex& vi0 = face.indices[idx0];
        const unsigned int numTris = face.indices.size() - 2;
        for (unsigned int t = 0; t < numTris; ++t, ++idx1, ++idx2)
        {
            const Face::VertexIndex& vi1 = face.indices[idx1];
            const Face::VertexIndex& vi2 = face.indices[idx2];
            vertex vtx[3];
            vtx[0].v = positions[vi0.v];
            vtx[1].v = positions[vi1.v];
            vtx[2].v = positions[vi2.v];
            if (face.which_attribs & FACE_ATTRIB_NORMAL)
            {
                vtx[0].n = normals[vi0.vn];
                vtx[1].n = normals[vi1.vn];
                vtx[2].n = normals[vi2.vn];
            }
            vtx[0].c = vtx[1].c = vtx[2].c = vec3(1.0, 1.0, 1.0);
            triangles->add(vtx[0], vtx[1], vtx[2]);
        }
    }

    return true;
}

