LIBGO_SUPPORT_SRC_DIR   := $(REP_DIR)/src/lib/libgo_support

SRC_CC := dummy.cc anon_mmap.cc file_operations_anon.cc

INC_DIR += $(call select_from_repositories,src/lib/libc)
INC_DIR += $(call select_from_repositories,src/lib/libc)/spec/x86_64

LIBS   = base posix libc

CC_CXX_WARN_STRICT =

vpath %.cc $(LIBGO_SUPPORT_SRC_DIR)
