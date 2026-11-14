// MP 4.13.32: MonetLoader for MP (MonetLoaderOSS fork, libmonetloader.so) is built into libMP32.so / libMP64.so.
// src/mp_monet.cpp writes it to the game's private folder and starts it - no separate .so in the mods folder.
// ML_BLOB = src/embed/monetloader/<abi>/libmonetloader.so (Android.mk; 4.13.35: moved from monetloader/)
    .section .rodata.mp_monetloader,"a"
    .balign 16
    .global mp_monetloader_blob
    .hidden mp_monetloader_blob
mp_monetloader_blob:
    .incbin ML_BLOB
    .global mp_monetloader_blob_end
    .hidden mp_monetloader_blob_end
mp_monetloader_blob_end:
    .byte 0
    .section .note.GNU-stack,"",%progbits
