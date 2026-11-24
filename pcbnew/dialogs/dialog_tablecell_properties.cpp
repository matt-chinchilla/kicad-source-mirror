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
 * Modified by Circuit Center Inc on 2026-11-18: DIALOG_TABLECELL_PROPERTIES asks the in-browser
 * editor's host to answer it (host_prompt.h, ask tableCell) before KiCad's own dialog shows
 * (WebAssembly build only).
 *
 * Modified by Circuit Center Inc on 2026-11-23: the host's answer is checked in
 * kicadPromptAnswer and the cells' commit runs after the parked wait resumes, on the tool's
 * coroutine (a commit with the table selected dispatches an event that cannot run inside the
 * plain export; WebAssembly build only).
 */

#include <wx/hyperlink.h>

#include <widgets/bitmap_button.h>
#include <widgets/font_choice.h>
#include <dialogs/html_message_box.h>
#include <confirm.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <board.h>
#include <footprint.h>
#include <pcb_textbox.h>
#include <pcb_tablecell.h>
#include <pcb_table.h>
#include <project.h>
#include <pcb_edit_frame.h>
#include <pcb_layer_box_selector.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>
#include <scintilla_tricks.h>
#include "dialog_tablecell_properties.h"

#ifdef __EMSCRIPTEN__
#include <cmath>
#include <limits>
#include <optional>
#include <host_prompt.h>


/// A color as the in-browser editor's records carry it: "#rrggbbaa", "" when unspecified.
[[maybe_unused]] static std::string ccTableColor( const KIGFX::COLOR4D& aColor )
{
    if( aColor == KIGFX::COLOR4D::UNSPECIFIED )
        return std::string();

    return aColor.ToHexString().Lower().ToStdString( wxConvUTF8 );
}


/// "" is unspecified, "#rrggbb" or "#rrggbbaa" a color; anything else is refused.
[[maybe_unused]] static bool ccTableColorOf( const nlohmann::json& aValue, KIGFX::COLOR4D& aColor )
{
    if( !aValue.is_string() )
        return false;

    const std::string text = aValue.get<std::string>();

    if( text.empty() )
    {
        aColor = KIGFX::COLOR4D::UNSPECIFIED;
        return true;
    }

    KIGFX::COLOR4D color;

    if( ( text.size() != 7 && text.size() != 9 )
        || !color.SetFromHexString( wxString::FromUTF8( text ) ) )
    {
        return false;
    }

    aColor = color;
    return true;
}


[[maybe_unused]] static const char* const CC_TABLE_STYLES[] = { "solid", "dash", "dot", "dashdot", "dashdotdot" };


/// The name of the line style a style combo's selection stands for (lineTypeNames' order).
[[maybe_unused]] static std::string ccTableStyle( int aSelection )
{
    if( aSelection < 0 || aSelection >= (int) lineTypeNames.size() )
        aSelection = 0;

    auto it = lineTypeNames.begin();
    std::advance( it, aSelection );

    const int style = static_cast<int>( it->first );

    return ( style >= 0 && style < 5 ) ? CC_TABLE_STYLES[style] : CC_TABLE_STYLES[0];
}


/// The style combo's selection for a style name, -1 when the name is not one.
[[maybe_unused]] static int ccTableStyleSelection( const nlohmann::json& aValue )
{
    if( !aValue.is_string() )
        return -1;

    const std::string name = aValue.get<std::string>();

    for( int ii = 0; ii < 5; ++ii )
    {
        if( name != CC_TABLE_STYLES[ii] )
            continue;

        auto it = lineTypeNames.find( static_cast<LINE_STYLE>( ii ) );

        return it == lineTypeNames.end() ? -1
                                         : (int) std::distance( lineTypeNames.begin(), it );
    }

    return -1;
}


/// A length in millimeters to internal units, refused when not a finite number in range.
[[maybe_unused]] static bool ccTableLength( const nlohmann::json& aValue, const EDA_IU_SCALE& aScale,
                           long long& aIU )
{
    if( !aValue.is_number() )
        return false;

    const double mm = aValue.get<double>();

    if( !std::isfinite( mm ) )
        return false;

    const double iu = std::round( mm * aScale.IU_PER_MM );

    if( std::fabs( iu ) > (double) std::numeric_limits<int>::max() )
        return false;

    aIU = static_cast<long long>( iu );
    return true;
}

