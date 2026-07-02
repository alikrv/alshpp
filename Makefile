CC := cc
CFLAGS := -Wall -Wextra -O2 -Iinclude -Icommon_utils
AR := ar
ARFLAGS := rcs
BUILD_DIR := build

COMMON_UTIL_SRCS := common_utils/src/strings.c common_utils/src/file_utils.c common_utils/src/path_utils.c
COMMON_UTIL_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(COMMON_UTIL_SRCS))

ALSHPP_SRCS := src/alshpp.c
ALSHPP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(ALSHPP_SRCS))

APP_SRCS := src/main.c
APP_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(APP_SRCS))

LIB_NAME := libalshpp.a
EXEC_NAME := alshpp

.PHONY: all cli lib clean

all: cli lib

cli: $(EXEC_NAME)

lib: $(LIB_NAME)

$(EXEC_NAME): $(APP_OBJS) $(LIB_NAME)
	$(CC) $(CFLAGS) -o $@ $(APP_OBJS) -L. -lalshpp

$(LIB_NAME): $(ALSHPP_OBJS) $(COMMON_UTIL_OBJS)
	$(AR) $(ARFLAGS) $@ $^

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(EXEC_NAME) $(LIB_NAME)
