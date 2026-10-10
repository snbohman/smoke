#_____________________VARIABLES____________________

CXX := clang++

EXE_PATH := bin
EXE_NAME := $(EXE_PATH)/main

SRC_DIR := src
INC_DIR := include
OBJ_PATH := build

CPP_VERSION := 20

MOSS_PATH := ../moss/core/include
MOSS_EXT_PATH := ../moss/extensions/include
MOSS_EXT_LIB_PATH := ../moss/extensions/bin/debug

SRC := $(wildcard $(SRC_DIR)/*.cpp)
OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_PATH)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

CXXFLAGS := -Wall -g \
	-I$(INC_DIR) \
	-I$(MOSS_PATH) \
	-I$(MOSS_EXT_PATH) \
	-std=c++$(CPP_VERSION) \
	-Wno-reorder-ctor \
	-MMD -MP

LDFLAGS := -L$(MOSS_EXT_LIB_PATH)
LDLIBS := -lvulkan -lmossExtensions -lvk-bootstrap -lglfw


#_____________________BUILD_________________________

.PHONY: all compile link run debug clean

all: $(EXE_NAME) run

compile: $(OBJ)

link: $(EXE_NAME)

$(OBJ_PATH)/%.o: $(SRC_DIR)/%.cpp
	mkdir -p $(OBJ_PATH)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(EXE_NAME): $(OBJ)
	mkdir -p $(EXE_PATH)
	$(CXX) $(OBJ) $(LDFLAGS) $(LDLIBS) -o $@


#_____________________DEBUG_________________________

debug: $(EXE_NAME)
	./$(EXE_NAME)


#_____________________RUN___________________________

run: $(EXE_NAME)
	./$(EXE_NAME)


#_____________________CLEAN_________________________

clean:
	rm -rf $(OBJ_PATH) $(EXE_PATH)/main

#_____________________SHADER________________________

shader:
	glslc shaders/diffuse.comp -o shaders/diffuse.comp.spv
	glslc shaders/display.frag -o shaders/display.frag.spv
	glslc shaders/fullscreen.vert -o shaders/fullscreen.vert.spv
	

-include $(DEP)
