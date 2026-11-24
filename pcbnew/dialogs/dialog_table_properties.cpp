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
 * Modified by Circuit Center Inc on 2026-11-18: DIALOG_TABLE_PROPERTIES asks the in-browser
 * editor's host to answer it (host_prompt.h, ask table) before KiCad's own dialog shows
 * (WebAssembly build only).
 *
 * Modified by Circuit Center Inc on 2026-11-23: the host's answer is checked in
 * kicadPromptAnswer and the table's commit runs after the parked wait resumes, on the tool's
 * coroutine (a commit with the table selected dispatches an event that cannot run inside the
 * plain export; WebAssembly build only).
 */

#include "dialog_table_properties.h"

#include <wx/hyperlink.h>

#include <kiplatform/ui.h>
#include <widgets/font_choice.h>
#include <widgets/color_swatch.h>
#include <widgets/wx_grid.h>
#include <dialogs/html_message_box.h>
#include <widgets/grid_text_helpers.h>
#include <widgets/grid_color_swatch_helpers.h>
#include <grid_tricks.h>
#include <scintilla_tricks.h>
#include <string_utils.h>
#include <confirm.h>
#include <board.h>
#include <board_commit.h>
#include <board_design_settings.h>
#include <footprint.h>
#include <pcb_textbox.h>
#include <pcb_tablecell.h>
#include <pcb_table.h>
#include <project.h>
#include <pcb_edit_frame.h>
#include <pcb_layer_box_selector.h>
#include <tool/tool_manager.h>
#include <tools/pcb_actions.h>

#ifdef __EMSCRIPTEN__
#include <cmath>
#include <limits>
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

/// One stroke group of the table answer (border or separators), read whole before anything
/// is written; a member left at its default keeps the dialog's value.
struct CC_TABLE_STROKE_IN
{
    int            first = -1;      ///< the first switch (external or rows), -1 kept
    int            second = -1;     ///< the second switch (header or cols), -1 kept
    bool           hasWidth = false;
    long long      width = 0;
    bool           hasColor = false;
    KIGFX::COLOR4D color;
    int            style = -1;      ///< a style combo selection, -1 kept
};


[[maybe_unused]] static bool ccTableStrokeIn( const nlohmann::json& aAnswer, const char* aKey,
                                              const char* aFirst, const char* aSecond,
                                              bool aColors, const EDA_IU_SCALE& aScale,
                                              CC_TABLE_STROKE_IN& aIn, std::string& aField )
{
    if( !aAnswer.contains( aKey ) )
        return true;

    const nlohmann::json& value = aAnswer[aKey];

    aField = aKey;

    if( !value.is_object() )
        return false;

    auto at = [&]( const char* aName )
    {
        aField = std::string( aKey ) + "." + aName;
    };

    if( value.contains( aFirst ) )
    {
        at( aFirst );

        if( !value[aFirst].is_boolean() )
            return false;

        aIn.first = value[aFirst].get<bool>() ? 1 : 0;
    }

    if( value.contains( aSecond ) )
    {
        at( aSecond );

        if( !value[aSecond].is_boolean() )
            return false;

        aIn.second = value[aSecond].get<bool>() ? 1 : 0;
    }

    if( value.contains( "width" ) && !value["width"].is_null() )
    {
        at( "width" );

        if( !ccTableLength( value["width"], aScale, aIn.width ) || aIn.width < 0 )
            return false;

        aIn.hasWidth = true;
    }

    if( value.contains( "color" ) )
    {
        at( "color" );

        if( aColors )
        {
            if( !ccTableColorOf( value["color"], aIn.color ) )
                return false;

            aIn.hasColor = true;
        }
        else if( !value["color"].is_string() || !value["color"].get<std::string>().empty() )
        {
            // The board's table has no stroke colors: only the record's "" is taken.
            return false;
        }
    }

    if( value.contains( "style" ) )
    {
        at( "style" );
        aIn.style = ccTableStyleSelection( value["style"] );

        if( aIn.style < 0 )
            return false;
    }

    aField.clear();
    return true;
}
#endif // __EMSCRIPTEN__


