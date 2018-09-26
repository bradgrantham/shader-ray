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
ray.o: /usr/include/wchar.h /usr/include/_types.h /usr/include/sys/_types.h
ray.o: /usr/include/sys/cdefs.h /usr/include/sys/_symbol_aliasing.h
ray.o: /usr/include/sys/_posix_availability.h /usr/include/machine/_types.h
ray.o: /usr/include/i386/_types.h /usr/include/sys/_pthread/_pthread_types.h
ray.o: /usr/include/Availability.h /usr/include/AvailabilityInternal.h
ray.o: /usr/include/sys/_types/_null.h /usr/include/sys/_types/_size_t.h
ray.o: /usr/include/sys/_types/_mbstate_t.h /usr/include/machine/types.h
ray.o: /usr/include/i386/types.h /usr/include/sys/_types/_int8_t.h
ray.o: /usr/include/sys/_types/_int16_t.h /usr/include/sys/_types/_int32_t.h
ray.o: /usr/include/sys/_types/_int64_t.h /usr/include/sys/_types/_u_int8_t.h
ray.o: /usr/include/sys/_types/_u_int16_t.h
ray.o: /usr/include/sys/_types/_u_int32_t.h
ray.o: /usr/include/sys/_types/_u_int64_t.h
ray.o: /usr/include/sys/_types/_intptr_t.h
ray.o: /usr/include/sys/_types/_uintptr_t.h
ray.o: /usr/include/sys/_types/_ct_rune_t.h /usr/include/sys/_types/_rune_t.h
ray.o: /usr/include/sys/_types/_wchar_t.h /usr/include/stdio.h
ray.o: /usr/include/_stdio.h /usr/include/sys/_types/_va_list.h
ray.o: /usr/include/sys/stdio.h /usr/include/sys/_types/_off_t.h
ray.o: /usr/include/sys/_types/_ssize_t.h /usr/include/secure/_stdio.h
ray.o: /usr/include/secure/_common.h /usr/include/time.h
ray.o: /usr/include/sys/_types/_clock_t.h /usr/include/sys/_types/_time_t.h
ray.o: /usr/include/sys/_types/_timespec.h /usr/include/_wctype.h
ray.o: /usr/include/__wctype.h /usr/include/sys/_types/_wint_t.h
ray.o: /usr/include/_types/_wctype_t.h /usr/include/ctype.h
ray.o: /usr/include/_ctype.h /usr/include/runetype.h /usr/include/inttypes.h
ray.o: /usr/include/stdint.h /usr/include/_types/_uint8_t.h
ray.o: /usr/include/_types/_uint16_t.h /usr/include/_types/_uint32_t.h
ray.o: /usr/include/_types/_uint64_t.h /usr/include/_types/_intmax_t.h
ray.o: /usr/include/_types/_uintmax_t.h /opt/local/include/GLFW/glfw3.h
ray.o: /usr/include/stddef.h /usr/include/sys/_types/_offsetof.h
ray.o: /usr/include/sys/_types/_ptrdiff_t.h
ray.o: /usr/include/sys/_types/_rsize_t.h /opt/local/include/GL/glcorearb.h
ray.o: world.h vectormath.h geometry.h triangle-set.h group.h
world.o: triangle-set.h vectormath.h geometry.h obj-support.h
world.o: trisrc-support.h group.h bvh.h world.h
obj-support.o: obj-support.h vectormath.h triangle-set.h geometry.h
trisrc-support.o: vectormath.h geometry.h triangle-set.h obj-support.h
bvh.o: bvh.h group.h triangle-set.h vectormath.h geometry.h
group.o: group.h triangle-set.h vectormath.h geometry.h
