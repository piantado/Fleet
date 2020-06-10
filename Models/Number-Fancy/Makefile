
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

all:
	g++-9 -o main Main.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)

debug:
	g++ -o main Main.cpp -g -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
clang:
	clang++ Main.cpp -o main -O2 $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
	
postprocess:
	g++ -o pp Postprocess.cpp -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
