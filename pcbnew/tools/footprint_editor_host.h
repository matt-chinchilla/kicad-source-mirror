// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FOOTPRINT_EDITOR_HOST_H
#define FOOTPRINT_EDITOR_HOST_H

#ifdef __EMSCRIPTEN__

#include <string>
#include <emscripten.h>
#include <json_common.h>
#include <kiid.h>

/**
 * Asks the WebAssembly editor's host whether it takes this request of the footprint editor in
 * its own window (engine run 15, the island's ev.fpedit): window.kicadHostFpEdit.onRequest(json)
 * with {ask, uuid, isNew}. ask is an item kind ("pad", "field", "text", "textbox", "shape",
 * "dimension", "zone", "table", "barcode", "image", "point") or a frame request ("footprint",
 * "padDefaults", "wizard", "newFootprint", "rename", "cleanup", "pushPad", "enumerate",
 * "padTable", "graphics"); uuid is the item's ("" for a frame request and a new picture).
 * A truthy answer takes the request and KiCad's dialog does not open; no listener, a falsy
 * answer or a throwing listener leaves it to KiCad's dialog.
 */
inline bool CcAskHostFpEdit( const char* aAsk, const KIID* aUuid, bool aIsNew )
{
    nlohmann::json req;
    req["ask"] = aAsk;
    req["uuid"] = aUuid ? aUuid->AsString().ToStdString( wxConvUTF8 ) : std::string();
    req["isNew"] = aIsNew;

    const std::string text = req.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        if( !window.kicadHostFpEdit || typeof window.kicadHostFpEdit.onRequest !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostFpEdit.onRequest( UTF8ToString( $0 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc fpedit] onRequest listener threw', e ); return 0; }
    }, text.c_str() );

    return taken != 0;
}

#endif // __EMSCRIPTEN__

#endif // FOOTPRINT_EDITOR_HOST_H
