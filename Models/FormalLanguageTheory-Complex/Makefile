include ../../Fleet.mk

all:
	g++-8 -o main Main.cpp -lm -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE)
	
profiled:
	g++ -o main Main.cpp -lm -O2 -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE)

clang:
	clang++ Main.cpp -o main -lm -O2 $(CLANG_FLAGS) $(FLEET_INCLUDE)
	
debug:
	g++ -o main Main.cpp -lm -O2 -pg -p -g $(FLEET_FLAGS) $(FLEET_INCLUDE)

sanitize:
	g++ -o main Main.cpp -lm -O2 -lasan -fsanitize=address -fno-omit-frame-pointer $(FLEET_FLAGS) $(FLEET_INCLUDE)


	
