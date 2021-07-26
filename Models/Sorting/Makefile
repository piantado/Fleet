
# Define where Fleet lives (directory containing src)
FLEET_ROOT=../../

include $(FLEET_ROOT)/Fleet.mk


all:
	g++ Main.cpp -o main -O2 $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS)  -I  /usr/local/include/eigen3/
clang:
	clang++ Main.cpp -o main -O2 $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) -I  /usr/local/include/eigen3/ 
web:
	em++ Main.cpp -s WASM=1 $(CLANG_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) -I  /usr/local/include/eigen3/ -s ERROR_ON_UNDEFINED_SYMBOLS=0 -o main.html
debug:
	g++ Main.cpp -O0 -o main -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) -I  /usr/local/include/eigen3/
profiled:
	g++ Main.cpp -o main -O2 -pg -g $(FLEET_FLAGS) $(FLEET_INCLUDE) $(FLEET_LIBS) -I  /usr/local/include/eigen3/
