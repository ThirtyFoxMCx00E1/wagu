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
SRC = mp_server.cpp ../src/net/server.cpp ../src/net/lan.cpp ../src/net/online.cpp ../src/net/authcrypto.cpp

mp_server: $(SRC) ../src/net/server.h ../src/net/protocol.h ../src/net/lan.h ../src/net/online.h ../src/net/authcrypto.h obj/libmbedtls.a
	$(CXX) -O2 -std=c++17 -pthread -I../src -I$(MBED)/include $(SRC) obj/libmbedtls.a $(EXTRA) -o mp_server

obj/mbedtls/%.o: $(MBED)/library/%.c
	@mkdir -p obj/mbedtls
	$(CC) -O2 -I$(MBED)/include -I$(MBED)/library -c $< -o $@

obj/libmbedtls.a: $(MBED_OBJ)
	$(AR) rcs $@ $^

clean:
	rm -rf mp_server obj
