include ../../Fleet.mk

all:
	g++ Main.cpp -o main -lm -O3 $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/

debug:
	g++ Main.cpp -o main -lm -g $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/
	
profiled:
	g++ -o main Main.cpp -lm -O3 -pg -p -ffast-math $(FLEET_FLAGS) $(FLEET_INCLUDE) -I  /usr/include/eigen3/
