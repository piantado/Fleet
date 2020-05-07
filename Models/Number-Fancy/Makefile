include ../../Fleet.mk

all:
	g++ -o main Main.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

debug:
	g++ -o main Main.cpp -g -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
postprocess:
	g++ -o pp Postprocess.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
