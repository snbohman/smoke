#_____________________SHELL_________________________

SHELL := cmd.exe
.SHELLFLAGS := /C

#_____________________VARIABLES____________________

CXX := clang++

EXE_PATH := bin
EXE_NAME := $(EXE_PATH)/main.exe

SRC_DIR := src
INC_DIR := include
OBJ_PATH := build

CPP_VERSION := 20

MOSS_PATH := ../moss/core/include
MOSS_EXT_PATH := ../moss/extensions/include
MOSS_EXT_EXT_PATH := ../moss/extensions/external
MOSS_EXT_LIB_PATH := ../moss/extensions/bin/debug

SRC := $(wildcard $(SRC_DIR)/*.cpp)
OBJ := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_PATH)/%.o,$(SRC))
DEP := $(OBJ:.o=.d)

CXXFLAGS := -Wall -g \
	-I$(INC_DIR) \
	-I$(MOSS_PATH) \
	-I$(MOSS_EXT_PATH) \
	-I$(MOSS_EXT_EXT_PATH) \
	-std=c++$(CPP_VERSION) \
	-I$(VULKAN_SDK)/include \
	-I$(GLFW_ROOT)/include \
	-Wno-reorder-ctor \
	-MMD -MP

LDFLAGS := -L$(MOSS_EXT_LIB_PATH) -L$(VULKAN_SDK)/Lib -L$(GLFW_ROOT)/lib-vc2022
LDLIBS := -lvulkan-1 -lmossExtensions -lglfw3 -lgdi32 -luser32 -lshell32

#_____________________SHADER VARIABLES______________

SHADER_SRC_DIR := shaders
SHADER_BUILD_DIR := $(OBJ_PATH)/shader

# Recursively discover .vert, .frag, and .comp files.
SHADERS := $(shell powershell -NoProfile -Command "Get-ChildItem '$(SHADER_SRC_DIR)' -Recurse -File | Where-Object { $$_.Extension -eq '.vert' -or $$_.Extension -eq '.frag' -or $$_.Extension -eq '.comp' } | ForEach-Object { $$_.FullName.Substring((Get-Location).Path.Length + 1).Replace('\','/') }")

SPV_FILES := $(patsubst $(SHADER_SRC_DIR)/%,$(SHADER_BUILD_DIR)/%.spv,$(SHADERS))

#_____________________BUILD_________________________

.PHONY: all compile link run debug clean shader

all: shader $(EXE_NAME) run

compile: $(OBJ)

link: $(EXE_NAME)

$(OBJ_PATH)/%.o: $(SRC_DIR)/%.cpp
	@if not exist "$(OBJ_PATH)" mkdir "$(OBJ_PATH)"
	$(CXX) $(CXXFLAGS) -c "$<" -o "$@"

$(EXE_NAME): $(OBJ)
	@if not exist "$(EXE_PATH)" mkdir "$(EXE_PATH)"
	$(CXX) $(OBJ) $(LDFLAGS) $(LDLIBS) -o "$@"

#_____________________DEBUG_________________________

debug: $(EXE_NAME)
	"$(EXE_NAME)"

#_____________________RUN___________________________

run: $(EXE_NAME)
	"$(EXE_NAME)"

#_____________________CLEAN_________________________

clean:
	@if exist "$(OBJ_PATH)" rmdir /s /q "$(OBJ_PATH)"
	@if exist "$(EXE_NAME)" del /q "$(EXE_NAME)"

#_____________________SHADER________________________

shader: $(SPV_FILES)

$(SHADER_BUILD_DIR)/%.spv: $(SHADER_SRC_DIR)/%
	@if not exist "$(subst /,\,$(dir $@))" mkdir "$(subst /,\,$(dir $@))"
	glslc "$<" -o "$@"

#_____________________DEPENDENCIES__________________

-include $(DEP)
