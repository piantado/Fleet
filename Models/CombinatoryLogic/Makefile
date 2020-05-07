include ../../Fleet.mk

all:
	g++ Main.cpp -o main -lm -O3 $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/
static:
	g++ Main.cpp -o main -lm -O3 -static -Wl,--whole-archive -lpthread -Wl,--no-whole-archive $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/
debug:
	g++ Main.cpp -o main -lm -g $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/

profiled:
	g++ Main.cpp -o main -lm -g -pg -fprofile-arcs -ftest-coverage $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/
