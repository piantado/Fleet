include ../../Fleet.mk

all:
	g++ -o main Main.cpp -lm -O3 -ffast-math $(FLEET_FLAGS) $(FLEET_INCLUDE)

debug:
	g++ -o main Main.cpp -lm -O0 -g $(FLEET_FLAGS) $(FLEET_INCLUDE)
	
