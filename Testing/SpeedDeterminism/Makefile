# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

all:
	g++ Main.cpp -o main -O2 -pthread $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
