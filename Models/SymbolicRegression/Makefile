include ../../Fleet.mk

all:
	g++ -std=gnu++14 -Wall -fdiagnostics-color=auto -o main Main.cpp -lm -g -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE)

debug:
	g++ -std=gnu++14 -Wall -fdiagnostics-color=auto -o main Main.cpp -lm -g -pg $(FLEET_FLAGS) $(FLEET_INCLUDE)
	
