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

#pragma once

#include "vectormath.h"
#include <algorithm>

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
    operator bool() { return t0 < t1; }
};

struct vertex {
    vec3 v;
    vec3 c;
    vec3 n;
};

struct triangle
{
    vec3 v[3];
    vec3 c[3];
    vec3 n[3];

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
