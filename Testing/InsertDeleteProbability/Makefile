
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

all:
	g++ Main.cpp -o main -O3 -fopenmp $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
static:
	g++ Main.cpp -o main -O3 -fopenmp -static -Wl,--whole-archive -lpthread -Wl,--no-whole-archive $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
debug:
	g++ Main.cpp -o main -g -fopenmp $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)

profiled:
	g++ Main.cpp -o main -g -pg -fprofile-arcs -ftest-coverage -fopenmp $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
