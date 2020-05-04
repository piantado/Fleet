include ../../Fleet.mk

all:
	g++ -o main Main.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
profiled:
	g++ -o main Main.cpp -O2 -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

clang:
	clang++ Main.cpp -o main -O2 $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
debug:
	g++ -o main Main.cpp -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

sanitize:
	g++ -o main Main.cpp -O2 -lasan -fsanitize=address -fno-omit-frame-pointer $(FLEET_FLAGS) $(FLEET_INCLUDE)


	
