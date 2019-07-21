include ../../Fleet.mk

all:
	g++ -o main Main.cpp -lm -O3 -ffast-math $(FLEET_FLAGS) $(FLEET_INCLUDE)
	
profiled:
	g++ -o main Main.cpp -lm -O3 -pg -p -ffast-math $(FLEET_FLAGS) $(FLEET_INCLUDE)

debug:
	g++ -o main Main.cpp -lm -O3 -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE)

sanitize:
	g++ -o main Main.cpp -lm -O3 -lasan -fsanitize=address -fno-omit-frame-pointer $(FLEET_FLAGS) $(FLEET_INCLUDE)


	
