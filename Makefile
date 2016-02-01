
DOVECOT_VERSION=2.2.21
DOVECOT_DIR=$(abspath dovecot)
DOVECOT_KEYRING_FILE=$(DOVECOT_DIR)/dovecot.gpg
DOVECOT_SOURCE_URL=http://dovecot.org/releases/2.2/dovecot-$(DOVECOT_VERSION).tar.gz
DOVECOT_SOURCE_SIG_URL=$(DOVECOT_SOURCE_URL).sig
DOVECOT_SOURCE_FILE=$(DOVECOT_DIR)/dovecot-$(DOVECOT_VERSION).tar.gz
DOVECOT_SOURCE_SIG_FILE=$(DOVECOT_SOURCE_FILE).sig
DOVECOT_SOURCE_DIR=$(DOVECOT_DIR)/source
DOVECOT_TARGET_DIR=$(DOVECOT_DIR)/target
DOVECOT_INCLUDE_DIR=dovecot/target/include

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
	-I$(DOVECOT_INCLUDE_DIR)
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

dovecot-download:
	-test ! -f $(DOVECOT_SOURCE_FILE) && curl -o $(DOVECOT_SOURCE_FILE) $(DOVECOT_SOURCE_URL)

dovecot-download-verify: dovecot-download
	-test ! -f $(DOVECOT_SOURCE_SIG_FILE) && curl -o $(DOVECOT_SOURCE_SIG_FILE) $(DOVECOT_SOURCE_SIG_URL)
	gpg --verify --keyring $(DOVECOT_KEYRING_FILE) $(DOVECOT_SOURCE_SIG_FILE)

dovecot-extract: dovecot-download-verify
	-test ! -d $(DOVECOT_SOURCE_DIR) && \
		(tar xzf $(DOVECOT_SOURCE_FILE) -C $(DOVECOT_DIR) && mv $(DOVECOT_DIR)/dovecot-$(DOVECOT_VERSION) $(DOVECOT_SOURCE_DIR))

dovecot-configure: dovecot-extract
	cd $(DOVECOT_SOURCE_DIR) && ./configure --prefix $(DOVECOT_TARGET_DIR) --with-sqlite

dovecot-compile: dovecot-configure
	cd $(DOVECOT_SOURCE_DIR) && make

dovecot-install: dovecot-compile
	cd $(DOVECOT_SOURCE_DIR) && make install

clean:
	rm -f $(O_FILES) $(TARGET_LIB_SO)

spec-all: $(TARGET_LIB_SO)
	bash --login -c 'rake spec:integration'

spec-focus: $(TARGET_LIB_SO)
	bash --login -c 'rake spec:integration:focus'

log:
	tail -f dovecot/log/dovecot.log

-include Makefile.deploy