/// The table cell answer's keys both editors share, read whole before anything is written.
struct CC_CELL_IN
{
    std::optional<std::string> text;
    std::optional<std::string> font;
    std::optional<bool>        bold;
    std::optional<bool>        italic;
    std::optional<int>         hAlign;     ///< 0 left, 1 center, 2 right
    std::optional<int>         vAlign;     ///< 0 top, 1 center, 2 bottom
    std::optional<long long>   margins[4]; ///< left, top, right, bottom
    bool                       editTable = false;
};


/// Reads the shared keys; a null or a missing key leaves its value as it is. A key this editor
/// does not have (aForeign) is taken only as null.
[[maybe_unused]] static bool ccCellIn( const nlohmann::json& aAnswer, const EDA_IU_SCALE& aScale,
                                       std::initializer_list<const char*> aForeign,
                                       CC_CELL_IN& aIn, std::string& aField )
{
    aField.clear();

    if( !aAnswer.is_object() )
        return false;

    auto given = [&]( const char* aKey )
    {
        aField = aKey;
        return aAnswer.contains( aKey ) && !aAnswer[aKey].is_null();
    };

    for( const char* key : aForeign )
    {
        if( given( key ) )
            return false;
    }

    if( given( "text" ) )
    {
        if( !aAnswer["text"].is_string() )
            return false;

        aIn.text = aAnswer["text"].get<std::string>();
    }

    if( given( "font" ) )
    {
        if( !aAnswer["font"].is_string() )
            return false;

        aIn.font = aAnswer["font"].get<std::string>();
    }

    for( const char* key : { "bold", "italic" } )
    {
        if( given( key ) )
        {
            if( !aAnswer[key].is_boolean() )
                return false;

            ( std::string( key ) == "bold" ? aIn.bold : aIn.italic ) = aAnswer[key].get<bool>();
        }
    }

    auto alignOf = [&]( const char* aKey, std::initializer_list<const char*> aNames,
                        std::optional<int>& aOut )
    {
        if( !given( aKey ) )
            return true;

        if( !aAnswer[aKey].is_string() )
            return false;

        int index = 0;

        for( const char* name : aNames )
        {
            if( aAnswer[aKey].get<std::string>() == name )
            {
                aOut = index;
                return true;
            }

            ++index;
        }

        return false;
    };

    if( !alignOf( "hAlign", { "left", "center", "right" }, aIn.hAlign )
        || !alignOf( "vAlign", { "top", "center", "bottom" }, aIn.vAlign ) )
    {
        return false;
    }

    if( given( "margins" ) )
    {
        const nlohmann::json& margins = aAnswer["margins"];

        if( !margins.is_object() )
            return false;

        int index = 0;

        for( const char* side : { "left", "top", "right", "bottom" } )
        {
            if( margins.contains( side ) && !margins[side].is_null() )
            {
                aField = std::string( "margins." ) + side;
                long long iu = 0;

                if( !ccTableLength( margins[side], aScale, iu ) || iu < 0 )
                    return false;

                aIn.margins[index] = iu;
            }

            ++index;
        }
    }

    if( given( "editTable" ) )
    {
        if( !aAnswer["editTable"].is_boolean() )
            return false;

        aIn.editTable = aAnswer["editTable"].get<bool>();
    }

    aField.clear();
    return true;
}


/// The alignment a row of three radio buttons shows, null when none is checked.
[[maybe_unused]] static nlohmann::json ccCellAlign( BITMAP_BUTTON* aFirst, BITMAP_BUTTON* aSecond,
                                                    BITMAP_BUTTON* aThird, const char* aFirstName,
                                                    const char* aThirdName )
{
    if( aFirst->IsChecked() )
        return aFirstName;

    if( aSecond->IsChecked() )
        return "center";

    if( aThird->IsChecked() )
        return aThirdName;

    return nullptr;
}


