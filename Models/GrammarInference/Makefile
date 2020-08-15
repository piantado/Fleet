
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

all:
	g++-9 Main.cpp -o main -lm -O3 -fopenmp $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/local/include/eigen3/

debug:
	g++ Main.cpp -o main -lm -g -fopenmp $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/local/include/eigen3/
	
profiled:
	g++ -o main Main.cpp -lm -O3 -pg -p -g -fopenmp $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/local/include/eigen3/
clang:
	clang++ Main.cpp -o main -O2 -fopenmp $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) -stdlib=libstdc++ -I  /usr/local/include/eigen3/

