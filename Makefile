default : ray 

OPTFLAGS        ?=      -O3 -g -ffast-math -ffinite-math-only

LDFLAGS_GL	+=	-L/opt/local/lib  -lglfw -framework OpenGL -framework Cocoa -framework IOkit -lfreeimageplus

LDFLAGS 	+=	

CXXFLAGS	+=	-Wall  -std=c++11 $(OPTFLAGS) -I/opt/local/include/

SOURCES         = ray.cpp world.cpp obj-support.cpp

OBJECTS         = $(SOURCES:.cpp=.o)

clean:
	rm ray $(OBJECTS)

.cpp.o	: 
	$(CXX) -c $(CXXFLAGS) $<

ray: $(OBJECTS)
	$(CXX) -o $@ $^ $(OPTFLAGS) $(LDFLAGS_GL)