/// Checks one button of a row of three (the dialog's own radio behavior), the others off.
[[maybe_unused]] static void ccCellAlignSet( BITMAP_BUTTON* aFirst, BITMAP_BUTTON* aSecond,
                                             BITMAP_BUTTON* aThird, int aIndex )
{
    aFirst->Check( aIndex == 0 );
    aSecond->Check( aIndex == 1 );
    aThird->Check( aIndex == 2 );
}


/// A binder's value in millimeters, null when it shows mixed values.
[[maybe_unused]] static nlohmann::json ccCellLength( const UNIT_BINDER& aBinder,
                                                     const EDA_IU_SCALE& aScale )
{
    if( aBinder.IsIndeterminate() )
        return nullptr;

    return aScale.IUTomm( aBinder.GetValue() );
}
#endif // __EMSCRIPTEN__

DIALOG_TABLECELL_PROPERTIES::DIALOG_TABLECELL_PROPERTIES( PCB_BASE_EDIT_FRAME*        aFrame,
                                                          std::vector<PCB_TABLECELL*> aCells ) :
        DIALOG_TABLECELL_PROPERTIES_BASE( aFrame ),
        m_frame( aFrame ),
        m_table( nullptr ),
        m_cells( std::move( aCells ) ),
        m_textHeight( aFrame, m_SizeYLabel, m_SizeYCtrl, m_SizeYUnits ),
        m_textWidth( aFrame, m_SizeXLabel, m_SizeXCtrl, m_SizeXUnits ),
        m_textThickness( aFrame, m_ThicknessLabel, m_ThicknessCtrl, m_ThicknessUnits ),
        m_marginLeft( aFrame, nullptr, m_marginLeftCtrl, nullptr ),
        m_marginTop( aFrame, nullptr, m_marginTopCtrl, m_marginTopUnits ),
        m_marginRight( aFrame, nullptr, m_marginRightCtrl, nullptr ),
        m_marginBottom( aFrame, nullptr, m_marginBottomCtrl, nullptr ),
        m_cellText( m_cellTextCtrl ),
        m_returnValue( TABLECELL_PROPS_CANCEL )
{
    wxASSERT( m_cells.size() > 0 && m_cells[0] );

    m_cellText->SetEOLMode( wxSTC_EOL_LF );

#ifdef _WIN32
    // Without this setting, on Windows, some esoteric unicode chars create display issue
    // in a wxStyledTextCtrl.
    // for SetTechnology() info, see https://www.scintilla.org/ScintillaDoc.html#SCI_SETTECHNOLOGY
    m_cellText->SetTechnology( wxSTC_TECHNOLOGY_DIRECTWRITE );
#endif

    m_scintillaTricks = new SCINTILLA_TRICKS(
            m_cellText, wxT( "{}" ), false,
            // onAcceptFn
            [this]( wxKeyEvent& aEvent )
            {
                wxPostEvent( this, wxCommandEvent( wxEVT_COMMAND_BUTTON_CLICKED, wxID_OK ) );
            },
            // onCharFn
            [this]( wxStyledTextEvent& aEvent )
            {
                m_scintillaTricks->DoTextVarAutocomplete(
                        // getTokensFn
                        [this]( const wxString& xRef, wxArrayString* tokens )
                        {
                            m_frame->GetContextualTextVars( m_table, xRef, tokens );
                        } );
            } );

    // A hack which causes Scintilla to auto-size the text editor canvas
    // See: https://github.com/jacobslusser/ScintillaNET/issues/216
    m_cellText->SetScrollWidth( 1 );
    m_cellText->SetScrollWidthTracking( true );

    SetInitialFocus( m_cellText );

    m_table = static_cast<PCB_TABLE*>( m_cells[0]->GetParent() );

    m_hAlignLeft->SetIsRadioButton();
    m_hAlignLeft->SetBitmap( KiBitmapBundle( BITMAPS::text_align_left ) );
    m_hAlignCenter->SetIsRadioButton();
    m_hAlignCenter->SetBitmap( KiBitmapBundle( BITMAPS::text_align_center ) );
    m_hAlignRight->SetIsRadioButton();
    m_hAlignRight->SetBitmap( KiBitmapBundle( BITMAPS::text_align_right ) );

    m_separator0->SetIsSeparator();

    m_vAlignTop->SetIsRadioButton();
    m_vAlignTop->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_top ) );
    m_vAlignCenter->SetIsRadioButton();
    m_vAlignCenter->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_center ) );
    m_vAlignBottom->SetIsRadioButton();
    m_vAlignBottom->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_bottom ) );

    m_separator1->SetIsSeparator();

    m_bold->SetIsCheckButton();
    m_bold->SetBitmap( KiBitmapBundle( BITMAPS::text_bold ) );
    m_italic->SetIsCheckButton();
    m_italic->SetBitmap( KiBitmapBundle( BITMAPS::text_italic ) );

    m_autoTextThickness->SetIsCheckButton();
    m_autoTextThickness->SetBitmap( KiBitmapBundle( BITMAPS::edit_cmp_symb_links ) );

    SetupStandardButtons();
    Layout();

    m_helpWindow = nullptr;

    Connect( wxEVT_CHAR_HOOK, wxKeyEventHandler( DIALOG_TABLECELL_PROPERTIES::OnCharHook ), nullptr, this );

    m_hAlignLeft->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onHAlignButton, this );
    m_hAlignCenter->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onHAlignButton, this );
    m_hAlignRight->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onHAlignButton, this );
    m_vAlignTop->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onVAlignButton, this );
    m_vAlignCenter->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onVAlignButton, this );
    m_vAlignBottom->Bind( wxEVT_BUTTON, &DIALOG_TABLECELL_PROPERTIES::onVAlignButton, this );

    // Now all widgets have the size fixed, call FinishDialogSettings
    finishDialogSettings();
}

