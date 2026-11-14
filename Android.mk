LOCAL_PATH := $(call my-dir)

# mbedTLS 3.6 (Apache-2.0): TLS for private MQTT brokers (broker.txt).
# Static + hidden symbols so it can never clash with another mod's TLS library.
include $(CLEAR_VARS)
LOCAL_MODULE := mp_mbedtls
MBEDTLS_SRC := $(wildcard $(LOCAL_PATH)/third_party/mbedtls/library/*.c)
LOCAL_SRC_FILES := $(MBEDTLS_SRC:$(LOCAL_PATH)/%=%)
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third_party/mbedtls/include
LOCAL_CFLAGS := -O2 -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-everything
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp
endif
include $(BUILD_STATIC_LIBRARY)

# 3.11: Opus 1.6.1 (BSD) for voice chat, floating point build, hidden symbols
include $(CLEAR_VARS)
LOCAL_MODULE := mp_opus
include $(LOCAL_PATH)/third_party/opus/celt_sources.mk
include $(LOCAL_PATH)/third_party/opus/silk_sources.mk
include $(LOCAL_PATH)/third_party/opus/opus_sources.mk
LOCAL_SRC_FILES := $(addprefix third_party/opus/,$(CELT_SOURCES) $(SILK_SOURCES) $(SILK_SOURCES_FLOAT) $(OPUS_SOURCES) $(OPUS_SOURCES_FLOAT))
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third_party/opus/include $(LOCAL_PATH)/third_party/opus/celt $(LOCAL_PATH)/third_party/opus/silk $(LOCAL_PATH)/third_party/opus/silk/float
LOCAL_CFLAGS := -O2 -DOPUS_BUILD -DPACKAGE_VERSION=\"1.6.1\" -DUSE_ALLOCA -DHAVE_LRINTF -DHAVE_LRINT -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-everything
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp
endif
include $(BUILD_STATIC_LIBRARY)

# 4.11: the Pawn abstract machine (pawn-lang/compiler, zLib) + SQLite 3.50 (public domain) for SA-MP gamemodes
include $(CLEAR_VARS)
LOCAL_MODULE := mp_samp
LOCAL_SRC_FILES := third_party/pawn-amx/amx.c third_party/pawn-amx/amxcore.c third_party/pawn-amx/float.c third_party/pawn-amx/amxstring.c third_party/pawn-amx/amxaux.c third_party/sqlite/sqlite3.c third_party/puff/puff.c
LOCAL_C_INCLUDES := $(LOCAL_PATH)/third_party/pawn-amx $(LOCAL_PATH)/third_party/sqlite
LOCAL_CFLAGS := -O2 -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-everything -DHAVE_STDINT_H -DAMX_ANSIONLY -DAMX_NODYNALOAD -DAMX_DONT_RELOCATE -DFLOATPOINT -Dstricmp=strcasecmp -DNDEBUG -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_THREADSAFE=1
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp
endif
include $(BUILD_STATIC_LIBRARY)

# 4.11: UnRAR (Alexander Roshal, see third_party/unrar/license.txt) - imports .rar SA-MP servers
include $(CLEAR_VARS)
LOCAL_MODULE := mp_unrar
LOCAL_CPP_EXTENSION := .cpp
LOCAL_SRC_FILES := $(addprefix third_party/unrar/,rar.cpp strlist.cpp strfn.cpp pathfn.cpp smallfn.cpp global.cpp file.cpp filefn.cpp filcreat.cpp archive.cpp arcread.cpp unicode.cpp system.cpp crypt.cpp crc.cpp rawread.cpp encname.cpp resource.cpp match.cpp timefn.cpp rdwrfn.cpp consio.cpp options.cpp errhnd.cpp rarvm.cpp secpassword.cpp rijndael.cpp getbits.cpp sha1.cpp sha256.cpp blake2s.cpp hash.cpp extinfo.cpp extract.cpp volume.cpp list.cpp find.cpp unpack.cpp headers.cpp threadpool.cpp rs16.cpp cmddata.cpp ui.cpp largepage.cpp filestr.cpp scantree.cpp dll.cpp qopen.cpp)
LOCAL_CPPFLAGS := -O2 -std=c++11 -fvisibility=hidden -ffunction-sections -fdata-sections -Wno-everything -DRARDLL -D_FILE_OFFSET_BITS=64 -D_LARGEFILE_SOURCE
LOCAL_CPP_FEATURES := exceptions
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_CFLAGS += -mfloat-abi=softfp
endif
include $(BUILD_STATIC_LIBRARY)

include $(CLEAR_VARS)
LOCAL_CPP_EXTENSION := .cpp
ifeq ($(TARGET_ARCH_ABI), armeabi-v7a)
    LOCAL_MODULE := MP32
    LOCAL_CFLAGS += -mfloat-abi=softfp
else
    LOCAL_MODULE := MP64
endif

LOCAL_SRC_FILES := \
    src/main.cpp src/mp.cpp \
    src/game/scripting.cpp src/game/scriptguard.cpp src/game/cleoblock.cpp src/game/game.cpp src/game/netstate.cpp src/game/npcgame.cpp src/game/game219.cpp src/game/game301.cpp src/game/game304.cpp src/game/game358.cpp src/game/skins.cpp src/game/fileguard.cpp src/game/world.cpp src/game/modscan.cpp src/game/dzmodels.cpp src/game/boneattach.cpp src/game/screenguard.cpp src/game/gamemenu.cpp src/game/lgball.cpp src/game/hudlayout.cpp \
    src/net/client.cpp src/net/server.cpp src/net/serverlist.cpp src/net/lan.cpp src/net/online.cpp src/net/authcrypto.cpp src/net/samp_import.cpp src/net/authclient.cpp src/net/social.cpp \
    src/ui/hud.cpp src/ui/nametext.cpp src/ui/floatmenu.cpp src/ui/vkeyboard.cpp src/ui/gifs.cpp src/ui/clipboard.cpp src/ui/filepick.cpp src/ui/emoji.cpp src/ui/i18n.cpp src/ui/neonpaint.cpp \
    src/audio/sound.cpp src/audio/voice.cpp src/progress/progress.cpp src/mp_monet.cpp src/embed/monetloader_blob.S \
    include/mod/logger.cpp include/mod/config.cpp \
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_renderware.cpp imgui/RW/RenderWare.cpp
LOCAL_STATIC_LIBRARIES := mp_mbedtls mp_opus mp_samp mp_unrar
# 4.13.32: MonetLoader for MP built in (src/embed/monetloader_blob.S)
LOCAL_ASFLAGS += '-DML_BLOB="$(abspath $(LOCAL_PATH)/monetloader/$(TARGET_ARCH_ABI)/libmonetloader.so)"'

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include $(LOCAL_PATH)/imgui $(LOCAL_PATH)/src $(LOCAL_PATH)/third_party/mbedtls/include $(LOCAL_PATH)/third_party/opus/include
LOCAL_CFLAGS   += -O2 -DNDEBUG -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -DHAVE_STDINT_H -DAMX_ANSIONLY -DAMX_NODYNALOAD -DAMX_DONT_RELOCATE -DFLOATPOINT -Wno-everything -Werror=non-pod-varargs -ffunction-sections -fdata-sections
LOCAL_CPPFLAGS += -std=c++17 -fexceptions -frtti
LOCAL_LDLIBS   += -llog -lOpenSLES -landroid -lz
LOCAL_LDFLAGS  += -Wl,--gc-sections -s
include $(BUILD_SHARED_LIBRARY)
