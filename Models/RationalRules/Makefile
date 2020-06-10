
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk


all:
	g++ Main.cpp -o main -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
static:
	g++ Main.cpp -o main -O3 -static -Wl,--whole-archive -lpthread -Wl,--no-whole-archive $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
debug:
	g++ Main.cpp -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
clang:
	clang++ Main.cpp -o main -O2 $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
profiled:
	g++ Main.cpp -o main -g -pg -fprofile-arcs -ftest-coverage $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/ $(FLEET_LIBS)
