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

#include "triangle-set.h"
#include "geometry.h"

struct group
{
    vec3 D; /* split direction */

    box3d box;

    group *negative, *positive;
    group *dirhit[8], *dirmiss[8];

    triangle_set& triangles;
    int start;
    unsigned int count;

    group(triangle_set& triangles_, group *neg, group *pos, const vec3& direction, const box3d& box_);
    group(triangle_set& triangles_, int start_, unsigned int count_);
    ~group();

    int my_index;
};

