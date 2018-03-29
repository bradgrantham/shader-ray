default : ray 

# OPTFLAGS        ?=      -O3 -ffast-math -msse4.1
OPTFLAGS        ?=      -O3 -g -march=nocona -mtune=nocona -fstrict-aliasing -ffast-math -ffinite-math-only -mavx

LDFLAGS_GL	+=	-L/opt/local/lib  -lglfw -framework OpenGL -framework Cocoa -framework IOkit -lfreeimageplus

LDFLAGS 	+=	

CXXFLAGS	+=	-Wall  $(OPTFLAGS) -I/opt/local/include/

clean:
	rm ray

world.o: world.h vectormath.h
glesray1.o: world.h vectormath.h

.cpp.o	: 
	$(CXX) -c $(CXXFLAGS) $<

ray: ray.o world.o
	$(CXX) -o $@ $^ $(OPTFLAGS) $(LDFLAGS_GL)

