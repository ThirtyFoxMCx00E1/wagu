// 2.35: blocks the CLEO Android menu + CLEO touch / swipe scripts while the host disables cheats.
#pragma once
namespace CleoBlock
{
    void Process(bool block);   // every frame (game thread): finds + hooks libcleo once, applies `block`
    bool Available();           // the known CLEO build was found and hooked
}
