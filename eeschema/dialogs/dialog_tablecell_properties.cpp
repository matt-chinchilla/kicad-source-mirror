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
 */

#include <gr_text.h>
#include <sch_edit_frame.h>
#include <widgets/bitmap_button.h>
#include <widgets/color_swatch.h>
#include <widgets/font_choice.h>
#include <settings/color_settings.h>
#include <widgets/wx_infobar.h>
#include <sch_table.h>
#include <sch_text.h>
#include <sch_commit.h>
#include <tool/tool_manager.h>
#include <dialogs/html_message_box.h>
#include <dialog_tablecell_properties.h>

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

DIALOG_TABLECELL_PROPERTIES::DIALOG_TABLECELL_PROPERTIES( SCH_EDIT_FRAME* aFrame, std::vector<SCH_TABLECELL*> aCells ) :
        DIALOG_TABLECELL_PROPERTIES_BASE( aFrame ),
        m_frame( aFrame ),
        m_table( nullptr ),
        m_cells( std::move( aCells ) ),
        m_scintillaTricks( nullptr ),
        m_helpWindow( nullptr ),
        m_textSize( aFrame, m_textSizeLabel, m_textSizeCtrl, m_textSizeUnits ),
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
                            getContextualTextVars( xRef, tokens );
                        } );
            } );

    SetInitialFocus( m_cellText );

    m_table = static_cast<SCH_TABLE*>( m_cells[0]->GetParent() );

    if( m_frame->GetColorSettings()->GetOverrideSchItemColors() )
        m_infoBar->ShowMessage( _( "Note: individual item colors overridden in Preferences." ) );

    m_hAlignLeft->SetIsRadioButton();
    m_hAlignLeft->SetBitmap( KiBitmapBundle( BITMAPS::text_align_left ) );
    m_hAlignCenter->SetIsRadioButton();
    m_hAlignCenter->SetBitmap( KiBitmapBundle( BITMAPS::text_align_center ) );
    m_hAlignRight->SetIsRadioButton();
    m_hAlignRight->SetBitmap( KiBitmapBundle( BITMAPS::text_align_right ) );

    m_vAlignTop->SetIsRadioButton();
    m_vAlignTop->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_top ) );
    m_vAlignCenter->SetIsRadioButton();
    m_vAlignCenter->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_center ) );
    m_vAlignBottom->SetIsRadioButton();
    m_vAlignBottom->SetBitmap( KiBitmapBundle( BITMAPS::text_valign_bottom ) );

    KIGFX::COLOR4D canvas = aFrame->GetColorSettings()->GetColor( LAYER_SCHEMATIC_BACKGROUND );
    m_textColorSwatch->SetSwatchBackground( canvas );
    m_fillColorSwatch->SetSwatchBackground( canvas );

    SetupStandardButtons();
    Layout();

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
    delete m_scintillaTricks;
    m_scintillaTricks = nullptr;

    if( m_helpWindow )
        m_helpWindow->Destroy();
}

void DIALOG_TABLECELL_PROPERTIES::getContextualTextVars( const wxString& aCrossRef, wxArrayString* aTokens )
{
    SCHEMATIC* schematic = m_table->Schematic();

    if( !aCrossRef.IsEmpty() )
    {
        SCH_SYMBOL* refSymbol = nullptr;

        if( schematic )
        {
            SCH_REFERENCE_LIST refs;
            schematic->Hierarchy().GetSymbols( refs, SYMBOL_FILTER_ALL );

            for( int jj = 0; jj < (int) refs.GetCount(); jj++ )
            {
                SCH_REFERENCE& ref = refs[jj];

                if( ref.GetSymbol()->GetRef( &ref.GetSheetPath(), true ) == aCrossRef )
                {
                    refSymbol = ref.GetSymbol();
                    break;
                }
            }
        }

        if( refSymbol )
            refSymbol->GetContextualTextVars( aTokens );
    }
    else
    {
        if( schematic && schematic->CurrentSheet().Last() )
        {
            schematic->CurrentSheet().Last()->GetContextualTextVars( aTokens );
        }
        else
        {
            for( std::pair<wxString, wxString> entry : Prj().GetTextVars() )
                aTokens->push_back( entry.first );
        }
    }
}


