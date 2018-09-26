This is an interactive ray-tracer implemented almost entirely in an OpenGL ES fragment shader.  It features triangles, a BVH (built on the CPU at load time), physically based materials, and HDR environment maps.

![Gold Stanford Bunny in Pisa](bunny.jpg "Gold Stanford Bunny in Pisa") ![Glazed plaster Stanford Bunny in Pisa](bunny2.jpg "Glazed plaster Stanford Bunny in Pisa")

It runs on MacOS and requires GLFW and FreeImagePlus.  Build it using ```make```.  We compile it using MacPorts defaults - for HomeBrew you may need to change the Makefile.

Run it like so:

```
./ray model environment
```

The loader supports a private "trisrc" format and Wavefront OBJ files.
For models and environment images, check out https://github.com/bradgrantham/scene-data .  Try models/bunny.trisrc (may need to be uncompressed after checking out) and images/pisa.hdr.

Press 'm' to cycle through materials.  For the last material in the list, which is a diffuse glazed plaster-like material, press 'd' to cycle through diffuse material colors.  The global material replaces all the objects material attributes (at the moment).

Click and drag to move the object.  Press and hold the Shift key to zoom in and out.  Press 'l' to switch to rotating the light (visible on diffuse materials) and 'o' to switch back to moving the object.

Press 's' to save a screenshot to the file "color.ppm".

# License

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
