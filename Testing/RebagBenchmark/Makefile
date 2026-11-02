FLEET_ROOT=../../
include $(FLEET_ROOT)/Fleet.mk
STD ?= -std=c++26
all:
	g++ Main.cpp -o main -O2 $(STD) $(filter-out -std=% -march=native,$(FLEET_FLAGS)) $(FLEET_INCLUDE) -I.. $(FLEET_LIBS)
