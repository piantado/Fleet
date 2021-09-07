
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk


all:
	g++ Main.cpp -o main -O2 -pthread $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) 
clang:
	clang++ Main.cpp -o main -O2 $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
debug:
	g++ Main.cpp -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
profiled:
	g++ Main.cpp -o main -O2 -pg -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
