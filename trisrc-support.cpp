#include <cstdio>
#include <cmath>
#include "vectormath.h"
#include "geometry.h"
#include "triangle-set.h"
#include "obj-support.h"

const float screengamma = 2.63;

static float geometryScaleFactor = 1.0f;
static bool correctFileColorGamma = true;

static void initialize_runtime_parameters() __attribute__((constructor));
static void initialize_runtime_parameters()
{
    if(getenv("COLORS_ARE_LINEAR") != 0) {
        correctFileColorGamma = false;
        fprintf(stderr, "file colors are linear\n");
    }
    if(getenv("GEOMETRY_SCALE") != 0) {
        geometryScaleFactor = atof(getenv("GEOMETRY_SCALE"));
        fprintf(stderr, "geometry scale set to %f\n", geometryScaleFactor);
    }
}


bool ParseTriSrc(FILE *fp, triangle_set& triangles)
{
    char texture_name[512];
    char tag_name[512];
    float specular_color[4];
    float shininess;

    while(fscanf(fp,"\"%[^\"]\"", texture_name) == 1) {
        if(strcmp(texture_name, "*") == 0) {
            texture_name[0] = '\0';
        }

        if(fscanf(fp,"%s ", tag_name) != 1) {
            fprintf(stderr, "couldn't read tag name\n");
            return false;
        }

        if(fscanf(fp,"%g %g %g %g %g ", &specular_color[0], &specular_color[1],
            &specular_color[2], &specular_color[3], &shininess) != 5) {
            fprintf(stderr, "couldn't read specular properties\n");
            return false;
        }

        if(shininess > 0 && shininess < 1) {
            // shininess is not exponent - what is it?
            shininess *= 10;
        }

        float v[3][3];
        float n[3][3];
        float c[3][4];
        float t[3][2];
        for(int i = 0; i < 3; i++) {

            if(fscanf(fp,"%g %g %g %g %g %g %g %g %g %g %g %g ",
                &v[i][0], &v[i][1], &v[i][2],
                &n[i][0], &n[i][1], &n[i][2],
                &c[i][0], &c[i][1], &c[i][2], &c[i][3],
                &t[i][0], &t[i][1]) != 12) {

                fprintf(stderr, "couldn't read Vertex\n");
                return false;
            }
        }

        //MATERIAL mtl(texture_name, specular_color, shininess);

        vertex vtx[3];
        for(int i = 0; i < 3; i++) {
            vtx[i].v = vec3(v[i][0], v[i][1], v[i][2]) * geometryScaleFactor;
            if(correctFileColorGamma) {
                vtx[i].c.set(pow(c[i][0], screengamma), pow(c[i][1], screengamma), pow(c[i][2], screengamma));
            } else {
                vtx[i].c.set(c[i][0], c[i][1], c[i][2]);
            }
            vtx[i].n.set(n[i][0], n[i][1], n[i][2]);
            vtx[i].n = normalize(vtx[i].n);
        }
        triangles.add(vtx[0], vtx[1], vtx[2]);
    }
    return true;
}


