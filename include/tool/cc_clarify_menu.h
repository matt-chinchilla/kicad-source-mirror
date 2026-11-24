// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_CLARIFY_MENU_H
#define CC_CLARIFY_MENU_H

#ifdef __EMSCRIPTEN__

/*
 * The clarify-selection lists, told apart from the ordinary context menu. Both are scheduled
 * with CMENU_NOW (TOOL_MENU::ShowContextMenu and SELECTION_TOOL::doSelectionMenu), so the
 * trigger alone cannot say which one TOOL_MANAGER::DispatchContextMenu is showing.
 * doSelectionMenu marks its menu here for as long as the menu lives (CC_CLARIFY_MENU_MARK) and
 * DispatchContextMenu reports clarify to the host only for a marked menu.
 */

#include <set>

class ACTION_MENU;

// The clarify lists alive now.
inline std::set<const ACTION_MENU*>& CcClarifyMenus()
{
    static std::set<const ACTION_MENU*> menus;
    return menus;
}

inline bool CcIsClarifyMenu( const ACTION_MENU* aMenu )
{
    return aMenu && CcClarifyMenus().count( aMenu ) > 0;
}

// Marks a clarify list from its scheduling to the end of the scope that holds it.
struct CC_CLARIFY_MENU_MARK
{
    explicit CC_CLARIFY_MENU_MARK( const ACTION_MENU* aMenu ) :
            m_menu( aMenu )
    {
        CcClarifyMenus().insert( m_menu );
    }

    ~CC_CLARIFY_MENU_MARK() { CcClarifyMenus().erase( m_menu ); }

    CC_CLARIFY_MENU_MARK( const CC_CLARIFY_MENU_MARK& ) = delete;
    CC_CLARIFY_MENU_MARK& operator=( const CC_CLARIFY_MENU_MARK& ) = delete;

    const ACTION_MENU* m_menu;
};

#endif // __EMSCRIPTEN__

#endif // CC_CLARIFY_MENU_H
