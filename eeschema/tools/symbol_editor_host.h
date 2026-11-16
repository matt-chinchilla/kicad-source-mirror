/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation, either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef SYMBOL_EDITOR_HOST_H
#define SYMBOL_EDITOR_HOST_H

#ifdef __EMSCRIPTEN__

#include <string>
#include <emscripten.h>
#include <json_common.h>
#include <kiid.h>

/**
 * Asks the WebAssembly editor's host whether it edits this item of the symbol editor in its
 * own window (engine run 14, the island's ev.symedit): window.kicadHostSymEdit.onRequest(json)
 * with {ask, uuid, isNew}, where ask is "pin", "field", "text", "textbox", "shape" or "symbol"
 * and uuid is the item's ("" for the symbol). A truthy answer takes the request and KiCad's
 * dialog does not open; no listener, a falsy answer or a throwing listener leaves it to KiCad's
 * dialog, as the schematic editor's askHostForProperties does.
 */
inline bool CcAskHostSymEdit( const char* aAsk, const KIID* aUuid, bool aIsNew )
{
    nlohmann::json req;
    req["ask"] = aAsk;
    req["uuid"] = aUuid ? aUuid->AsString().ToStdString( wxConvUTF8 ) : std::string();
    req["isNew"] = aIsNew;

    const std::string text = req.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        if( !window.kicadHostSymEdit || typeof window.kicadHostSymEdit.onRequest !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostSymEdit.onRequest( UTF8ToString( $0 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc symedit] onRequest listener threw', e ); return 0; }
    }, text.c_str() );

    return taken != 0;
}

#endif // __EMSCRIPTEN__

#endif // SYMBOL_EDITOR_HOST_H
