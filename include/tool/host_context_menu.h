// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef HOST_CONTEXT_MENU_H
#define HOST_CONTEXT_MENU_H

#ifdef __EMSCRIPTEN__

/*
 * K-MENU (engine run 15, canvas addendum 3.1): the canvas context menu of every frame, and the
 * clarify-selection list, are reported to the WebAssembly editor's host as a MODEL before KiCad
 * draws its own popup, so the host draws the menu itself.
 *
 * TOOL_MANAGER::DispatchContextMenu calls CcHostTakesMenu in place of frame->PopupMenu. It does
 * what wxWindowBase::PopupMenu and the port's DoPopupMenu do around the drawing (the invoking
 * window, UpdateUI, wxEVT_MENU_OPEN to the menu and its ACTION_MENU submenus), serializes the
 * model and asks window.kicadHostMenu.onMenu( text ), text being the JSON
 * { token, clarify, tool, title, at, kinds, items } (a string, as every host ask since run 12).
 * No listener, a falsy answer or a throwing listener: false, and KiCad's popup runs as before.
 * Truthy: the chain parks on the port's modal wait ("menu") until kicadContextMenuChoose
 * resolves it, then replays the choice as DoPopupMenu does (Toggle a checkable, SendEvent from
 * the containing menu), so a choice reaches ACTION_MENU::OnMenuEvent exactly as from KiCad's
 * popup (K-HOLD included).
 *
 * Every frame (schematic, board, symbol, footprint, drawing sheet) dispatches its canvas menus
 * through TOOL_MANAGER, so this one hook serves them all.
 */

#include <functional>
#include <set>
#include <string>
#include <wx/menu.h>
#include <wx/window.h>
#include <json_common.h>
#include <eda_item.h>
#include <hotkeys_basic.h>
#include <host_prompt.h>
#include <tool/action_manager.h>
#include <tool/action_menu.h>
#include <tool/selection.h>
#include <tool/tool_action.h>
#include <tool/tool_base.h>
#include <tool/tool_manager.h>
#include <tool/tools_holder.h>
#include <view/view.h>

/// Bounds of one model: past them the menu is cut and "cut": true is added.
constexpr int    CC_MENU_MAX_ITEMS = 400;
constexpr int    CC_MENU_MAX_DEPTH = 3;
constexpr size_t CC_MENU_MAX_LABEL = 128;


/** The one menu the host holds (at most one: the engine is parked in it). */
struct CC_PARKED_MENU
{
    int          token = 0;            ///< the model's token, 0 when none is parked
    int          waitToken = 0;        ///< the port's wait
    ACTION_MENU* menu = nullptr;       ///< the cloned menu DispatchContextMenu shows
    wxWindow*    invoker = nullptr;    ///< the frame the menu was invoked from
};


inline CC_PARKED_MENU& CcParkedMenu()
{
    static CC_PARKED_MENU s_parked;
    return s_parked;
}


inline int CcNextMenuToken()
{
    static int s_next = 0;
    return ++s_next;
}


/** True while the host holds a context menu (the engine is parked in it). */
inline bool CcHostMenuParked()
{
    return CcParkedMenu().token != 0;
}


/** Cancel the parked menu, as a press outside KiCad's popup does (nothing chosen). */
inline void CcHostMenuCancelParked()
{
    CC_PARKED_MENU& parked = CcParkedMenu();

    if( parked.token == 0 )
        return;

    const int wait = parked.waitToken;
    parked = CC_PARKED_MENU();
    wxWasmResolveWait( wait, -1 );
}


