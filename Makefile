# Macro to use on any target where we don't normally want asserts
ASSERTOFF = -D NDEBUG

# Make _ASSERT=true will nullify our ASSERTOFF flag, thus allowing them
ifdef _ASSERT
ASSERTOFF =
endif

# This turns asserts off for make (plugin), not for test or perf
FLAGS += $(ASSERTOFF)

# FLAGS will be passed to both the C and C++ compiler
CFLAGS += -O3 -std=c99 -Isrc
CXXFLAGS += -O3 -Isrc

# Careful about linking to libraries, since you can't assume much about the user's environment and library search path.
# Static libraries are fine.
LDFLAGS +=

# Add .cpp and .c files to the build
SOURCES = $(wildcard src/*.cpp src/*.c src/*/*.cpp src/*/*.c src/*/*/*.cpp src/*/*/*/c)

# Must include the VCV plugin Makefile framework
RACK_DIR ?= ../..


# Convenience target for including files in the distributable release
#.PHONY: dist
#dist: all

DISTRIBUTABLES += $(wildcard LICENSE* *.pdf README*) res
include $(RACK_DIR)/plugin.mk
