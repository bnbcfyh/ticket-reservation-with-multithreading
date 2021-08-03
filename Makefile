all: compile

compile:
	@g++ code.cpp -o simulation.o -lpthread