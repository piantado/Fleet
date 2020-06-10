
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

all:
	g++ -o main Main.cpp -O3 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

debug:
	g++ -o main Main.cpp -g -pg $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
