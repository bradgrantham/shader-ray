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

#include "group.h"

group::group(triangle_set_ptr triangles_, group *neg, group *pos, const vec3& direction, const box3d& box_) :
    D(direction),
    box(box_),
    negative(neg),
    positive(pos),
    triangles(triangles_),
    start(0),
    count(0)
{
}

group::group(triangle_set_ptr triangles_, int start_, unsigned int count_) :
    negative(nullptr),
    positive(nullptr),
    triangles(triangles_),
    start(start_),
    count(count_)
{
    for(unsigned int i = 0; i < count; i++) {
        triangle t = triangles->get(start + i);
        box.add(t.v[0], t.v[1], t.v[2]);
    }
}

group::~group()
{
    delete negative;
    delete positive;
}

