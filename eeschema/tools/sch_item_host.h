// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SCH_ITEM_HOST_H
#define SCH_ITEM_HOST_H

#ifdef __EMSCRIPTEN__

#include <memory>
#include <string>
#include <emscripten.h>
#include <json_common.h>
#include <kiid.h>
#include <wx/string.h>
#include <sch_item.h>
#include <sch_label.h>
#include <sch_line.h>
#include <sch_screen.h>

class SCH_EDIT_FRAME;
class SCH_SHEET;
class SCH_SHEET_PATH;

/**
 * The record kind the WebAssembly editor's host names a schematic item by (engine run 15,
 * the island's ev.schItem): "label" (all four label kinds), "wire" (a wire or a bus),
 * "line" (a graphic line), "busEntry", "junction", "text", "textbox", "shape", "ruleArea",
 * "image", "sheet", "sheetPin" or "group". Null for every other item: a symbol (run 11's
 * own ask), a table or table cell (the prompt asks own those), a field, a pin, a no connect,
 * a marker.
 */
inline const char* CcSchItemKind( const EDA_ITEM* aItem )
{
    if( !aItem )
        return nullptr;

    switch( aItem->Type() )
    {
    case SCH_LABEL_T:
    case SCH_GLOBAL_LABEL_T:
    case SCH_HIER_LABEL_T:
    case SCH_DIRECTIVE_LABEL_T: return "label";
    case SCH_LINE_T:
        return static_cast<const SCH_LINE*>( aItem )->IsGraphicLine() ? "line" : "wire";
    case SCH_BUS_WIRE_ENTRY_T:
    case SCH_BUS_BUS_ENTRY_T:   return "busEntry";
    case SCH_JUNCTION_T:        return "junction";
    case SCH_TEXT_T:            return "text";
    case SCH_TEXTBOX_T:         return "textbox";
    case SCH_SHAPE_T:           return "shape";
    case SCH_RULE_AREA_T:       return "ruleArea";
    case SCH_BITMAP_T:          return "image";
    case SCH_SHEET_T:           return "sheet";
    case SCH_SHEET_PIN_T:       return "sheetPin";
    case SCH_GROUP_T:           return "group";
    default:                    return nullptr;
    }
}

/**
 * Asks the WebAssembly editor's host whether it edits this schematic item in its own window
 * (engine run 15, cap schItem): window.kicadHostSchItem.onRequest(json) with
 * {kind, uuid, sheetPath, isNew}, kind one of CcSchItemKind's names, sheetPath the sheet
 * path the item is on (SCH_SHEET_PATH::PathAsString), isNew true for an item a placement tool
 * is making. A truthy answer takes the request and KiCad's dialog does not open; no listener,
 * a falsy answer or a throwing listener leaves it to KiCad's dialog, as the symbol ask does.
 */
inline bool CcAskHostSchItem( const char* aKind, const KIID& aUuid, const wxString& aSheetPath,
                              bool aIsNew )
{
    if( !aKind )
        return false;

    nlohmann::json req;
    req["kind"] = aKind;
    req["uuid"] = aUuid.AsString().ToStdString( wxConvUTF8 );
    req["sheetPath"] = aSheetPath.ToStdString( wxConvUTF8 );
    req["isNew"] = aIsNew;

    const std::string text = req.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        if( !window.kicadHostSchItem || typeof window.kicadHostSchItem.onRequest !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostSchItem.onRequest( UTF8ToString( $0 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc schitem] onRequest listener threw', e ); return 0; }
    }, text.c_str() );

    return taken != 0;
}

/**
 * The text a new label of aType takes when the host holds its dialog: the text of the last
 * label of that type on aScreen, incremented as KiCad's repeat increments it
 * (SCH_LABEL_BASE::IncrementLabel), else "Label".
 */
inline wxString CcProposedLabelText( SCH_SCREEN* aScreen, KICAD_T aType )
{
    SCH_LABEL_BASE* last = nullptr;

    if( aScreen )
    {
        for( SCH_ITEM* item : aScreen->Items().OfType( aType ) )
            last = static_cast<SCH_LABEL_BASE*>( item );
    }

    if( last && !last->GetText().IsEmpty() )
    {
        std::unique_ptr<SCH_LABEL_BASE> probe(
                static_cast<SCH_LABEL_BASE*>( last->Clone() ) );

        if( probe && probe->IncrementLabel( 1 ) )
            return probe->GetText();
    }

    return wxT( "Label" );
}

/** The two answers of KiCad's sheet file questions, chosen by the page before it sends. */
enum class CC_SHEET_FILE_MODE
{
    USE_EXISTING,   ///< link the sheet to the file the name names (KiCad's "yes" to linking)
    RENAME          ///< save this sheet's content under the new name (KiCad's other answer)
};

/**
 * SCH_EDIT_FRAME::ChangeSheetFile with no message box (sheet.cpp, K-SHEETFILE): each of KiCad's
 * questions is answered by aMode, and an error is a word in aError in place of a dialog
 * ("normalize", "case_clash", "in_use", "exists", "copy", "save", "load", "recursion").
 * aCurrentSheet is the sheet path aSheet sits on. The sheet's own file name field is left to
 * the caller, as DIALOG_SHEET_PROPERTIES sets it after ChangeSheetFile.
 */
bool ccChangeSheetFile( SCH_EDIT_FRAME* aFrame, SCH_SHEET* aSheet, SCH_SHEET_PATH& aCurrentSheet,
                        const wxString& aNewFilename, CC_SHEET_FILE_MODE aMode,
                        bool* aClearAnnotationNewItems, bool* aIsUndoable,
                        const wxString* aSourceSheetFilename, wxString* aError );

/**
 * The first free file name for a new sheet: aBase (a name with its extension, relative to the
 * schematic's folder) when no sheet of the hierarchy uses it and no such file exists, else
 * aBase with _2, _3 ... before the extension.
 */
wxString ccNextSheetFileName( SCH_EDIT_FRAME* aFrame, const wxString& aBase );

#endif // __EMSCRIPTEN__

#endif // SCH_ITEM_HOST_H