DIALOG_TABLECELL_PROPERTIES::~DIALOG_TABLECELL_PROPERTIES()
{
    Disconnect( wxEVT_CHAR_HOOK, wxKeyEventHandler( DIALOG_TABLECELL_PROPERTIES::OnCharHook ), nullptr, this );

    delete m_scintillaTricks;
    m_scintillaTricks = nullptr;

    if( m_helpWindow )
        m_helpWindow->Destroy();
}


bool DIALOG_TABLECELL_PROPERTIES::TransferDataToWindow()
{
    if( !wxDialog::TransferDataToWindow() )
        return false;

    bool              firstCell = true;
    GR_TEXT_H_ALIGN_T hAlign = GR_TEXT_H_ALIGN_INDETERMINATE;
    GR_TEXT_V_ALIGN_T vAlign = GR_TEXT_V_ALIGN_INDETERMINATE;
    int               textThickness = 0;
    int               effectivePenWidth = 0;

    for( PCB_TABLECELL* cell : m_cells )
    {
        if( firstCell )
        {
            m_cellTextCtrl->SetValue( cell->GetText() );

            m_fontCtrl->SetFontSelection( cell->GetFont() );
            m_textWidth.SetValue( cell->GetTextWidth() );
            m_textHeight.SetValue( cell->GetTextHeight() );
            textThickness = cell->GetTextThickness();
            effectivePenWidth = cell->GetEffectiveTextPenWidth();

            hAlign = cell->GetHorizJustify();
            vAlign = cell->GetVertJustify();

            m_marginLeft.SetValue( cell->GetMarginLeft() );
            m_marginTop.SetValue( cell->GetMarginTop() );
            m_marginRight.SetValue( cell->GetMarginRight() );
            m_marginBottom.SetValue( cell->GetMarginBottom() );

            // wxCheckBoxState bold = cell->IsBold() ? wxCHK_CHECKED : wxCHK_UNCHECKED;
            m_bold->Check( cell->IsBold() );

            // wxCheckBoxState italic = cell->IsItalic() ? wxCHK_CHECKED : wxCHK_UNCHECKED;
            m_italic->Check( cell->IsItalic() );

            firstCell = false;
        }
        else
        {
            if( cell->GetText() != m_cellTextCtrl->GetValue() )
                m_cellTextCtrl->SetValue( INDETERMINATE_STATE );

            if( cell->GetFont() != m_fontCtrl->GetFontSelection( cell->IsBold(), cell->IsItalic() ) )
                m_fontCtrl->SetSelection( -1 );

            if( cell->GetTextWidth() != m_textWidth.GetValue() )
                m_textWidth.SetValue( INDETERMINATE_STATE );

            if( cell->GetTextHeight() != m_textHeight.GetValue() )
                m_textHeight.SetValue( INDETERMINATE_STATE );

            if( cell->GetTextThickness() != textThickness )
                textThickness = -1;

            if( cell->GetEffectiveTextPenWidth() != effectivePenWidth )
                effectivePenWidth = -1;

            if( cell->GetHorizJustify() != hAlign )
                hAlign = GR_TEXT_H_ALIGN_INDETERMINATE;

            if( cell->GetVertJustify() != vAlign )
                vAlign = GR_TEXT_V_ALIGN_INDETERMINATE;

            if( cell->GetMarginLeft() != m_marginLeft.GetIntValue() )
                m_marginLeft.SetValue( INDETERMINATE_STATE );

            if( cell->GetMarginTop() != m_marginTop.GetIntValue() )
                m_marginTop.SetValue( INDETERMINATE_STATE );

            if( cell->GetMarginRight() != m_marginRight.GetIntValue() )
                m_marginRight.SetValue( INDETERMINATE_STATE );

            if( cell->GetMarginBottom() != m_marginBottom.GetIntValue() )
                m_marginBottom.SetValue( INDETERMINATE_STATE );
        }

        switch( hAlign )
        {
        case GR_TEXT_H_ALIGN_LEFT: m_hAlignLeft->Check(); break;
        case GR_TEXT_H_ALIGN_CENTER: m_hAlignCenter->Check(); break;
        case GR_TEXT_H_ALIGN_RIGHT: m_hAlignRight->Check(); break;
        case GR_TEXT_H_ALIGN_INDETERMINATE: break;
        }

        switch( vAlign )
        {
        case GR_TEXT_V_ALIGN_TOP: m_vAlignTop->Check(); break;
        case GR_TEXT_V_ALIGN_CENTER: m_vAlignCenter->Check(); break;
        case GR_TEXT_V_ALIGN_BOTTOM: m_vAlignBottom->Check(); break;
        case GR_TEXT_V_ALIGN_INDETERMINATE: break;
        }
    }

    m_textThickness.SetValue( INDETERMINATE_STATE );
    m_autoTextThickness->Check( false );

    if( textThickness == 0 )
    {
        if( effectivePenWidth > 0 )
            m_textThickness.SetValue( effectivePenWidth );

        m_autoTextThickness->Check( true );
        m_textThickness.Enable( false );
    }
    else if( textThickness > 0 )
    {
        m_textThickness.SetValue( textThickness );
    }

    return true;
}

