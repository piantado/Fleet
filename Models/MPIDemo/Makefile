
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk


all:
	mpic++ Main.cpp -o main -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) 
debug:
	mpicc++ Main.cpp -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
