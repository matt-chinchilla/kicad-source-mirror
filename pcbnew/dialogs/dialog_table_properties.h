/*
 * This program source code file is part of KiCad, a free EDA CAD application.
 *
 * Copyright The KiCad Developers, see AUTHORS.txt for contributors.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you may find one here:
 * http://www.gnu.org/licenses/old-licenses/gpl-2.0.html
 * or you may search the http://www.gnu.org website for the version 2 license,
 * or you may write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 *
 * Modified by Circuit Center Inc on 2026-11-18: DIALOG_TABLE_PROPERTIES declares a ShowModal and
 * a ShowQuasiModal that ask the in-browser editor's host first (WebAssembly build only).
 */

#ifndef DIALOG_TABLE_PROPERTIES_H
#define DIALOG_TABLE_PROPERTIES_H

#include <wx/hyperlink.h>

#include <widgets/unit_binder.h>
#include <dialog_table_properties_base.h>


class PCB_BASE_EDIT_FRAME;
class PCB_TABLE;
class WX_GRID;
class HTML_MESSAGE_BOX;
class wxHyperlinkCtrl;


class DIALOG_TABLE_PROPERTIES : public DIALOG_TABLE_PROPERTIES_BASE
{
public:
    DIALOG_TABLE_PROPERTIES( PCB_BASE_EDIT_FRAME* aParentFrame, PCB_TABLE* aTable );
    ~DIALOG_TABLE_PROPERTIES();

#ifdef __EMSCRIPTEN__
    /// Ask the in-browser editor's host first (host_prompt.h, ask "table"); KiCad's dialog
    /// only when the host does not take it.
    int ShowModal() override;
    int ShowQuasiModal();
#endif

private:
#ifdef __EMSCRIPTEN__
    /// 1 answered, 0 canceled, -1 not taken (CcHostPromptModal).
    int ccAskHost();

    /// True when the grid's cell at aRow, aCol stands for a cell a merge covers.
    bool ccCoveredCell( int aRow, int aCol ) const;
#endif

    void sizeGridToTable();

    bool TransferDataToWindow() override;
    bool TransferDataFromWindow() override;

    void onBorderChecked( wxCommandEvent& aEvent ) override;
    void onHeaderChecked( wxCommandEvent& aEvent ) override;
    void onSize( wxSizeEvent& aEvent ) override;

    void onSyntaxHelp( wxHyperlinkEvent& aEvent );

private:
    PCB_BASE_EDIT_FRAME* m_frame;
    PCB_TABLE*           m_table;

    WX_GRID* m_grid;

    UNIT_BINDER m_borderWidth;
    UNIT_BINDER m_separatorsWidth;

    wxHyperlinkCtrl*  m_syntaxHelp;
    HTML_MESSAGE_BOX* m_helpWindow;
};


#endif //DIALOG_TABLE_PROPERTIES_H
