include ../../Fleet.mk

all:
	g++ -std=gnu++14 -o main Main.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

debug:
	g++ -std=gnu++14 -Wall -fdiagnostics-color=auto -o main Main.cpp -g -pg $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
