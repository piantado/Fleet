# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk

all:
	g++ Main.cpp -o main -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
debug:
	g++ Main.cpp -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
sanitize:
	g++ Main.cpp -o main -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)
test: all
	./main
