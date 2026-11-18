// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_HOST_DESIGN_BLOCK_H
#define CC_HOST_DESIGN_BLOCK_H

#ifdef __EMSCRIPTEN__

#include <memory>
#include <design_block.h>

/**
 * The place options the host gives with a design block (the WebAssembly editor's block.place):
 * KiCad's Design Blocks pane keeps them in its chooser settings (place_as_sheet,
 * keep_annotations, place_as_group); a host block carries its own, and the chooser's settings
 * are never read for it.
 */
struct CC_HOST_DESIGN_BLOCK_OPTIONS
{
    bool asSheet = true;
    bool keepAnnotations = false;
    bool asGroup = false;
};

/**
 * The slot that holds the host's block between kicadDesignBlockPlace and the placement tool
 * (K-B1). One block at a time: a new one replaces the one held.
 */
struct CC_HOST_DESIGN_BLOCK_SLOT
{
    std::unique_ptr<DESIGN_BLOCK> block;
    CC_HOST_DESIGN_BLOCK_OPTIONS  options;
};

inline CC_HOST_DESIGN_BLOCK_SLOT& ccHostDesignBlockSlot()
{
    static CC_HOST_DESIGN_BLOCK_SLOT s_slot;
    return s_slot;
}

/**
 * The block the host handed in, or nullptr when none is held (the placement tools then read
 * the Design Blocks pane as KiCad does).
 */
inline DESIGN_BLOCK* ccHostDesignBlock()
{
    return ccHostDesignBlockSlot().block.get();
}

/**
 * The options given with the held block (the defaults when none is held).
 */
inline const CC_HOST_DESIGN_BLOCK_OPTIONS& ccHostDesignBlockOptions()
{
    return ccHostDesignBlockSlot().options;
}

/**
 * The setter: the slot takes ownership of aBlock (nullptr clears the slot) with its options.
 */
inline void ccSetHostDesignBlock( std::unique_ptr<DESIGN_BLOCK> aBlock,
                                  const CC_HOST_DESIGN_BLOCK_OPTIONS& aOptions = {} )
{
    CC_HOST_DESIGN_BLOCK_SLOT& slot = ccHostDesignBlockSlot();
    slot.block = std::move( aBlock );
    slot.options = slot.block ? aOptions : CC_HOST_DESIGN_BLOCK_OPTIONS();
}

/**
 * The placement tool takes the held block: ownership passes to the caller, and the slot is
 * cleared (its options are copied into aOptions first when given).
 */
inline std::unique_ptr<DESIGN_BLOCK> ccTakeHostDesignBlock( CC_HOST_DESIGN_BLOCK_OPTIONS* aOptions = nullptr )
{
    CC_HOST_DESIGN_BLOCK_SLOT& slot = ccHostDesignBlockSlot();

    if( aOptions )
        *aOptions = slot.options;

    std::unique_ptr<DESIGN_BLOCK> block = std::move( slot.block );
    slot.options = CC_HOST_DESIGN_BLOCK_OPTIONS();
    return block;
}

#endif // __EMSCRIPTEN__

#endif // CC_HOST_DESIGN_BLOCK_H
