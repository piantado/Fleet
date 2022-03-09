
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

# FLAGS here is meant to be passed in on the make command line

all:
	g++ -o main Main.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) $(FLAGS)
	
profiled:
	g++ -o main Main.cpp -O2 -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) $(FLAGS)

debug:
	g++ -Wall -fdiagnostics-color=auto -o main Main.cpp -g -pg $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) $(FLAGS)
	
