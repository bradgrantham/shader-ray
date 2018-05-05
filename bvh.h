#include "group.h"
#include "triangle-set.h"

group* make_bvh(triangle_set& triangles, int start, unsigned int count, int level = 0);
void print_bvh_stats();
