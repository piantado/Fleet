include ../../Fleet.mk

all:
	g++-9 -Wall -fdiagnostics-color=auto -o main Main.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

debug:
	g++-9 -Wall -fdiagnostics-color=auto -o main Main.cpp -g -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
postprocess:
	g++-9 -Wall -fdiagnostics-color=auto -o pp Postprocess.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