/** The word for one selected item's kind (canvas addendum 3.4). */
inline const char* CcMenuKindOf( const EDA_ITEM* aItem )
{
    if( !aItem )
        return "other";

    switch( aItem->Type() )
    {
    // Board and footprint frames (wave 1's board.item.get words).
    case PCB_FOOTPRINT_T:       return "footprint";
    case PCB_PAD_T:             return "pad";
    case PCB_TRACE_T:           return "track";
    case PCB_ARC_T:             return "arc";
    case PCB_VIA_T:             return "via";
    case PCB_ZONE_T:
        // ZONE is a board class: its own GetFriendlyName tells a rule area from a zone.
        return aItem->GetFriendlyName() == _( "Rule Area" ) ? "rulearea" : "zone";
    case PCB_TEXT_T:            return "text";
    case PCB_TEXTBOX_T:         return "textbox";
    case PCB_TABLE_T:           return "table";
    case PCB_TABLECELL_T:       return "tablecell";
    case PCB_SHAPE_T:           return "shape";
    case PCB_DIMENSION_T:
    case PCB_DIM_ALIGNED_T:
    case PCB_DIM_LEADER_T:
    case PCB_DIM_CENTER_T:
    case PCB_DIM_RADIAL_T:
    case PCB_DIM_ORTHOGONAL_T:  return "dimension";
    case PCB_REFERENCE_IMAGE_T: return "image";
    case PCB_BARCODE_T:         return "barcode";
    case PCB_TARGET_T:          return "target";
    case PCB_GROUP_T:           return "group";
    case PCB_GENERATOR_T:       return "generator";
    case PCB_MARKER_T:          return "marker";
    case PCB_FIELD_T:           return "field";

    // Schematic and symbol frames.
    case SCH_SYMBOL_T:          return "symbol";
    case SCH_FIELD_T:           return "field";
    case SCH_PIN_T:             return "pin";
    case SCH_LINE_T:
        if( aItem->IsType( { SCH_ITEM_LOCATE_WIRE_T } ) )
            return "wire";

        if( aItem->IsType( { SCH_ITEM_LOCATE_BUS_T } ) )
            return "bus";

        return "shape";         // a graphic line
    case SCH_BUS_WIRE_ENTRY_T:
    case SCH_BUS_BUS_ENTRY_T:   return "busentry";
    case SCH_JUNCTION_T:        return "junction";
    case SCH_NO_CONNECT_T:      return "noconnect";
    case SCH_LABEL_T:           return "label";
    case SCH_GLOBAL_LABEL_T:    return "globallabel";
    case SCH_HIER_LABEL_T:      return "hierlabel";
    case SCH_DIRECTIVE_LABEL_T: return "directivelabel";
    case SCH_SHEET_T:           return "sheet";
    case SCH_SHEET_PIN_T:       return "sheetpin";
    case SCH_TEXT_T:            return "text";
    case SCH_TEXTBOX_T:         return "textbox";
    case SCH_TABLE_T:           return "table";
    case SCH_TABLECELL_T:       return "tablecell";
    case SCH_SHAPE_T:           return "shape";
    case SCH_BITMAP_T:          return "image";
    case SCH_GROUP_T:           return "group";
    case SCH_RULE_AREA_T:       return "rulearea";
    case SCH_MARKER_T:          return "marker";

    // Drawing sheet frame.
    case WSG_LINE_T:            return "line";
    case WSG_RECT_T:            return "rect";
    case WSG_TEXT_T:            return "text";
    case WSG_BITMAP_T:          return "image";

    default:                    return "other";
    }
}


/** The distinct kinds of the frame's current selection, sorted. */
inline nlohmann::json CcMenuKinds( TOOL_MANAGER& aToolMgr )
{
    std::set<std::string> kinds;

    if( TOOLS_HOLDER* holder = aToolMgr.GetToolHolder() )
    {
        for( EDA_ITEM* item : holder->GetCurrentSelection() )
            kinds.insert( CcMenuKindOf( item ) );
    }

    nlohmann::json out = nlohmann::json::array();

    for( const std::string& kind : kinds )
        out.push_back( kind );

    return out;
}


/** Serialize @a aMenu's items as the port's WasmItemsToJson does, plus each item's action. */
inline nlohmann::json CcMenuItems( TOOL_MANAGER& aToolMgr, const wxMenu& aMenu, int aDepth,
                                   int& aCount, bool& aCut )
{
    nlohmann::json        items = nlohmann::json::array();
    const ACTION_MENU*    actionMenu = dynamic_cast<const ACTION_MENU*>( &aMenu );
    const ACTION_MANAGER* actionMgr = aToolMgr.GetActionManager();

    for( wxMenuItemList::compatibility_iterator node = aMenu.GetMenuItems().GetFirst(); node;
         node = node->GetNext() )
    {
        const wxMenuItem* item = node->GetData();

        if( aCount >= CC_MENU_MAX_ITEMS )
        {
            aCut = true;
            break;
        }

        ++aCount;

        const char* kind = "normal";

        if( item->IsSeparator() )
            kind = "separator";
        else if( item->GetSubMenu() )
            kind = "submenu";
        else if( item->GetKind() == wxITEM_CHECK )
            kind = "check";
        else if( item->GetKind() == wxITEM_RADIO )
            kind = "radio";

        wxString label = item->IsSeparator() ? wxString() : item->GetItemLabelText();

        if( label.length() > CC_MENU_MAX_LABEL )
        {
            label.Truncate( CC_MENU_MAX_LABEL );
            aCut = true;
        }

        const TOOL_ACTION* action = nullptr;

        if( actionMenu && actionMgr && actionMgr->IsActionUIId( item->GetId() ) )
            action = actionMenu->ActionFor( item->GetId() );

        std::string key;

        if( action && action->GetHotKey() != 0 )
            key = KeyNameFromKeyCode( action->GetHotKey() ).ToStdString( wxConvUTF8 );

        nlohmann::json entry;
        entry["id"] = item->GetId();
        entry["kind"] = kind;
        entry["label"] = label.ToStdString( wxConvUTF8 );
        entry["enabled"] = item->IsEnabled();
        entry["checked"] = item->IsCheckable() && item->IsChecked();
        entry["action"] = action ? action->GetName() : std::string();
        entry["key"] = key;

        if( const wxMenu* submenu = item->GetSubMenu() )
        {
            if( aDepth + 1 < CC_MENU_MAX_DEPTH )
                entry["items"] = CcMenuItems( aToolMgr, *submenu, aDepth + 1, aCount, aCut );
            else
            {
                entry["items"] = nlohmann::json::array();
                aCut = true;
            }
        }

        items.push_back( std::move( entry ) );
    }

    return items;
}


