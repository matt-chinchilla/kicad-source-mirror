// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_PCB_DESIGN_BLOCK_WRITE_H
#define CC_PCB_DESIGN_BLOCK_WRITE_H

#ifdef __EMSCRIPTEN__

#include <vector>
#include <wx/string.h>

class PCB_EDIT_FRAME;

/**
 * K-B3, the board half (engine run 15, kicadDesignBlockSave): the bodies of
 * PCB_EDIT_FRAME::SaveBoardAsDesignBlock, SaveSelectionAsDesignBlock (through
 * saveSelectionToDesignBlock) and the linked group's save (SaveToLinkedDesignBlock, through
 * UpdateDesignBlockFromSelection) that write the block's layout, lifted into free functions
 * that take the block's folder. No library is chosen or written, no properties dialog and no
 * overwrite question are shown, and nothing in the design changes (KiCad's selection saves
 * also group the selection or link the group afterwards; these do not). The file lands at
 * <aFolder>/<aName>.kicad_pcb (the folder made when missing), written by KiCad's own board
 * writer. On a refusal or a failure aLines receives KiCad's own message and the function
 * answers false.
 */
bool CcWriteBoardDesignBlock( PCB_EDIT_FRAME* aFrame, const wxString& aFolder,
                              const wxString& aName, std::vector<wxString>& aLines );

/**
 * The selection's layout. With aLinkedGroupOnly the selection must be one group linked to a
 * design block (KiCad's Save to Linked Design Block), else the selection is any set of items
 * (KiCad's Save Selection as Design Block); in both a single selected group is saved as its
 * children, as KiCad saves it.
 */
bool CcWritePcbSelectionDesignBlock( PCB_EDIT_FRAME* aFrame, bool aLinkedGroupOnly,
                                     const wxString& aFolder, const wxString& aName,
                                     std::vector<wxString>& aLines );

#endif // __EMSCRIPTEN__

#endif // CC_PCB_DESIGN_BLOCK_WRITE_H
