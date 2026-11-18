// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef BOARD_ITEM_HOST_H
#define BOARD_ITEM_HOST_H

#ifdef __EMSCRIPTEN__

#include <functional>
#include <string>
#include <emscripten.h>
#include <json_common.h>
#include <kiid.h>

class BOARD_COMMIT;
class BOARD_ITEM;
class PCB_BASE_EDIT_FRAME;

/**
 * Asks the WebAssembly editor's host whether it edits this board item in its own window
 * (engine run 15, the island's ev.boardItem): window.kicadHostBoardItem.onRequest(json) with
 * {kind, uuid, isNew}. kind is "pad", "shape", "dimension", "textbox", "barcode", "image",
 * "zone", "point", "target", "group", "tuning" or, for a new one, "text"; uuid is the item's
 * (the first item's for a selection of several of one kind; "" for a new picture and for the
 * tuning tool's live settings). A truthy answer takes the
 * request and KiCad's dialog does not open; no listener, a falsy answer or a throwing listener
 * leaves it to KiCad's dialog. Run A's window.kicadProperties ask is unchanged and asked first.
 */
inline bool CcAskHostBoardItem( const char* aKind, const KIID* aUuid, bool aIsNew )
{
    nlohmann::json req;
    req["kind"] = aKind;
    req["uuid"] = aUuid ? aUuid->AsString().ToStdString( wxConvUTF8 ) : std::string();
    req["isNew"] = aIsNew;

    const std::string text = req.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        if( !window.kicadHostBoardItem || typeof window.kicadHostBoardItem.onRequest !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostBoardItem.onRequest( UTF8ToString( $0 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc boarditem] onRequest listener threw', e ); return 0; }
    }, text.c_str() );

    return taken != 0;
}

/*
 * The tuning tool's and a placed tuning pattern's settings as the host's Tuning form reads and
 * sets them (engine run 15, defined in pcbnew/generators/pcb_tuning_pattern.cpp): lengths in mm,
 * delays in ps. A settings JSON is {mode, minAmplitude, maxAmplitude, spacing, cornerStyle,
 * cornerRadiusPercent, singleSided, timeDomain, overrideCustomRules, target {min, opt, max},
 * fromRules}; a set is any of those keys but mode and fromRules, target replaced whole, applied
 * as DIALOG_TUNING_PATTERN_PROPERTIES::TransferDataFromWindow applies its fields.
 */

/** The live tuning tool's settings, or "" when no tuning tool runs. A read. */
std::string CcTuningLiveGet();

/** True when aSet is a set the tuning form may send (read whole; nothing changed). */
bool CcTuningSetValid( const nlohmann::json& aSet );

/**
 * Hands aSet to the live tuning tool, which applies it as its dialog's OK does, on its own
 * coroutine, then calls aDone with the settings it now holds (the CcTuningLiveGet JSON).
 * False (nothing handed over) when no tuning tool runs or aSet is not valid.
 */
bool CcTuningLiveSet( const nlohmann::json& aSet,
                      std::function<void( const nlohmann::json& )> aDone );

/**
 * A placed tuning pattern's record ({kind: "tuning", uuid, layer, ...settings}), the settings
 * as its properties dialog shows them, or a null JSON when aItem is not a tuning pattern.
 */
nlohmann::json CcTuningPatternRecord( BOARD_ITEM* aItem );

/**
 * Applies aSet to a placed tuning pattern inside aCommit as its properties dialog's OK does
 * (the pattern rebuilt by the generator tool). False, nothing staged, for any other item or a
 * set that is not valid.
 */
bool CcTuningPatternApply( PCB_BASE_EDIT_FRAME* aFrame, BOARD_ITEM* aItem,
                           const nlohmann::json& aSet, BOARD_COMMIT& aCommit );

#endif // __EMSCRIPTEN__

#endif // BOARD_ITEM_HOST_H