DIALOG_TABLE_PROPERTIES::DIALOG_TABLE_PROPERTIES( PCB_BASE_EDIT_FRAME* aFrame, PCB_TABLE* aTable ) :
        DIALOG_TABLE_PROPERTIES_BASE( aFrame ),
        m_frame( aFrame ),
        m_table( aTable ),
        m_borderWidth( aFrame, m_borderWidthLabel, m_borderWidthCtrl, m_borderWidthUnits ),
        m_separatorsWidth( aFrame, m_separatorsWidthLabel, m_separatorsWidthCtrl, m_separatorsWidthUnits )
{
    m_grid = new WX_GRID( this, wxID_ANY, wxDefaultPosition, wxDefaultSize, 0 );

    m_grid->CreateGrid( m_table->GetRowCount(), m_table->GetColCount() );
    m_grid->EnableEditing( true );
    m_grid->EnableGridLines( true );
    m_grid->EnableDragGridSize( false );
    m_grid->SetMargins( 0, 0 );
    m_grid->SetCellHighlightROPenWidth( 0 );

    m_grid->EnableDragColMove( false );
    m_grid->EnableDragColSize( false );
    m_grid->SetColLabelSize( 0 );
    m_grid->EnableDragRowMove( false );
    m_grid->EnableDragRowSize( false );
    m_grid->SetRowLabelSize( 0 );
    m_grid->SetDefaultCellAlignment( wxALIGN_LEFT, wxALIGN_TOP );

    m_gridSizer->Add( m_grid, 1, wxEXPAND, 5 );
    m_grid->PushEventHandler( new GRID_TRICKS( m_grid ) );

    for( int row = 0; row < m_table->GetRowCount(); ++row )
    {
        for( int col = 0; col < m_table->GetColCount(); ++col )
        {
            PCB_TABLECELL*  cell = m_table->GetCell( row, col );
            wxGridCellAttr* attr = new wxGridCellAttr;

            if( cell->GetColSpan() == 0 || cell->GetRowSpan() == 0 )
            {
                attr->SetRenderer( new GRID_CELL_COLOR_RENDERER( this ) );
                attr->SetReadOnly();
            }
            else
            {
                attr->SetEditor( new GRID_CELL_STC_EDITOR(
                        true, false,
                        [this, cell]( wxStyledTextEvent& aEvent, SCINTILLA_TRICKS* aScintillaTricks )
                        {
                            aScintillaTricks->DoTextVarAutocomplete(
                                    // getTokensFn
                                    [this, cell]( const wxString& xRef, wxArrayString* tokens )
                                    {
                                        m_frame->GetContextualTextVars( cell, xRef, tokens );
                                    } );
                        } ) );
            }

            m_grid->SetAttr( row, col, attr );
        }
    }

    if( m_table->GetParentFootprint() )
    {
        // Do not allow locking items in the footprint editor
        m_cbLocked->Show( false );
    }

    // Configure the layers list selector.  Note that footprints are built outside the current
    // board and so we may need to show all layers if the text is on an unactivated layer.
    if( !m_frame->GetBoard()->IsLayerEnabled( m_table->GetLayer() ) )
        m_LayerSelectionCtrl->ShowNonActivatedLayers( true );

    m_LayerSelectionCtrl->SetLayersHotkeys( false );
    m_LayerSelectionCtrl->SetBoardFrame( m_frame );
    m_LayerSelectionCtrl->Resync();

    for( const auto& [lineStyle, lineStyleDesc] : lineTypeNames )
    {
        m_borderStyleCombo->Append( lineStyleDesc.name, KiBitmap( lineStyleDesc.bitmap ) );
        m_separatorsStyleCombo->Append( lineStyleDesc.name, KiBitmap( lineStyleDesc.bitmap ) );
    }

    SetupStandardButtons();
    Layout();

    // Add syntax help hyperlink
    m_syntaxHelp = new wxHyperlinkCtrl( this, wxID_ANY, _( "Syntax help" ), wxEmptyString, wxDefaultPosition,
                                        wxDefaultSize, wxHL_DEFAULT_STYLE );
    m_syntaxHelp->SetToolTip( _( "Show syntax help window" ) );
    m_gridSizer->Add( m_syntaxHelp, 0, wxTOP | wxBOTTOM | wxRIGHT | wxLEFT, 3 );

    m_syntaxHelp->Bind( wxEVT_HYPERLINK, &DIALOG_TABLE_PROPERTIES::onSyntaxHelp, this );

    m_helpWindow = nullptr;

    // Now all widgets have the size fixed, call FinishDialogSettings
    finishDialogSettings();
}


