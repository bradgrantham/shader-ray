#pragma once
#include <string>
#include <vector>
#include <cstdio>
#include "vectormath.h"
#include "vectormath.h"

struct triangle_set;

bool ParseTriSrc(FILE *fp, triangle_set& triangles);
