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

uniform mat4 modelview;
uniform highp mat4 camera_matrix;
uniform highp mat4 camera_normal_matrix;
uniform float aspect;
uniform float image_plane_width;

in vec4 pos;
in vec2 vtex;
out highp vec3 world_ray_origin;
out highp vec3 world_ray_direction;

struct ray {
    highp vec3 o;
    highp vec3 d;
};

void ray_transform(in ray r, in mat4 matrix, in mat4 normal_matrix, out ray t)
{
    t.o = (matrix * vec4(r.o, 1.0)).xyz;
    t.d = (normal_matrix * vec4(r.d, 0.0)).xyz;
}

ray image_plane_ray(highp float u, highp float v, highp float aspect, highp float image_plane_width)
{
    ray eyeray;

    // u is 0.0 at left, 1.0 at right
    // v is 0.0 at bottom, 1.0 at top
    eyeray.d = normalize(vec3(image_plane_width * (u - 0.5), image_plane_width * (v - 0.5) * aspect, -1.0));
    eyeray.o = vec3(0.0, 0.0, 0.0);

    return eyeray;
}

void main()
{
    gl_Position = modelview * pos;

    ray eyeray, worldray;
    eyeray = image_plane_ray(vtex[0], 1.0 - vtex[1], aspect, image_plane_width);
    ray_transform(eyeray, camera_matrix, camera_normal_matrix, worldray);
    world_ray_origin = worldray.o;
    world_ray_direction = worldray.d;
}