DIALOG_TABLE_PROPERTIES::~DIALOG_TABLE_PROPERTIES()
{
    // Delete the GRID_TRICKS.
    m_grid->PopEventHandler( true );

    if( m_helpWindow )
        m_helpWindow->Destroy();
}


bool DIALOG_TABLE_PROPERTIES::TransferDataToWindow()
{
    BOARD* board = m_frame->GetBoard();

    if( !wxDialog::TransferDataToWindow() )
        return false;

    //
    // Cell Contents
    //

    wxColour coveredColor = wxSystemSettings::GetColour( wxSYS_COLOUR_BTNFACE );

    if( KIPLATFORM::UI::IsDarkTheme() )
        coveredColor = coveredColor.ChangeLightness( 140 );
    else
        coveredColor = coveredColor.ChangeLightness( 100 );

    for( int row = 0; row < m_table->GetRowCount(); ++row )
    {
        for( int col = 0; col < m_table->GetColCount(); ++col )
        {
            PCB_TABLECELL* tableCell;

            if( IsBackLayer( m_table->GetLayer() ) )
                tableCell = m_table->GetCell( row, m_table->GetColCount() - 1 - col );
            else
                tableCell = m_table->GetCell( row, col );

            if( tableCell->GetColSpan() == 0 || tableCell->GetRowSpan() == 0 )
            {
                m_grid->SetCellValue( row, col, coveredColor.GetAsString() );
                continue;
            }

            wxString text = tableCell->GetText();

            // show text variable cross-references in a human-readable format
            text = board->ConvertKIIDsToCrossReferences( text );

            m_grid->SetCellValue( row, col, text );
        }
    }

    CallAfter(
            [this]()
            {
                for( int row = 0; row < m_table->GetRowCount(); ++row )
                {
                    for( int col = 0; col < m_table->GetColCount(); ++col )
                    {
                        PCB_TABLECELL* tableCell = m_table->GetCell( row, col );

                        if( tableCell->IsSelected() )
                        {
                            m_grid->SetGridCursor( row, col );
                            m_grid->EnableCellEditControl();
                            m_grid->ShowCellEditControl();
                            return;
                        }
                    }
                }
            } );

    sizeGridToTable();

    //
    // Table Properties
    //

    m_LayerSelectionCtrl->SetLayerSelection( m_table->GetLayer() );
    m_cbLocked->SetValue( m_table->IsLocked() );

    m_borderCheckbox->SetValue( m_table->StrokeExternal() );
    m_headerBorder->SetValue( m_table->StrokeHeaderSeparator() );

    if( m_table->GetBorderStroke().GetWidth() >= 0 )
        m_borderWidth.SetValue( m_table->GetBorderStroke().GetWidth() );

    int style = static_cast<int>( m_table->GetBorderStroke().GetLineStyle() );

    if( style >= 0 && style < (int) lineTypeNames.size() )
        m_borderStyleCombo->SetSelection( style );
    else
        m_borderStyleCombo->SetSelection( 0 );

    m_borderWidth.Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );
    m_borderStyleLabel->Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );
    m_borderStyleCombo->Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );

    bool rows = m_table->StrokeRows() && m_table->GetSeparatorsStroke().GetWidth() >= 0;
    bool cols = m_table->StrokeColumns() && m_table->GetSeparatorsStroke().GetWidth() >= 0;

    m_rowSeparators->SetValue( rows );
    m_colSeparators->SetValue( cols );

    if( m_table->GetSeparatorsStroke().GetWidth() >= 0 )
        m_separatorsWidth.SetValue( m_table->GetSeparatorsStroke().GetWidth() );

    style = static_cast<int>( m_table->GetSeparatorsStroke().GetLineStyle() );

    if( style >= 0 && style < (int) lineTypeNames.size() )
        m_separatorsStyleCombo->SetSelection( style );
    else
        m_separatorsStyleCombo->SetSelection( 0 );

    m_separatorsWidth.Enable( rows || cols );
    m_separatorsStyleLabel->Enable( rows || cols );
    m_separatorsStyleCombo->Enable( rows || cols );

    return true;
}

