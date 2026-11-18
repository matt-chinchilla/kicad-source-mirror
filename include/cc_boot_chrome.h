// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The browser engine's start-up hook (engine run 15, boot-e1). KIWAY::Player calls it with each
// frame it creates while no frame is the top yet: the frame the engine boots, before
// single_top.cpp shows it. The merged image's bindings set it at static initialization
// (pcbjam wasm/bindings/cc_boot_dispatch.inc), so the page may have KiCad's chrome (menu bar,
// toolbars, panes, status bar) held hidden before the frame is ever laid out on screen or
// painted. Null in any other image, where nothing is called.

#ifndef CC_BOOT_CHROME_H
#define CC_BOOT_CHROME_H

#ifdef __EMSCRIPTEN__

class wxFrame;

using CC_BOOT_FRAME_HOOK = void ( * )( wxFrame* aFrame );

// One hook for the whole image (a function local static of an inline function).
inline CC_BOOT_FRAME_HOOK& ccBootFrameHook()
{
    static CC_BOOT_FRAME_HOOK hook = nullptr;
    return hook;
}

#endif // __EMSCRIPTEN__

#endif // CC_BOOT_CHROME_H
