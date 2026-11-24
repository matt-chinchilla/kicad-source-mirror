// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_HOST_TOOL_H
#define CC_HOST_TOOL_H

#ifdef __EMSCRIPTEN__

/*
 * Tells the WebAssembly editor's host which tool KiCad holds: TOOLS_HOLDER::PushTool and
 * PopTool call CcTellHostTool with the top frame's CurrentToolName() once the tool stack has
 * changed. window.kicadHostTool.onChange( text ) with text the JSON {"tool":"<action name>"}
 * (a string, as every host ask since run 12), the selection tool being
 * "common.InteractiveSelection.selectionTool". Sent only when the name differs from the last one
 * sent; no listener or a throwing listener changes nothing in KiCad. The export
 * kicadActiveTool reads the same name at any time.
 */

#include <string>
#include <emscripten.h>
#include <json_common.h>

// The last name told to the host ("" before the first).
inline std::string& CcHostToolLastSent()
{
    static std::string last;
    return last;
}

inline void CcTellHostTool( const std::string& aName )
{
    if( aName == CcHostToolLastSent() )
        return;

    CcHostToolLastSent() = aName;

    nlohmann::json msg;
    msg["tool"] = aName;

    const std::string text = msg.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    EM_ASM( {
        if( !window.kicadHostTool || typeof window.kicadHostTool.onChange !== 'function' )
            return;

        // A throwing listener must never unwind the wasm frame that called it.
        try { window.kicadHostTool.onChange( UTF8ToString( $0 ) ); }
        catch( e ) { console.error( '[cc tool] onChange listener threw', e ); }
    }, text.c_str() );
}

#endif // __EMSCRIPTEN__

#endif // CC_HOST_TOOL_H