bool DIALOG_TABLECELL_PROPERTIES::TransferDataToWindow()
{
    if( !wxDialog::TransferDataToWindow() )
        return false;

    bool              firstCell = true;
    GR_TEXT_H_ALIGN_T hAlign = GR_TEXT_H_ALIGN_INDETERMINATE;
    GR_TEXT_V_ALIGN_T vAlign = GR_TEXT_V_ALIGN_INDETERMINATE;

    for( SCH_TABLECELL* cell : m_cells )
    {
        wxString text = cell->GetText();

        m_cellText->SetValue( text );
        m_cellText->EmptyUndoBuffer();

        if( firstCell )
        {
            m_fontCtrl->SetFontSelection( cell->GetFont() );
            m_textSize.SetValue( cell->GetTextWidth() );

            m_bold->Set3StateValue( cell->IsBold() ? wxCHK_CHECKED : wxCHK_UNCHECKED );
            m_italic->Set3StateValue( cell->IsItalic() ? wxCHK_CHECKED : wxCHK_UNCHECKED );

            hAlign = cell->GetHorizJustify();
            vAlign = cell->GetVertJustify();

            m_textColorBook->SetSelection( 1 );
            m_textColorSwatch->SetSwatchColor( cell->GetTextColor(), false );
            m_textColorSwatch->SetDefaultColor( COLOR4D::UNSPECIFIED );

            m_fillColorBook->SetSelection( 1 );

            if( cell->IsSolidFill() )
                m_fillColorSwatch->SetSwatchColor( cell->GetFillColor(), false );
            else
                m_fillColorSwatch->SetSwatchColor( COLOR4D::UNSPECIFIED, false );

            m_fillColorSwatch->SetDefaultColor( COLOR4D::UNSPECIFIED );

            m_marginLeft.SetValue( cell->GetMarginLeft() );
            m_marginTop.SetValue( cell->GetMarginTop() );
            m_marginRight.SetValue( cell->GetMarginRight() );
            m_marginBottom.SetValue( cell->GetMarginBottom() );

            firstCell = false;
        }
        else
        {
            if( cell->GetFont() != m_fontCtrl->GetFontSelection( cell->IsBold(), cell->IsItalic() ) )
                m_fontCtrl->SetSelection( -1 );

            if( cell->GetTextWidth() != m_textSize.GetValue() )
                m_textSize.SetValue( INDETERMINATE_STATE );

            wxCheckBoxState bold = cell->IsBold() ? wxCHK_CHECKED : wxCHK_UNCHECKED;

            if( bold != m_bold->Get3StateValue() )
                m_bold->Set3StateValue( wxCHK_UNDETERMINED );

            wxCheckBoxState italic = cell->IsItalic() ? wxCHK_CHECKED : wxCHK_UNCHECKED;

            if( italic != m_italic->Get3StateValue() )
                m_italic->Set3StateValue( wxCHK_UNDETERMINED );

            if( cell->GetHorizJustify() != hAlign )
                hAlign = GR_TEXT_H_ALIGN_INDETERMINATE;

            if( cell->GetVertJustify() != vAlign )
                vAlign = GR_TEXT_V_ALIGN_INDETERMINATE;

            if( cell->GetTextColor() != m_textColorSwatch->GetSwatchColor() )
            {
                m_textColorBook->SetSelection( 0 );
                m_textColorPopup->SetSelection( 0 );
            }

            COLOR4D fillColor = cell->IsSolidFill() ? cell->GetFillColor() : COLOR4D::UNSPECIFIED;

            if( fillColor != m_fillColorSwatch->GetSwatchColor() )
            {
                m_fillColorBook->SetSelection( 0 );
                m_fillColorPopup->SetSelection( 0 );
            }

            if( fillColor != m_fillColorSwatch->GetSwatchColor() )
                fillColor = COLOR4D::UNSPECIFIED;

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
        case GR_TEXT_H_ALIGN_LEFT:          m_hAlignLeft->Check();   break;
        case GR_TEXT_H_ALIGN_CENTER:        m_hAlignCenter->Check(); break;
        case GR_TEXT_H_ALIGN_RIGHT:         m_hAlignRight->Check();  break;
        case GR_TEXT_H_ALIGN_INDETERMINATE:                          break;
        }

        switch( vAlign )
        {
        case GR_TEXT_V_ALIGN_TOP:           m_vAlignTop->Check();    break;
        case GR_TEXT_V_ALIGN_CENTER:        m_vAlignCenter->Check(); break;
        case GR_TEXT_V_ALIGN_BOTTOM:        m_vAlignBottom->Check(); break;
        case GR_TEXT_V_ALIGN_INDETERMINATE:                          break;
        }
    }

    return true;
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


void DIALOG_TABLECELL_PROPERTIES::onTextColorPopup( wxCommandEvent& aEvent )
{
    if( aEvent.GetSelection() == 1 )
    {
        m_textColorBook->SetSelection( 1 );
        m_textColorSwatch->GetNewSwatchColor();
    }
}


void DIALOG_TABLECELL_PROPERTIES::onFillColorPopup( wxCommandEvent& aEvent )
{
    if( aEvent.GetSelection() == 1 )
    {
        m_fillColorBook->SetSelection( 1 );
        m_fillColorSwatch->GetNewSwatchColor();
    }
}


bool DIALOG_TABLECELL_PROPERTIES::TransferDataFromWindow()
{
    if( !wxDialog::TransferDataFromWindow() )
        return false;

    SCH_COMMIT commit( m_frame );

    /* save table in undo list if not already in edit */
    if( m_table->GetEditFlags() == 0 )
        commit.Modify( m_table, m_frame->GetScreen() );

    for( SCH_TABLECELL* cell : m_cells )
    {
        wxString text = m_cellTextCtrl->GetValue();

#ifdef __WXMAC__
        // On macOS CTRL+Enter produces '\r' instead of '\n' regardless of EOL setting
        text.Replace( "\r", "\n" );
#elif defined( __WINDOWS__ )
        // On Windows, a new line is coded as \r\n.  We use only \n in KiCad files and in
        // drawing routines so strip the \r char.
        text.Replace( "\r", "" );
#endif

#ifdef __EMSCRIPTEN__
        if( !m_ccKeepText )
#endif
        cell->SetText( text );

        if( m_bold->Get3StateValue() == wxCHK_CHECKED )
            cell->SetBold( true );
        else if( m_bold->Get3StateValue() == wxCHK_UNCHECKED )
            cell->SetBold( false );

        if( m_italic->Get3StateValue() == wxCHK_CHECKED )
            cell->SetItalic( true );
        else if( m_italic->Get3StateValue() == wxCHK_UNCHECKED )
            cell->SetItalic( false );

        if( m_fontCtrl->HaveFontSelection() )
            cell->SetFont( m_fontCtrl->GetFontSelection( cell->IsBold(), cell->IsItalic() ) );

        if( !m_textSize.IsIndeterminate() )
            cell->SetTextSize( VECTOR2I( m_textSize.GetIntValue(), m_textSize.GetIntValue() ) );

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

        if( m_textColorBook->GetSelection() == 1 )
            cell->SetTextColor( m_textColorSwatch->GetSwatchColor() );

        if( m_fillColorBook->GetSelection() == 1 )
        {
            COLOR4D fillColor = m_fillColorSwatch->GetSwatchColor();

            if( fillColor == COLOR4D::UNSPECIFIED )
            {
                cell->SetFillMode( FILL_T::NO_FILL );
            }
            else
            {
                cell->SetFillMode( FILL_T::FILLED_WITH_COLOR );
                cell->SetFillColor( fillColor );
            }
        }

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
        commit.Push( _( "Edit Table Cell Properties" ) );

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

void DIALOG_TABLECELL_PROPERTIES::onMultiLineTCLostFocus( wxFocusEvent& event )
{
    if( m_scintillaTricks )
        m_scintillaTricks->CancelAutocomplete();

    event.Skip();
}


void DIALOG_TABLECELL_PROPERTIES::OnFormattingHelp( wxHyperlinkEvent& aEvent )
{
    if( m_helpWindow )
    {
        m_helpWindow->Raise();
        m_helpWindow->Show( true );
        return;
    }

    m_helpWindow = SCH_TEXT::ShowSyntaxHelp( this );
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

    for( SCH_TABLECELL* cell : m_cells )
        uuids.push_back( cell->m_Uuid.AsStdString() );

    auto threeState = []( wxCheckBox* aBox ) -> nlohmann::json
    {
        if( aBox->Get3StateValue() == wxCHK_UNDETERMINED )
            return nullptr;

        return aBox->Get3StateValue() == wxCHK_CHECKED;
    };

    nlohmann::json record;
    record["uuids"] = uuids;
    record["text"] = m_cells.size() == 1
                             ? nlohmann::json( m_cellText->GetValue().ToStdString( wxConvUTF8 ) )
                             : nlohmann::json();
    record["font"] = m_fontCtrl->GetSelection() < 0
                             ? nlohmann::json()
                             : nlohmann::json( m_fontCtrl->GetStringSelection().ToStdString(
                                       wxConvUTF8 ) );
    record["bold"] = threeState( m_bold );
    record["italic"] = threeState( m_italic );
    record["hAlign"] = ccCellAlign( m_hAlignLeft, m_hAlignCenter, m_hAlignRight, "left", "right" );
    record["vAlign"] = ccCellAlign( m_vAlignTop, m_vAlignCenter, m_vAlignBottom, "top", "bottom" );
    record["margins"] = { { "left", ccCellLength( m_marginLeft, scale ) },
                          { "right", ccCellLength( m_marginRight, scale ) },
                          { "top", ccCellLength( m_marginTop, scale ) },
                          { "bottom", ccCellLength( m_marginBottom, scale ) } };
    record["size"] = ccCellLength( m_textSize, scale );
    record["width"] = nullptr;
    record["height"] = nullptr;
    record["thickness"] = nullptr;
    record["autoThickness"] = nullptr;
    record["textColor"] = m_textColorBook->GetSelection() == 1
                                  ? nlohmann::json( ccTableColor(
                                            m_textColorSwatch->GetSwatchColor() ) )
                                  : nlohmann::json();

    if( m_fillColorBook->GetSelection() == 1 )
    {
        const KIGFX::COLOR4D fill = m_fillColorSwatch->GetSwatchColor();

        record["fill"] = { { "mode", fill == KIGFX::COLOR4D::UNSPECIFIED ? "none" : "color" },
                           { "color", ccTableColor( fill ) } };
    }
    else
    {
        record["fill"] = nullptr;
    }

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

            // The schematic's cells have no width, height or pen thickness of their own.
            if( !ccCellIn( aAnswer, m_scale, { "width", "height", "thickness", "autoThickness" },
                           in, aField ) )
            {
                return false;
            }

            std::optional<long long>      size;
            std::optional<KIGFX::COLOR4D> textColor;
            std::optional<KIGFX::COLOR4D> fill;

            if( aAnswer.contains( "size" ) && !aAnswer["size"].is_null() )
            {
                aField = "size";
                long long iu = 0;

                if( !ccTableLength( aAnswer["size"], m_scale, iu ) || iu <= 0 )
                    return false;

                size = iu;
            }

            if( aAnswer.contains( "textColor" ) && !aAnswer["textColor"].is_null() )
            {
                aField = "textColor";
                KIGFX::COLOR4D color;

                if( !ccTableColorOf( aAnswer["textColor"], color ) )
                    return false;

                textColor = color;
            }

            if( aAnswer.contains( "fill" ) && !aAnswer["fill"].is_null() )
            {
                aField = "fill";
                const nlohmann::json& value = aAnswer["fill"];

                if( !value.is_object() || !value.contains( "mode" )
                    || !value["mode"].is_string() )
                {
                    return false;
                }

                const std::string mode = value["mode"].get<std::string>();

                if( mode == "none" )
                {
                    fill = KIGFX::COLOR4D::UNSPECIFIED;
                }
                else if( mode == "color" )
                {
                    KIGFX::COLOR4D color;

                    if( !value.contains( "color" ) || !ccTableColorOf( value["color"], color )
                        || color == KIGFX::COLOR4D::UNSPECIFIED )
                    {
                        aField = "fill.color";
                        return false;
                    }

                    fill = color;
                }
                else
                {
                    aField = "fill.mode";
                    return false;
                }
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

            // KiCad's dialog writes the text it shows into every selected cell; with several
            // cells and no text given, each cell keeps its own.
            dlg->m_ccKeepText = !in.text && dlg->m_cells.size() > 1;

            if( in.bold )
                dlg->m_bold->Set3StateValue( *in.bold ? wxCHK_CHECKED : wxCHK_UNCHECKED );

            if( in.italic )
                dlg->m_italic->Set3StateValue( *in.italic ? wxCHK_CHECKED : wxCHK_UNCHECKED );

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

            if( size )
                dlg->m_textSize.SetValue( *size );

            if( textColor )
            {
                dlg->m_textColorBook->SetSelection( 1 );
                dlg->m_textColorSwatch->SetSwatchColor( *textColor, false );
            }

            if( fill )
            {
                dlg->m_fillColorBook->SetSelection( 1 );
                dlg->m_fillColorSwatch->SetSwatchColor( *fill, false );
            }

            if( !dlg->TransferDataFromWindow() )
                return false;

            // The dialog's own Edit Table button: KiCad then shows (asks) the table.
            if( in.editTable )
                dlg->m_returnValue = TABLECELL_PROPS_EDIT_TABLE;

            return true;
        }

    private:
        DIALOG_TABLECELL_PROPERTIES* m_dialog;
        const EDA_IU_SCALE&          m_scale;
    };

    RECEIVER receiver( this, scale );

    return CcHostPromptModal( "tableCell", record, receiver );
}
#endif // __EMSCRIPTEN__
