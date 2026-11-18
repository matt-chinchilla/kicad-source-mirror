// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_PASTE_SPECIAL_H
#define CC_PASTE_SPECIAL_H

#ifdef __EMSCRIPTEN__

#include <optional>

/**
 * K-PASTE (engine run 15, kicadPasteSpecial): the choices of KiCad's Paste Special dialog, given
 * by the host. mode is the dialog's PASTE_MODE as a number (0 unique annotations, 1 keep
 * annotations, 2 remove annotations); clearNets is the board's Clear Nets box. Armed by the
 * export for one Paste Special and taken by the paste handler in place of the dialog.
 */
struct CC_PASTE_SPECIAL
{
    int  mode = 1;
    bool clearNets = false;
};

inline std::optional<CC_PASTE_SPECIAL>& ccPasteSpecialSlot()
{
    static std::optional<CC_PASTE_SPECIAL> s_slot;
    return s_slot;
}

/**
 * The armed choices, taken once (the slot is empty afterwards), or nothing when the host armed
 * none (KiCad's dialog then shows as before).
 */
inline std::optional<CC_PASTE_SPECIAL> ccTakePasteSpecial()
{
    std::optional<CC_PASTE_SPECIAL> taken = ccPasteSpecialSlot();
    ccPasteSpecialSlot().reset();
    return taken;
}

#endif // __EMSCRIPTEN__

#endif // CC_PASTE_SPECIAL_H
