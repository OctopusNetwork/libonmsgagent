rootdir := $(shell pwd)/../../..
project := ubuntu-base

include $(rootdir)/build/common/inc.mk
ifeq ($(hostmaketype), windows)
rootdir := ../../..
endif

TARGETSPEC = libmsgagent
TARGET_LIB = libmsgagent.$(staticlibsuffix)
MODULE = kktmsgagent
MODULEVERSION = 0.0.1

include ../source.mk
include $(rootdir)/build/utils/archive.$(hostmaketype).mk
