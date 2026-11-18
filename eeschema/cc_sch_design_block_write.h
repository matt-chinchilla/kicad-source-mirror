// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_SCH_DESIGN_BLOCK_WRITE_H
#define CC_SCH_DESIGN_BLOCK_WRITE_H

#ifdef __EMSCRIPTEN__

#include <vector>
#include <wx/string.h>

class SCH_EDIT_FRAME;
class SCH_SHEET_PATH;

/**
 * K-B3, the schematic half (engine run 15, kicadDesignBlockSave): the bodies of
 * SCH_EDIT_FRAME::SaveSheetAsDesignBlock and SaveSelectionAsDesignBlock that write the block's
 * schematic, lifted into free functions that take the block's folder. No library is chosen or
 * written, no properties dialog and no overwrite question are shown, and nothing in the design
 * changes (KiCad's selection save also groups the selection afterwards; this one does not).
 * The file lands at <aFolder>/<aName>.kicad_sch (the folder made when missing), written by
 * KiCad's own schematic writer. On a refusal or a failure aLines receives KiCad's own message
 * and the function answers false.
 */
bool CcWriteSheetDesignBlock( SCH_EDIT_FRAME* aFrame, const SCH_SHEET_PATH& aSheetPath,
                              const wxString& aFolder, const wxString& aName,
                              std::vector<wxString>& aLines );

bool CcWriteSchSelectionDesignBlock( SCH_EDIT_FRAME* aFrame, const wxString& aFolder,
                                     const wxString& aName, std::vector<wxString>& aLines );

#endif // __EMSCRIPTEN__

#endif // CC_SCH_DESIGN_BLOCK_WRITE_H
