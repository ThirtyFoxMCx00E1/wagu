all: mp_server
CXX ?= g++
CC  ?= gcc
MBED = ../third_party/mbedtls
MBED_SRC = $(wildcard $(MBED)/library/*.c)
MBED_OBJ = $(patsubst $(MBED)/library/%.c,obj/mbedtls/%.o,$(MBED_SRC))
AR  ?= ar
# Termux (Android): log to the terminal instead of logcat, link liblog
ifeq ($(shell uname -o 2>/dev/null),Android)
  EXTRA += -DMP_LOG_STDOUT -llog
endif
AMX = ../third_party/pawn-amx
AMX_SRC = $(AMX)/amx.c $(AMX)/amxcore.c $(AMX)/float.c $(AMX)/amxstring.c $(AMX)/amxaux.c
AMX_FLAGS = -DHAVE_STDINT_H -DAMX_ANSIONLY -DAMX_NODYNALOAD -DAMX_DONT_RELOCATE -DFLOATPOINT -Dstricmp=strcasecmp -DNDEBUG
UNRAR = ../third_party/unrar
UNRAR_SRC = $(addprefix $(UNRAR)/,rar.cpp strlist.cpp strfn.cpp pathfn.cpp smallfn.cpp global.cpp file.cpp filefn.cpp filcreat.cpp archive.cpp arcread.cpp unicode.cpp system.cpp crypt.cpp crc.cpp rawread.cpp encname.cpp resource.cpp match.cpp timefn.cpp rdwrfn.cpp consio.cpp options.cpp errhnd.cpp rarvm.cpp secpassword.cpp rijndael.cpp getbits.cpp sha1.cpp sha256.cpp blake2s.cpp hash.cpp extinfo.cpp extract.cpp volume.cpp list.cpp find.cpp unpack.cpp headers.cpp threadpool.cpp rs16.cpp cmddata.cpp ui.cpp largepage.cpp filestr.cpp scantree.cpp dll.cpp qopen.cpp)
obj/libunrar.a: $(UNRAR_SRC)
	@mkdir -p obj/unrar
	for f in $(UNRAR_SRC); do $(CXX) -O2 -std=c++11 -w -DRARDLL -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE -c $$f -o obj/unrar/$$(basename $$f .cpp).o || exit 1; done
	$(AR) rcs $@ obj/unrar/*.o
obj/libsamp.a: $(AMX_SRC) ../third_party/sqlite/sqlite3.c ../third_party/puff/puff.c
	@mkdir -p obj/samp
	for f in $(AMX_SRC); do $(CC) -O2 $(AMX_FLAGS) -c $$f -o obj/samp/$$(basename $$f .c).o || exit 1; done
	$(CC) -O2 -DSQLITE_THREADSAFE=1 -DSQLITE_OMIT_LOAD_EXTENSION -c ../third_party/sqlite/sqlite3.c -o obj/samp/sqlite3.o
	$(CC) -O2 -c ../third_party/puff/puff.c -o obj/samp/puff.o
	$(AR) rcs $@ obj/samp/*.o
SRC = mp_server.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp ../src/net/authcrypto.cpp ../src/net/samp_import.cpp

mp_server: $(SRC) ../src/net/server.h ../src/net/protocol.h ../src/net/lan.h ../src/net/online.h ../src/net/authcrypto.h obj/libmbedtls.a obj/libsamp.a obj/libunrar.a ../src/net/server_samp.inc ../src/net/server_bridge.inc ../src/net/server_samp_sql.inc ../src/net/server_samp_sscanf.inc ../src/net/samp.h
	$(CXX) -O2 -std=c++17 -pthread -I../src -I$(MBED)/include $(AMX_FLAGS) $(SRC) obj/libmbedtls.a obj/libsamp.a obj/libunrar.a $(EXTRA) -ldl -lm -o mp_server

obj/mbedtls/%.o: $(MBED)/library/%.c
	@mkdir -p obj/mbedtls
	$(CC) -O2 -I$(MBED)/include -I$(MBED)/library -c $< -o $@

obj/libmbedtls.a: $(MBED_OBJ)
	$(AR) rcs $@ $^

clean:
	rm -rf mp_server obj
