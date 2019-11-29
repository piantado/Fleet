include ../../Fleet.mk

all:
	g++ -o main Main.cpp -lm -O2 -ffast-math $(FLEET_FLAGS) $(FLEET_INCLUDE)
	
profiled:
	g++ -o main Main.cpp -lm -O2 -pg -p -g -ffast-math $(FLEET_FLAGS) $(FLEET_INCLUDE)

debug:
	g++ -o main Main.cpp -lm -O2 -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE)

sanitize:
	g++ -o main Main.cpp -lm -O2 -lasan -fsanitize=address -fno-omit-frame-pointer $(FLEET_FLAGS) $(FLEET_INCLUDE)


	
