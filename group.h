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
    negative(nullptr),
    positive(nullptr),
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