void DIALOG_TABLE_PROPERTIES::onHeaderChecked( wxCommandEvent& aEvent )
{
    BOARD_DESIGN_SETTINGS& bds = m_frame->GetDesignSettings();
    PCB_LAYER_ID           currentLayer = ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() );
    int                    defaultLineThickness = bds.GetLineThickness( currentLayer );

    bool border = m_borderCheckbox->GetValue();
    bool header = m_headerBorder->GetValue();

    if( ( border || header ) && m_borderWidth.GetValue() < 0 )
        m_borderWidth.SetValue( defaultLineThickness );

    m_borderWidth.Enable( border || header );
    m_borderStyleLabel->Enable( border || header );
    m_borderStyleCombo->Enable( border || header );

    bool row = m_rowSeparators->GetValue();
    bool col = m_colSeparators->GetValue();

    if( ( row || col ) && m_separatorsWidth.GetValue() < 0 )
        m_separatorsWidth.SetValue( defaultLineThickness );

    m_separatorsWidth.Enable( row || col );
    m_separatorsStyleLabel->Enable( row || col );
    m_separatorsStyleCombo->Enable( row || col );
}

void DIALOG_TABLE_PROPERTIES::onBorderChecked( wxCommandEvent& aEvent )
{
    BOARD_DESIGN_SETTINGS& bds = m_frame->GetDesignSettings();
    PCB_LAYER_ID           currentLayer = ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() );
    int                    defaultLineThickness = bds.GetLineThickness( currentLayer );

    bool border = m_borderCheckbox->GetValue();
    bool header = m_headerBorder->GetValue();

    if( ( border || header ) && m_borderWidth.GetValue() < 0 )
        m_borderWidth.SetValue( defaultLineThickness );

    m_borderWidth.Enable( border || header );
    m_borderStyleLabel->Enable( border || header );
    m_borderStyleCombo->Enable( border || header );

    bool row = m_rowSeparators->GetValue();
    bool col = m_colSeparators->GetValue();

    if( ( row || col ) && m_separatorsWidth.GetValue() < 0 )
        m_separatorsWidth.SetValue( defaultLineThickness );

    m_separatorsWidth.Enable( row || col );
    m_separatorsStyleLabel->Enable( row || col );
    m_separatorsStyleCombo->Enable( row || col );
}


