#export GCC_COLORS='error=01;31:warning=01;35:note=01;36:caret=01;32:locus=01:quote=01'

# Include directories
#FLEET_INCLUDE=-I../../src/ -I../../src/Nodes -I../../src/Inference -I../../src/Hypotheses
FLEET_INCLUDE=-I../../src/ -I../../src/Inference

# Some standard/default flags
FLEET_FLAGS=-std=gnu++17 -Wall  -fdiagnostics-color=auto -lpthread