void DIALOG_TABLECELL_PROPERTIES::onBoldToggle( wxCommandEvent& aEvent )
{
    int textSize = std::min( m_textWidth.GetValue(), m_textHeight.GetValue() );

    if( aEvent.IsChecked() )
        m_textThickness.ChangeValue( GetPenSizeForBold( textSize ) );
    else
        m_textThickness.ChangeValue( GetPenSizeForNormal( textSize ) );

    aEvent.Skip();
}

void DIALOG_TABLECELL_PROPERTIES::onTextSize( wxCommandEvent& aEvent )
{
    if( m_autoTextThickness->IsChecked() )
    {
        int textSize = std::min( m_textWidth.GetValue(), m_textHeight.GetValue() );
        int thickness;

        // Calculate the "best" thickness from text size and bold option:
        if( m_bold->IsChecked() )
            thickness = GetPenSizeForBold( textSize );
        else
            thickness = GetPenSizeForNormal( textSize );

        m_textThickness.SetValue( thickness );
    }
}


void DIALOG_TABLECELL_PROPERTIES::onAutoTextThickness( wxCommandEvent& aEvent )
{
    if( aEvent.IsChecked() )
    {
        m_autoTextThickness->Check( true );

        if( !m_textWidth.IsIndeterminate() && !m_textHeight.IsIndeterminate() )
        {
            wxCommandEvent dummy;
            onTextSize( dummy );
        }

        m_textThickness.Enable( false );
    }
    else
    {
        m_textThickness.Enable( true );
    }
}