bool DIALOG_TABLE_PROPERTIES::TransferDataFromWindow()
{
    if( !m_grid->CommitPendingChanges() )
        return false;

    if( !wxDialog::TransferDataFromWindow() )
        return false;

    BOARD*       board = m_frame->GetBoard();
    BOARD_COMMIT commit( m_frame );
    commit.Modify( m_table );

    // If no other command in progress, prepare undo command
    // (for a command in progress, will be made later, at the completion of command)
    bool pushCommit = ( m_table->GetEditFlags() == 0 );

    // Set IN_EDIT flag to force undo/redo/abort proper operation and avoid new calls to
    // SaveCopyInUndoList for the same text if is moved, and then rotated, edited, etc....
    if( !pushCommit )
        m_table->SetFlags( IN_EDIT );

    for( int row = 0; row < m_table->GetRowCount(); ++row )
    {
        for( int col = 0; col < m_table->GetColCount(); ++col )
        {
            PCB_TABLECELL* tableCell;

            if( IsBackLayer( m_table->GetLayer() ) )
                tableCell = m_table->GetCell( row, m_table->GetColCount() - 1 - col );
            else
                tableCell = m_table->GetCell( row, col );

            wxString txt = m_grid->GetCellValue( row, col );

            // Don't insert grey colour value back in to table cell
            if( tableCell->GetColSpan() == 0 || tableCell->GetRowSpan() == 0 )
                txt = wxEmptyString;

            // convert any text variable cross-references to their UUIDs
            txt = board->ConvertCrossReferencesToKIIDs( txt );

#ifdef __WXMAC__
            // On macOS CTRL+Enter produces '\r' instead of '\n' regardless of EOL setting.
            // Replace it now.
            txt.Replace( "\r", "\n" );
#elif defined( __WINDOWS__ )
            // On Windows, a new line is coded as \r\n.  We use only \n in kicad files and in
            // drawing routines so strip the \r char.
            txt.Replace( "\r", "" );
#endif

            tableCell->SetText( txt );
            tableCell->SetLayer( ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() ) );
        }
    }

    m_table->SetLayer( ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() ) );
    m_table->SetLocked( m_cbLocked->GetValue() );

    m_table->SetStrokeExternal( m_borderCheckbox->GetValue() );
    m_table->SetStrokeHeaderSeparator( m_headerBorder->GetValue() );
    {
        STROKE_PARAMS stroke = m_table->GetBorderStroke();

        if( m_borderCheckbox->GetValue() || m_headerBorder->GetValue() )
            stroke.SetWidth( std::max( 0, m_borderWidth.GetIntValue() ) );
        else
            stroke.SetWidth( -1 );

        auto it = lineTypeNames.begin();
        std::advance( it, m_borderStyleCombo->GetSelection() );

        if( it == lineTypeNames.end() )
            stroke.SetLineStyle( LINE_STYLE::SOLID );
        else
            stroke.SetLineStyle( it->first );

        m_table->SetBorderStroke( stroke );
    }

    m_table->SetStrokeRows( m_rowSeparators->GetValue() );
    m_table->SetStrokeColumns( m_colSeparators->GetValue() );
    {
        STROKE_PARAMS stroke = m_table->GetSeparatorsStroke();

        if( m_rowSeparators->GetValue() || m_colSeparators->GetValue() )
            stroke.SetWidth( std::max( 0, m_separatorsWidth.GetIntValue() ) );
        else
            stroke.SetWidth( -1 );

        auto it = lineTypeNames.begin();
        std::advance( it, m_separatorsStyleCombo->GetSelection() );

        if( it == lineTypeNames.end() )
            stroke.SetLineStyle( LINE_STYLE::SOLID );
        else
            stroke.SetLineStyle( it->first );

        m_table->SetSeparatorsStroke( stroke );
    }

    if( pushCommit )
        commit.Push( _( "Edit Table" ), SKIP_CONNECTIVITY );

    return true;
}


void DIALOG_TABLE_PROPERTIES::sizeGridToTable()
{
    Layout(); // Make sure we get the current client size for the grid

    wxSize availableGridSize = m_grid->GetClientSize();

    if( availableGridSize.x == 0 || availableGridSize.y == 0 )
        return;

    BOX2I  tableBBox = m_table->GetBoundingBox();
    double scalerX = static_cast<double>( availableGridSize.x ) / tableBBox.GetWidth();
    double scalerY = static_cast<double>( availableGridSize.y ) / tableBBox.GetHeight();

    for( int row = 0; row < m_table->GetRowCount(); ++row )
        m_grid->SetRowSize( row, std::floor( m_table->GetRowHeight( row ) * scalerY ) );

    for( int col = 0; col < m_table->GetColCount(); ++col )
        m_grid->SetColSize( col, std::floor( m_table->GetColWidth( col ) * scalerX ) );
}


void DIALOG_TABLE_PROPERTIES::onSize( wxSizeEvent& aEvent )
{
    if( m_table )
        sizeGridToTable();

    aEvent.Skip();
}


void DIALOG_TABLE_PROPERTIES::onSyntaxHelp( wxHyperlinkEvent& aEvent )
{
    m_helpWindow = PCB_TEXT::ShowSyntaxHelp( this );
}


#ifdef __EMSCRIPTEN__
int DIALOG_TABLE_PROPERTIES::ShowModal()
{
    switch( ccAskHost() )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_TABLE_PROPERTIES_BASE::ShowModal();
    }
}


int DIALOG_TABLE_PROPERTIES::ShowQuasiModal()
{
    switch( ccAskHost() )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_TABLE_PROPERTIES_BASE::ShowQuasiModal();
    }
}


