#
# Copyright 2018 Brad Grantham
# Modifications copyright 2018 Jesse Barker
# 
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
# 
#     http://www.apache.org/licenses/LICENSE-2.0
# 
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

default : ray 

OPTFLAGS        ?=      -O3 -g -ffast-math -ffinite-math-only

LDFLAGS_GL	+=	-L/opt/local/lib  -lglfw -framework OpenGL -framework Cocoa -framework IOkit -lfreeimageplus

LDFLAGS 	+=	

INCFLAGS        +=      -I/opt/local/include/

CXXFLAGS	+=	-Wall  -std=c++11 $(OPTFLAGS) $(INCFLAGS)

SOURCES         = ray.cpp world.cpp obj-support.cpp trisrc-support.cpp bvh.cpp group.cpp

OBJECTS         = $(SOURCES:.cpp=.o)

clean:
	rm ray $(OBJECTS)

.cpp.o	: 
	$(CXX) -c $(CXXFLAGS) $<

ray: $(OBJECTS)
	$(CXX) -o $@ $^ $(OPTFLAGS) $(LDFLAGS_GL)

depend: $(SOURCES)
	makedepend -- $(INCFLAGS) -- $^

# DO NOT DELETE

ray.o: /opt/local/include/FreeImagePlus.h /opt/local/include/FreeImage.h
ray.o: /opt/local/include/GLFW/glfw3.h /opt/local/include/GL/glcorearb.h
ray.o: world.h vectormath.h geometry.h triangle-set.h group.h
world.o: triangle-set.h vectormath.h geometry.h obj-support.h
world.o: trisrc-support.h group.h bvh.h world.h
obj-support.o: obj-support.h vectormath.h triangle-set.h geometry.h
trisrc-support.o: vectormath.h geometry.h triangle-set.h obj-support.h
bvh.o: bvh.h group.h triangle-set.h vectormath.h geometry.h
group.o: group.h triangle-set.h vectormath.h geometry.h
