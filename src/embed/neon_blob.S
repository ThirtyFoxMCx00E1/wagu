// MP 4.13.33: the neon title menu files (assets/MP/neon) are built into libMP32.so / libMP64.so.
// src/ui/neon_embed.cpp writes a missing or different file to files/TYFMC/MP/neon/ before the menu loads them, so
// ServerBrowserMode = neon / mp works even when the TYFMC folder of the package was not copied (or is an old one).
// The assembler finds the files through -I assets/MP/neon (Android.mk LOCAL_ASFLAGS).
    .section .rodata.mp_neon,"a"
    .balign 4
    .global mp_neon_0
    .hidden mp_neon_0
mp_neon_0:
    .incbin "art.png"
    .global mp_neon_0_end
    .hidden mp_neon_0_end
mp_neon_0_end:
    .balign 4
    .global mp_neon_1
    .hidden mp_neon_1
mp_neon_1:
    .incbin "logo.png"
    .global mp_neon_1_end
    .hidden mp_neon_1_end
mp_neon_1_end:
    .balign 4
    .global mp_neon_2
    .hidden mp_neon_2
mp_neon_2:
    .incbin "shade_left.png"
    .global mp_neon_2_end
    .hidden mp_neon_2_end
mp_neon_2_end:
    .balign 4
    .global mp_neon_3
    .hidden mp_neon_3
mp_neon_3:
    .incbin "shade_bottom.png"
    .global mp_neon_3_end
    .hidden mp_neon_3_end
mp_neon_3_end:
    .balign 4
    .global mp_neon_4
    .hidden mp_neon_4
mp_neon_4:
    .incbin "shade_top.png"
    .global mp_neon_4_end
    .hidden mp_neon_4_end
mp_neon_4_end:
    .balign 4
    .global mp_neon_5
    .hidden mp_neon_5
mp_neon_5:
    .incbin "card_identity.png"
    .global mp_neon_5_end
    .hidden mp_neon_5_end
mp_neon_5_end:
    .balign 4
    .global mp_neon_6
    .hidden mp_neon_6
mp_neon_6:
    .incbin "card_featured.png"
    .global mp_neon_6_end
    .hidden mp_neon_6_end
mp_neon_6_end:
    .balign 4
    .global mp_neon_7
    .hidden mp_neon_7
mp_neon_7:
    .incbin "dialog.png"
    .global mp_neon_7_end
    .hidden mp_neon_7_end
mp_neon_7_end:
    .balign 4
    .global mp_neon_8
    .hidden mp_neon_8
mp_neon_8:
    .incbin "button.png"
    .global mp_neon_8_end
    .hidden mp_neon_8_end
mp_neon_8_end:
    .balign 4
    .global mp_neon_9
    .hidden mp_neon_9
mp_neon_9:
    .incbin "pill.png"
    .global mp_neon_9_end
    .hidden mp_neon_9_end
mp_neon_9_end:
    .balign 4
    .global mp_neon_10
    .hidden mp_neon_10
mp_neon_10:
    .incbin "discord.png"
    .global mp_neon_10_end
    .hidden mp_neon_10_end
mp_neon_10_end:
    .balign 4
    .global mp_neon_11
    .hidden mp_neon_11
mp_neon_11:
    .incbin "arrow.png"
    .global mp_neon_11_end
    .hidden mp_neon_11_end
mp_neon_11_end:
    .balign 4
    .global mp_neon_12
    .hidden mp_neon_12
mp_neon_12:
    .incbin "arrow_down.png"
    .global mp_neon_12_end
    .hidden mp_neon_12_end
mp_neon_12_end:
    .balign 4
    .global mp_neon_13
    .hidden mp_neon_13
mp_neon_13:
    .incbin "dot.png"
    .global mp_neon_13_end
    .hidden mp_neon_13_end
mp_neon_13_end:
    .balign 4
    .global mp_neon_14
    .hidden mp_neon_14
mp_neon_14:
    .incbin "white.png"
    .global mp_neon_14_end
    .hidden mp_neon_14_end
mp_neon_14_end:
    .balign 4
    .global mp_neon_15
    .hidden mp_neon_15
mp_neon_15:
    .incbin "font_gothic.png"
    .global mp_neon_15_end
    .hidden mp_neon_15_end
mp_neon_15_end:
    .balign 4
    .global mp_neon_16
    .hidden mp_neon_16
mp_neon_16:
    .incbin "font_gothic.txt"
    .global mp_neon_16_end
    .hidden mp_neon_16_end
mp_neon_16_end:
    .balign 4
    .global mp_neon_17
    .hidden mp_neon_17
mp_neon_17:
    .incbin "font_caps.png"
    .global mp_neon_17_end
    .hidden mp_neon_17_end
mp_neon_17_end:
    .balign 4
    .global mp_neon_18
    .hidden mp_neon_18
mp_neon_18:
    .incbin "font_caps.txt"
    .global mp_neon_18_end
    .hidden mp_neon_18_end
mp_neon_18_end:
    .balign 4
    .global mp_neon_19
    .hidden mp_neon_19
mp_neon_19:
    .incbin "font_ui.png"
    .global mp_neon_19_end
    .hidden mp_neon_19_end
mp_neon_19_end:
    .balign 4
    .global mp_neon_20
    .hidden mp_neon_20
mp_neon_20:
    .incbin "font_ui.txt"
    .global mp_neon_20_end
    .hidden mp_neon_20_end
mp_neon_20_end:
    .byte 0
    .section .note.GNU-stack,"",%progbits
