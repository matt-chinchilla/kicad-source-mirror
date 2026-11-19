// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef DS_EDITOR_HOST_H
#define DS_EDITOR_HOST_H

#ifdef __EMSCRIPTEN__

#include <cstdint>
#include <string>
#include <vector>

#include <emscripten.h>
#include <json_common.h>
#include <kiid.h>
#include <wx/string.h>

/**
 * The picture the WebAssembly editor's host armed for the drawing sheet editor's next Place
 * Bitmaps press (engine run 15, K-DS1, the export kicadDsEditArmImage): the bytes of a PNG and
 * its file name. PL_EDITOR_FRAME::AddDrawingSheetItem takes it (and disarms it) in place of
 * KiCad's "Choose Image" file dialog.
 */
struct CC_DS_ARMED_IMAGE
{
    bool                 armed = false;
    std::vector<uint8_t> png;
    wxString             name;
};

/** The one armed picture slot of the image (an inline function's static: one per image). */
inline CC_DS_ARMED_IMAGE& CcArmedImage()
{
    static CC_DS_ARMED_IMAGE s_image;
    return s_image;
}

class DS_DATA_MODEL;

/**
 * K-DS2 (common/drawing_sheet/ds_data_model_io.cpp): writes aModel to aOutputString as
 * DS_DATA_MODEL::SaveInString does, with no (uuid ...) token, so KiCad 10.0's own parser (and
 * kicad-cli 10.0) reads the text. Used by kicadDsEditTake alone.
 */
void CcDsSaveInStringForStockKicad( DS_DATA_MODEL* aModel, wxString* aOutputString );

/**
 * Asks the WebAssembly editor's host for what the drawing sheet editor would ask of a KiCad
 * window (engine run 15, the island's ev.dsedit): window.kicadHostDsEdit.onRequest(json) with
 * {ask, uuid, isNew}, where ask is "image" (a picture to place, asked when nothing is armed)
 * and uuid the item's ("" for none). Answers whether a listener took the request; no listener,
 * a falsy answer or a throwing listener answers false. The caller opens no window either way.
 */
inline bool CcAskHostDsEdit( const char* aAsk, const KIID* aUuid, bool aIsNew )
{
    nlohmann::json req;
    req["ask"] = aAsk;
    req["uuid"] = aUuid ? aUuid->AsString().ToStdString( wxConvUTF8 ) : std::string();
    req["isNew"] = aIsNew;

    const std::string text = req.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        if( !window.kicadHostDsEdit || typeof window.kicadHostDsEdit.onRequest !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostDsEdit.onRequest( UTF8ToString( $0 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc dsedit] onRequest listener threw', e ); return 0; }
    }, text.c_str() );

    return taken != 0;
}

#endif // __EMSCRIPTEN__

#endif // DS_EDITOR_HOST_H