/** Send wxEVT_MENU_OPEN to @a aMenu and every ACTION_MENU submenu, as wx does on opening each. */
inline void CcMenuOpenAll( wxMenu& aMenu )
{
    if( ACTION_MENU* actionMenu = dynamic_cast<ACTION_MENU*>( &aMenu ) )
    {
        wxMenuEvent open( wxEVT_MENU_OPEN, 0, &aMenu );
        actionMenu->OnMenuEvent( open );
    }

    for( wxMenuItemList::compatibility_iterator node = aMenu.GetMenuItems().GetFirst(); node;
         node = node->GetNext() )
    {
        if( wxMenu* submenu = node->GetData()->GetSubMenu() )
            CcMenuOpenAll( *submenu );
    }
}


/** True when the page installed window.kicadHostMenu.onMenu (an older island has none). */
inline bool CcHostMenuListening()
{
    return EM_ASM_INT( {
        return ( window.kicadHostMenu && typeof window.kicadHostMenu.onMenu === 'function' )
                       ? 1 : 0;
    } ) != 0;
}


/** Call window.kicadHostMenu.onMenu with JSON text; true when it answered truthy. */
inline bool CcCallHostMenu( const nlohmann::json& aModel )
{
    const std::string text =
            aModel.dump( -1, ' ', false, nlohmann::json::error_handler_t::replace );

    int taken = EM_ASM_INT( {
        if( !window.kicadHostMenu || typeof window.kicadHostMenu.onMenu !== 'function' )
            return 0;

        // A throwing listener must never unwind the wasm frame that called it.
        try { return window.kicadHostMenu.onMenu( UTF8ToString( $0 ) ) ? 1 : 0; }
        catch( e ) { console.error( '[cc menu] onMenu listener threw', e ); return 0; }
    }, text.c_str() );

    return taken != 0;
}


/**
 * Offer the context menu @a aMenu (the clone DispatchContextMenu shows) to the host.
 *
 * @param aClarify the trigger was CMENU_NOW (the clarify-selection list).
 * @param aCursor  the menu cursor in world coordinates (TOOL_MANAGER::m_menuCursor).
 * @return true when the host took the menu (a choice, if any, has been replayed); false to
 *         show KiCad's own popup.
 */