bool DIALOG_TABLE_PROPERTIES::ccCoveredCell( int aRow, int aCol ) const
{
    // The grid shows a back layer table mirrored, as TransferDataToWindow fills it.
    const PCB_TABLECELL* cell = IsBackLayer( m_table->GetLayer() )
                                        ? m_table->GetCell( aRow, m_table->GetColCount() - 1 - aCol )
                                        : m_table->GetCell( aRow, aCol );

    return cell->GetColSpan() == 0 || cell->GetRowSpan() == 0;
}


int DIALOG_TABLE_PROPERTIES::ccAskHost()
{
    if( !TransferDataToWindow() )
        return -1;

    const EDA_IU_SCALE& scale = m_frame->GetIuScale();
    const int           rows = m_table->GetRowCount();
    const int           cols = m_table->GetColCount();

    auto covered = [this]( int aRow, int aCol )
    {
        return ccCoveredCell( aRow, aCol );
    };

    auto widthOf = [&]( const STROKE_PARAMS& aStroke ) -> nlohmann::json
    {
        if( aStroke.GetWidth() < 0 )
            return nullptr;

        return scale.IUTomm( aStroke.GetWidth() );
    };

    // The grid as TransferDataToWindow filled it, row by row; a covered cell is null.
    nlohmann::json cells = nlohmann::json::array();

    for( int row = 0; row < rows; ++row )
    {
        nlohmann::json line = nlohmann::json::array();

        for( int col = 0; col < cols; ++col )
        {
            if( covered( row, col ) )
                line.push_back( nullptr );
            else
                line.push_back( m_grid->GetCellValue( row, col ).ToStdString( wxConvUTF8 ) );
        }

        cells.push_back( line );
    }

    nlohmann::json record;
    record["uuid"] = m_table->m_Uuid.AsStdString();
    record["rows"] = rows;
    record["cols"] = cols;
    record["cells"] = cells;
    record["border"] = { { "external", m_borderCheckbox->GetValue() },
                         { "header", m_headerBorder->GetValue() },
                         { "width", widthOf( m_table->GetBorderStroke() ) },
                         { "color", "" },
                         { "style", ccTableStyle( m_borderStyleCombo->GetSelection() ) } };
    record["separators"] = { { "rows", m_rowSeparators->GetValue() },
                             { "cols", m_colSeparators->GetValue() },
                             { "width", widthOf( m_table->GetSeparatorsStroke() ) },
                             { "color", "" },
                             { "style", ccTableStyle( m_separatorsStyleCombo->GetSelection() ) } };
    record["layer"] = BOARD::GetStandardLayerName(
                              ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() ) )
                              .ToStdString( wxConvUTF8 );
    record["locked"] = m_cbLocked->GetValue();

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        RECEIVER( DIALOG_TABLE_PROPERTIES* aDialog, const EDA_IU_SCALE& aScale ) :
                m_dialog( aDialog ),
                m_scale( aScale )
        {
        }

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            DIALOG_TABLE_PROPERTIES* dlg = m_dialog;
            PCB_TABLE*               table = dlg->m_table;
            const int                rows = table->GetRowCount();
            const int                cols = table->GetColCount();

            aField.clear();

            if( !aAnswer.is_object() )
                return false;

            // Read the whole answer before anything is written.
            const nlohmann::json* cellsIn = nullptr;

            if( aAnswer.contains( "cells" ) )
            {
                aField = "cells";
                cellsIn = &aAnswer["cells"];

                if( !cellsIn->is_array() || (int) cellsIn->size() != rows )
                    return false;

                for( const nlohmann::json& line : *cellsIn )
                {
                    if( !line.is_array() || (int) line.size() != cols )
                        return false;

                    for( const nlohmann::json& text : line )
                    {
                        if( !text.is_string() && !text.is_null() )
                            return false;
                    }
                }

                aField.clear();
            }

            CC_TABLE_STROKE_IN border;
            CC_TABLE_STROKE_IN separators;

            if( !ccTableStrokeIn( aAnswer, "border", "external", "header", false, m_scale, border,
                                  aField )
                || !ccTableStrokeIn( aAnswer, "separators", "rows", "cols", false, m_scale,
                                     separators, aField ) )
            {
                return false;
            }

            // The layer by its canonical name, among the layers the dialog's selector offers.
            int layerIn = -1;

            if( aAnswer.contains( "layer" ) )
            {
                aField = "layer";

                if( !aAnswer["layer"].is_string() )
                    return false;

                const wxString name = wxString::FromUTF8( aAnswer["layer"].get<std::string>() );

                for( unsigned ii = 0; ii < dlg->m_LayerSelectionCtrl->GetCount(); ++ii )
                {
                    const int layer =
                            (int) (intptr_t) dlg->m_LayerSelectionCtrl->GetClientData( ii );

                    if( BOARD::GetStandardLayerName( ToLAYER_ID( layer ) ) == name )
                    {
                        layerIn = layer;
                        break;
                    }
                }

                if( layerIn < 0 )
                    return false;

                aField.clear();
            }

            // The lock switch is hidden in the footprint editor: there it cannot change.
            int lockedIn = -1;

            if( aAnswer.contains( "locked" ) )
            {
                aField = "locked";

                if( !aAnswer["locked"].is_boolean() )
                    return false;

                lockedIn = aAnswer["locked"].get<bool>() ? 1 : 0;

                if( !dlg->m_cbLocked->IsShown()
                    && ( lockedIn == 1 ) != dlg->m_cbLocked->GetValue() )
                {
                    return false;
                }

                aField.clear();
            }

            // Write into the dialog's own controls (a cell editor the dialog opened is closed
            // first so it cannot write its text back over the answer).
            dlg->m_grid->CommitPendingChanges( true );

            if( cellsIn )
            {
                for( int row = 0; row < rows; ++row )
                {
                    for( int col = 0; col < cols; ++col )
                    {
                        const nlohmann::json& text = ( *cellsIn )[row][col];

                        if( text.is_null() || dlg->ccCoveredCell( row, col ) )
                            continue;

                        dlg->m_grid->SetCellValue( row, col,
                                                   wxString::FromUTF8( text.get<std::string>() ) );
                    }
                }
            }

            if( layerIn >= 0 )
                dlg->m_LayerSelectionCtrl->SetLayerSelection( layerIn );

            if( lockedIn >= 0 )
                dlg->m_cbLocked->SetValue( lockedIn == 1 );

            if( border.first >= 0 )
                dlg->m_borderCheckbox->SetValue( border.first == 1 );

            if( border.second >= 0 )
                dlg->m_headerBorder->SetValue( border.second == 1 );

            if( separators.first >= 0 )
                dlg->m_rowSeparators->SetValue( separators.first == 1 );

            if( separators.second >= 0 )
                dlg->m_colSeparators->SetValue( separators.second == 1 );

            // The dialog's own handler: a stroke switched on with no width takes the layer's
            // default line width.
            wxCommandEvent dummy;
            dlg->onBorderChecked( dummy );

            if( border.hasWidth )
                dlg->m_borderWidth.SetValue( border.width );

            if( border.style >= 0 )
                dlg->m_borderStyleCombo->SetSelection( border.style );

            if( separators.hasWidth )
                dlg->m_separatorsWidth.SetValue( separators.width );

            if( separators.style >= 0 )
                dlg->m_separatorsStyleCombo->SetSelection( separators.style );

            // The checks TransferDataFromWindow begins with, its only refusals; its commit runs
            // in ccAskHost once the parked wait resumes (a commit here traps when the table is
            // selected: kicadPromptAnswer is a plain export and cannot park).
            if( !dlg->m_grid->CommitPendingChanges() || !dlg->wxDialog::TransferDataFromWindow() )
                return false;

            commitPending = true;
            return true;
        }

        bool commitPending = false;

    private:
        DIALOG_TABLE_PROPERTIES* m_dialog;
        const EDA_IU_SCALE&      m_scale;
    };

    RECEIVER  receiver( this, scale );
    const int answered = CcHostPromptModal( "table", record, receiver );

    // The commit, on the tool's coroutine where KiCad's ShowModal would have made it.
    if( answered == 1 && receiver.commitPending )
        return TransferDataFromWindow() ? 1 : 0;

    return answered;
}
#endif // __EMSCRIPTEN__
