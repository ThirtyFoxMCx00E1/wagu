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
    src/game/scripting.cpp src/game/scriptguard.cpp src/game/cleoblock.cpp src/game/game.cpp src/game/netstate.cpp src/game/npcgame.cpp src/game/game219.cpp src/game/game301.cpp src/game/game304.cpp src/game/skins.cpp src/game/world.cpp src/game/modscan.cpp \
    src/net/client.cpp src/net/server.cpp src/net/serverlist.cpp src/net/lan.cpp src/net/online.cpp \
    src/ui/hud.cpp src/ui/nametext.cpp src/ui/floatmenu.cpp src/ui/vkeyboard.cpp src/ui/emoji.cpp \
    src/audio/sound.cpp src/audio/voice.cpp src/progress/progress.cpp \
    include/mod/logger.cpp include/mod/config.cpp \
    imgui/imgui.cpp imgui/imgui_draw.cpp imgui/imgui_tables.cpp imgui/imgui_widgets.cpp \
    imgui/backends/imgui_impl_renderware.cpp imgui/RW/RenderWare.cpp
LOCAL_STATIC_LIBRARIES := mp_mbedtls mp_opus

LOCAL_C_INCLUDES += $(LOCAL_PATH)/include $(LOCAL_PATH)/imgui $(LOCAL_PATH)/src $(LOCAL_PATH)/third_party/mbedtls/include $(LOCAL_PATH)/third_party/opus/include
LOCAL_CFLAGS   += -O2 -DNDEBUG -DIMGUI_DISABLE_OBSOLETE_FUNCTIONS -Wno-everything -ffunction-sections -fdata-sections
LOCAL_CPPFLAGS += -std=c++17 -fexceptions -frtti
LOCAL_LDLIBS   += -llog -lOpenSLES -landroid -lz
LOCAL_LDFLAGS  += -Wl,--gc-sections -s
include $(BUILD_SHARED_LIBRARY)