void DIALOG_TABLECELL_PROPERTIES::onHAlignButton( wxCommandEvent& aEvent )
{
    for( BITMAP_BUTTON* btn : { m_hAlignLeft, m_hAlignCenter, m_hAlignRight } )
    {
        if( btn->IsChecked() && btn != aEvent.GetEventObject() )
            btn->Check( false );
    }
}


void DIALOG_TABLECELL_PROPERTIES::onVAlignButton( wxCommandEvent& aEvent )
{
    for( BITMAP_BUTTON* btn : { m_vAlignTop, m_vAlignCenter, m_vAlignBottom } )
    {
        if( btn->IsChecked() && btn != aEvent.GetEventObject() )
            btn->Check( false );
    }
}


bool DIALOG_TABLECELL_PROPERTIES::TransferDataFromWindow()
{
    if( !wxDialog::TransferDataFromWindow() )
        return false;

    BOARD_COMMIT commit( m_frame );
    commit.Modify( m_table );

    // If no other command in progress, prepare undo command
    // (for a command in progress, will be made later, at the completion of command)
    bool pushCommit = ( m_table->GetEditFlags() == 0 );

    // Set IN_EDIT flag to force undo/redo/abort proper operation and avoid new calls to
    // SaveCopyInUndoList for the same text if is moved, and then rotated, edited, etc....
    if( !pushCommit )
        m_table->SetFlags( IN_EDIT );

    for( PCB_TABLECELL* cell : m_cells )
    {
        if( m_cellTextCtrl->GetValue() != INDETERMINATE_STATE )
        {
            wxString txt = m_cellTextCtrl->GetValue();

#ifdef __WXMAC__
            // On macOS CTRL+Enter produces '\r' instead of '\n' regardless of EOL setting.
            // Replace it now.
            txt.Replace( "\r", "\n" );
#elif defined( __WINDOWS__ )
            // On Windows, a new line is coded as \r\n.  We use only \n in kicad files and in
            // drawing routines so strip the \r char.
            txt.Replace( "\r", "" );
#endif

            cell->SetText( txt );
        }

        cell->SetBold( m_bold->IsChecked() );
        cell->SetItalic( m_italic->IsChecked() );

        if( m_fontCtrl->HaveFontSelection() )
            cell->SetFont( m_fontCtrl->GetFontSelection( cell->IsBold(), cell->IsItalic() ) );

        if( !m_textWidth.IsIndeterminate() )
            cell->SetTextWidth( m_textWidth.GetIntValue() );

        if( !m_textHeight.IsIndeterminate() )
            cell->SetTextHeight( m_textHeight.GetIntValue() );

        if( m_autoTextThickness->IsChecked() )
            cell->SetAutoThickness( true );
        else if( !m_textThickness.IsIndeterminate() )
            cell->SetTextThickness( m_textThickness.GetIntValue() );

        if( m_hAlignLeft->IsChecked() )
            cell->SetHorizJustify( GR_TEXT_H_ALIGN_LEFT );
        else if( m_hAlignRight->IsChecked() )
            cell->SetHorizJustify( GR_TEXT_H_ALIGN_RIGHT );
        else if( m_hAlignCenter->IsChecked() )
            cell->SetHorizJustify( GR_TEXT_H_ALIGN_CENTER );

        if( m_vAlignTop->IsChecked() )
            cell->SetVertJustify( GR_TEXT_V_ALIGN_TOP );
        else if( m_vAlignBottom->IsChecked() )
            cell->SetVertJustify( GR_TEXT_V_ALIGN_BOTTOM );
        else if( m_vAlignCenter->IsChecked() )
            cell->SetVertJustify( GR_TEXT_V_ALIGN_CENTER );

        if( !m_marginLeft.IsIndeterminate() )
            cell->SetMarginLeft( m_marginLeft.GetIntValue() );

        if( !m_marginTop.IsIndeterminate() )
            cell->SetMarginTop( m_marginTop.GetIntValue() );

        if( !m_marginRight.IsIndeterminate() )
            cell->SetMarginRight( m_marginRight.GetIntValue() );

        if( !m_marginBottom.IsIndeterminate() )
            cell->SetMarginBottom( m_marginBottom.GetIntValue() );
    }

    if( !commit.Empty() )
        commit.Push( _( "Edit Table Cell Properties" ), SKIP_CONNECTIVITY );

    m_returnValue = TABLECELL_PROPS_OK;
    return true;
}


