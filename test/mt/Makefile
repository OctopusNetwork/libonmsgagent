rootdir := $(shell pwd)/../../..
project := ubuntu-base

include $(rootdir)/build/common/inc.mk
ifeq ($(hostmaketype), windows)
rootdir := ../../..
endif

####
##  Application targets
####
DEP_HEADERS +=  $(incdir)/kktmsgagent

apps = mt_msgagent

mt_msgagent_SRCS = $(rootdir)/kktmsgagent/test/mt/main.c
mt_msgagent_ARCHIVES = msgagent kktplatform kktevent
mt_msgagent_LDLIBS = -pthread
mt_msgagent_DISTSOLIBS =

include $(rootdir)/build/utils/app.mk
