// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_ACTION_STATE_H
#define CC_ACTION_STATE_H

#ifdef __EMSCRIPTEN__

/*
 * The checked state of one tool action of a frame, read for the WebAssembly editor's host (the
 * export kicadActionStates, and kicadActionStateWhy for every step). The whole read runs in the
 * core's own translation unit (common/tool/action_manager.cpp), by two roads:
 *
 *  (a) KiCad's condition map: ACTION_MANAGER::FindAction, ACTION_MANAGER::GetCondition, the
 *      default check condition test (SELECTION_CONDITIONS::ShowNever: the action is no toggle),
 *      then checkCondition( holder->GetCurrentSelection() ) inside try.
 *  (b) KiCad's own UI update road, independent of that map: a wxUpdateUIEvent with the action's
 *      UI id processed by the frame (EDA_BASE_FRAME::onUpdateUI, the handler SetConditions
 *      registered), its checked state when the handler set one.
 *
 * value and uiValue: -1 none, 0 false, 1 true. conditions counts the actions of this manager
 * with a registered condition, twins the actions of KiCad's action list that carry the found
 * action's name, twinCondition is true when a twin other than the found one has a condition.
 */

#include <string>

class TOOL_MANAGER;
class wxWindow;

struct CC_ACTION_CHECK
{
    bool found = false;
    int  id = -1;
    int  uiId = -1;
    bool condition = false;
    bool checkSet = false;
    bool showNever = false;
    int  value = -1;
    bool threw = false;
    int  uiValue = -1;
    int  conditions = 0;
    int  twins = 0;
    bool twinCondition = false;
};

CC_ACTION_CHECK CcActionCheck( TOOL_MANAGER* aMgr, wxWindow* aFrame, const std::string& aName );

#endif // __EMSCRIPTEN__

#endif // CC_ACTION_STATE_H
