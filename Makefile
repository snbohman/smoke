#_____________________VARIABLES____________________
CC=clang++

# FORMAT: bin/
EXE_PATH:=bin/
EXE_NAME=$(EXE_PATH)main


SRC=src/*.cpp
SRC_DIR=src
INC_DIR=include
OBJ_PATH=build/
CPP_VERSION=20

# Add -O3 for preformance
# For debbing, refrain to using -O3
CFLAGS := -Wall -g -I$(INC_DIR) -std=c++$(CPP_VERSION) -Wno-reorder-ctor
LDFLAGS := 

#_____________________COMPILE______________________
all: compile link clean_opt run
.PHONY: all

debug: debugcompile debug_clean
.PHONY: debug

debugcompile:
	$(CC) --debug $(SRC) $(CFLAGS) $(LDFLAGS)
.PHONY: debugcompile

debug_clean:
	rm -r $(EXE_PATH)debug.dSYM
	mv a.out $(EXE_PATH)debug
	mv a.out.dSYM $(EXE_PATH)debug.dSYM

compile:
	$(CC) -c $(SRC) $(CFLAGS)
.PHONY: compile

link:
	$(CC) ./*.o $(LDFLAGS)
.PHONY: link

#_____________________CLEAN________________________
clean_opt: obj exe

ifdef OBJ_PATH
obj:
	mv ./*.o $(OBJ_PATH)
else
obj:
	mkdir -p $(OBJ_PATH)
	mv ./*.o $(OBJ_PATH)
endif

ifdef EXE_PATH
exe:
	mkdir -p $(EXE_PATH)
	mv a.out $(EXE_NAME)
else
exe:
	mv a.out $(EXE_NAME)
endif

#_____________________RUN__________________________
run:
	./$(EXE_NAME)
.PHONY: run
