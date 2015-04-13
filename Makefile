
INCLUDE_DIR=dovecot/target/include
SOURCE_DIR=src
TARGET_LIB_SO=dovecot/target/lib/dovecot/lib18_scrambler_plugin.so

C_FILES=$(shell ls $(SOURCE_DIR)/*.c)
H_FILES=$(C_FILES:.c=.h)
O_FILES=$(C_FILES:.c=.o)

CC=gcc
CFLAGS=-std=gnu99 \
	-Wall -W -Wmissing-prototypes -Wmissing-declarations -Wpointer-arith -Wchar-subscripts -Wformat=2 \
	-Wbad-function-cast -fno-builtin-strftime -Wstrict-aliasing=2 -Wl,-z,relro,-z,now \
	-fPIC -fstack-check -ftrapv -DPIC -D_FORTIFY_SOURCE=2 -DHAVE_CONFIG_H \
	-I$(INCLUDE_DIR)
LDFLAGS=-gs -shared -lxcrypt -lcrypto -rdynamic -Wl,-soname,lib18_scrambler_plugin.so.1

ifeq ($(DEBUG), 1)
	CFLAGS+=-DDEBUG_STREAMS -g
endif

all: $(TARGET_LIB_SO)

$(SOURCE_DIR)/%.o: %.c $(H_FILES)
	$(CC) -c -o $@ $< $(CFLAGS)

$(TARGET_LIB_SO): $(O_FILES)
	mkdir -p $(shell dirname $(TARGET_LIB_SO))
	$(CC) -o $@ $^ $(LDFLAGS)

.PHONY: clean

clean:
	rm -f $(O_FILES) $(TARGET_LIB_SO)

spec-all: $(TARGET_LIB_SO)
	bash --login -c 'rake spec:integration'

spec-focus: $(TARGET_LIB_SO)
	bash --login -c 'rake spec:integration:focus'

log:
	tail -f dovecot/log/dovecot.log

-include Makefile.deploy