inline bool CcHostTakesMenu( TOOL_MANAGER& aToolMgr, ACTION_MENU& aMenu, wxWindow* aFrame,
                             const TOOL_BASE* aTool, bool aClarify, const VECTOR2D& aCursor )
{
    // No listener: KiCad's popup exactly as before, nothing of this hook runs.
    if( !aFrame || CcHostMenuParked() || !CcHostMenuListening() )
        return false;

    // What wxWindowBase::PopupMenu does before drawing: the invoking window, then the menu's
    // update UI and its open events (each ACTION_MENU's dirty update runs as when wx opens it).
    aMenu.SetInvokingWindow( aFrame );
    aMenu.UpdateUI();
    CcMenuOpenAll( aMenu );

    const int token = CcNextMenuToken();
    int       count = 0;
    bool      cut = false;

    VECTOR2D at = aCursor;

    if( KIGFX::VIEW* view = aToolMgr.GetView() )
        at = view->ToScreen( aCursor );

    nlohmann::json model;
    model["token"] = token;
    model["clarify"] = aClarify;
    model["tool"] = aTool ? aTool->GetName() : std::string();
    model["title"] = aMenu.GetTitle().ToStdString( wxConvUTF8 );
    model["at"] = { { "x", at.x }, { "y", at.y } };
    model["kinds"] = CcMenuKinds( aToolMgr );
    model["items"] = CcMenuItems( aToolMgr, aMenu, 0, count, cut );

    if( cut )
        model["cut"] = true;

    // Begin the wait BEFORE asking: a choice racing ahead of the suspend pre-resolves it.
    const int wait = wxWasmBeginWait( "menu" );

    CC_PARKED_MENU& parked = CcParkedMenu();
    parked.token = token;
    parked.waitToken = wait;
    parked.menu = &aMenu;
    parked.invoker = aFrame;

    if( !CcCallHostMenu( model ) )
    {
        parked = CC_PARKED_MENU();
        wxWasmResolveWait( wait, -1 );
        wxWasmYieldUntil( wait );   // pre-resolved: returns at once, never suspends
        return false;
    }

    const int chosen = CcHostWait( wait, "CcHostTakesMenu" );

    if( CcParkedMenu().token == token )
        CcParkedMenu() = CC_PARKED_MENU();

    if( chosen < 0 )
        return true;   // canceled: DispatchContextMenu sends KiCad's canceled event

    // The replay of the port's DoPopupMenu: toggle a checkable, then SendEvent from the menu
    // that contains the item (wxEVT_MENU reaches ACTION_MENU::OnMenuEvent, then the frame).
    wxMenu*     containing = nullptr;
    wxMenuItem* item = aMenu.FindItem( chosen, &containing );

    if( !item )
        return true;

    const bool checkable = item->IsCheckable();

    if( checkable )
        item->Toggle();

    if( containing )
        containing->SendEvent( chosen, checkable ? item->IsChecked() : -1 );

    return true;
}


/** The parked menu's item @a aId for the choose and highlight exports: 0 or the error code. */
inline int CcHostMenuItemCheck( int aToken, int aId, wxMenu** aContaining )
{
    const CC_PARKED_MENU& parked = CcParkedMenu();

    if( parked.token == 0 || parked.token != aToken || !parked.menu )
        return 1;   // stale

    wxMenuItem* item = parked.menu->FindItem( aId, aContaining );

    if( !item )
        return 2;   // no item with that id

    if( item->IsSeparator() || item->GetSubMenu() )
        return 4;   // a submenu or separator

    if( !item->IsEnabled() )
        return 3;   // disabled

    return 0;
}


/**
 * kicadContextMenuChoose: resolve the parked menu with @a aId (-1 cancels).
 *
 * @return 0 done, 1 stale, 2 no item with that id, 3 disabled, 4 a submenu or separator.
 */
inline int CcHostMenuChoose( int aToken, int aId )
{
    CC_PARKED_MENU& parked = CcParkedMenu();

    if( parked.token == 0 || parked.token != aToken )
        return 1;

    if( aId == -1 )
    {
        CcHostMenuCancelParked();
        return 0;
    }

    wxMenu*   containing = nullptr;
    const int check = CcHostMenuItemCheck( aToken, aId, &containing );

    if( check != 0 )
        return check;

    const int wait = parked.waitToken;
    parked = CC_PARKED_MENU();
    wxWasmResolveWait( wait, aId );
    return 0;
}


/**
 * kicadContextMenuHighlight: send wxEVT_MENU_HIGHLIGHT for @a aId to the parked menu's
 * containing menu, as KiCad's desktop popup does while the pointer moves over an item
 * (ACTION_MENU::OnMenuEvent turns it into TA_CHOICE_MENU_UPDATE: the clarify list brightens
 * that candidate). The event is delivered from the frame's own event loop, as a wx event is.
 *
 * @return the codes of CcHostMenuChoose.
 */
inline int CcHostMenuHighlight( int aToken, int aId )
{
    wxMenu*   containing = nullptr;
    const int check = CcHostMenuItemCheck( aToken, aId, &containing );

    if( check != 0 )
        return check;

    ACTION_MENU* actionMenu = dynamic_cast<ACTION_MENU*>( containing );
    wxWindow*    invoker = CcParkedMenu().invoker;

    if( !actionMenu || !invoker )
        return 2;

    invoker->CallAfter(
            [aToken, aId, actionMenu]()
            {
                // The menu may have closed before the loop came round: highlight nothing.
                if( CcParkedMenu().token != aToken )
                    return;

                wxMenuEvent highlight( wxEVT_MENU_HIGHLIGHT, aId, actionMenu );
                actionMenu->OnMenuEvent( highlight );
            } );

    return 0;
}

#endif // __EMSCRIPTEN__

#endif // HOST_CONTEXT_MENU_H