void DIALOG_TABLECELL_PROPERTIES::onEditTable( wxCommandEvent& aEvent )
{
    if( TransferDataFromWindow() )
    {
        m_returnValue = TABLECELL_PROPS_EDIT_TABLE;
        Close();
    }
}


void DIALOG_TABLECELL_PROPERTIES::onSyntaxHelp( wxHyperlinkEvent& aEvent )
{
    if( m_helpWindow )
    {
        m_helpWindow->Raise();
        m_helpWindow->Show( true );
        return;
    }

    m_helpWindow = PCB_TEXT::ShowSyntaxHelp( this );
}


#ifdef __EMSCRIPTEN__
int DIALOG_TABLECELL_PROPERTIES::ShowModal()
{
    switch( ccAskHost() )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_TABLECELL_PROPERTIES_BASE::ShowModal();
    }
}


int DIALOG_TABLECELL_PROPERTIES::ShowQuasiModal()
{
    switch( ccAskHost() )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_TABLECELL_PROPERTIES_BASE::ShowQuasiModal();
    }
}


int DIALOG_TABLECELL_PROPERTIES::ccAskHost()
{
    if( !TransferDataToWindow() )
        return -1;

    const EDA_IU_SCALE& scale = m_frame->GetIuScale();

    nlohmann::json uuids = nlohmann::json::array();

    for( PCB_TABLECELL* cell : m_cells )
        uuids.push_back( cell->m_Uuid.AsStdString() );

    const wxString text = m_cellTextCtrl->GetValue();

    nlohmann::json record;
    record["uuids"] = uuids;
    record["text"] = text == INDETERMINATE_STATE
                             ? nlohmann::json()
                             : nlohmann::json( text.ToStdString( wxConvUTF8 ) );
    record["font"] = m_fontCtrl->GetSelection() < 0
                             ? nlohmann::json()
                             : nlohmann::json( m_fontCtrl->GetStringSelection().ToStdString(
                                       wxConvUTF8 ) );
    record["bold"] = m_bold->IsChecked();
    record["italic"] = m_italic->IsChecked();
    record["hAlign"] = ccCellAlign( m_hAlignLeft, m_hAlignCenter, m_hAlignRight, "left", "right" );
    record["vAlign"] = ccCellAlign( m_vAlignTop, m_vAlignCenter, m_vAlignBottom, "top", "bottom" );
    record["margins"] = { { "left", ccCellLength( m_marginLeft, scale ) },
                          { "right", ccCellLength( m_marginRight, scale ) },
                          { "top", ccCellLength( m_marginTop, scale ) },
                          { "bottom", ccCellLength( m_marginBottom, scale ) } };
    record["size"] = nullptr;
    record["width"] = ccCellLength( m_textWidth, scale );
    record["height"] = ccCellLength( m_textHeight, scale );
    record["thickness"] = ccCellLength( m_textThickness, scale );
    record["autoThickness"] = m_autoTextThickness->IsChecked();
    record["textColor"] = nullptr;
    record["fill"] = nullptr;

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        RECEIVER( DIALOG_TABLECELL_PROPERTIES* aDialog, const EDA_IU_SCALE& aScale ) :
                m_dialog( aDialog ),
                m_scale( aScale )
        {
        }

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            DIALOG_TABLECELL_PROPERTIES* dlg = m_dialog;
            CC_CELL_IN                   in;

            // The board's cells have no text size of their own (a width and a height) and no
            // colors.
            if( !ccCellIn( aAnswer, m_scale, { "size", "textColor", "fill" }, in, aField ) )
                return false;

            std::optional<long long> lengths[3];   // width, height, thickness
            int                      index = 0;

            for( const char* key : { "width", "height", "thickness" } )
            {
                if( aAnswer.contains( key ) && !aAnswer[key].is_null() )
                {
                    aField = key;
                    long long iu = 0;

                    if( !ccTableLength( aAnswer[key], m_scale, iu ) || iu < 0
                        || ( iu == 0 && index < 2 ) )
                    {
                        return false;
                    }

                    lengths[index] = iu;
                }

                ++index;
            }

            std::optional<bool> autoThickness;

            if( aAnswer.contains( "autoThickness" ) && !aAnswer["autoThickness"].is_null() )
            {
                aField = "autoThickness";

                if( !aAnswer["autoThickness"].is_boolean() )
                    return false;

                autoThickness = aAnswer["autoThickness"].get<bool>();
            }

            if( in.font && !dlg->m_fontCtrl->SetStringSelection( wxString::FromUTF8( *in.font ) ) )
            {
                aField = "font";
                return false;
            }

            aField.clear();

            // Write into the dialog's own controls.
            if( in.text )
                dlg->m_cellText->SetValue( wxString::FromUTF8( *in.text ) );

            if( in.italic )
                dlg->m_italic->Check( *in.italic );

            if( in.bold && *in.bold != dlg->m_bold->IsChecked() )
            {
                dlg->m_bold->Check( *in.bold );

                // The dialog's own toggle handler: the pen thickness follows bold.
                wxCommandEvent toggle;
                toggle.SetInt( *in.bold ? 1 : 0 );
                dlg->onBoldToggle( toggle );
            }

            if( in.hAlign )
            {
                ccCellAlignSet( dlg->m_hAlignLeft, dlg->m_hAlignCenter, dlg->m_hAlignRight,
                                *in.hAlign );
            }

            if( in.vAlign )
            {
                ccCellAlignSet( dlg->m_vAlignTop, dlg->m_vAlignCenter, dlg->m_vAlignBottom,
                                *in.vAlign );
            }

            UNIT_BINDER* margins[4] = { &dlg->m_marginLeft, &dlg->m_marginTop,
                                        &dlg->m_marginRight, &dlg->m_marginBottom };

            for( int ii = 0; ii < 4; ++ii )
            {
                if( in.margins[ii] )
                    margins[ii]->SetValue( *in.margins[ii] );
            }

            if( lengths[0] )
                dlg->m_textWidth.SetValue( *lengths[0] );

            if( lengths[1] )
                dlg->m_textHeight.SetValue( *lengths[1] );

            if( autoThickness )
            {
                dlg->m_autoTextThickness->Check( *autoThickness );
                dlg->m_textThickness.Enable( !*autoThickness );
            }

            if( lengths[2] && !dlg->m_autoTextThickness->IsChecked() )
                dlg->m_textThickness.SetValue( *lengths[2] );

            // The dialog's own size handler: an automatic thickness follows the text size.
            if( dlg->m_autoTextThickness->IsChecked() && !dlg->m_textWidth.IsIndeterminate()
                && !dlg->m_textHeight.IsIndeterminate() )
            {
                wxCommandEvent dummy;
                dlg->onTextSize( dummy );
            }

            // The check TransferDataFromWindow begins with, its only refusal; its commit runs in
            // ccAskHost once the parked wait resumes (a commit here traps when the table is
            // selected: kicadPromptAnswer is a plain export and cannot park).
            if( !dlg->wxDialog::TransferDataFromWindow() )
                return false;

            commitPending = true;
            editTable = in.editTable;
            return true;
        }

        bool commitPending = false;
        bool editTable = false;

    private:
        DIALOG_TABLECELL_PROPERTIES* m_dialog;
        const EDA_IU_SCALE&          m_scale;
    };

    RECEIVER  receiver( this, scale );
    const int answered = CcHostPromptModal( "tableCell", record, receiver );

    // The commit, on the tool's coroutine where KiCad's ShowModal would have made it. It sets
    // TABLECELL_PROPS_OK at its end, so the dialog's own Edit Table button is applied after it:
    // KiCad then shows (asks) the table.
    if( answered == 1 && receiver.commitPending )
    {
        if( !TransferDataFromWindow() )
            return 0;

        if( receiver.editTable )
            m_returnValue = TABLECELL_PROPS_EDIT_TABLE;

        return 1;
    }

    return answered;
}
#endif // __EMSCRIPTEN__
