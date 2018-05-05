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

