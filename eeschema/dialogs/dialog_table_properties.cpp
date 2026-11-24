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

#include <kiplatform/ui.h>
#include <sch_edit_frame.h>
#include <widgets/color_swatch.h>
#include <widgets/grid_color_swatch_helpers.h>
#include <widgets/wx_grid.h>
#include <widgets/grid_text_helpers.h>
#include <grid_tricks.h>
#include <settings/color_settings.h>
#include <widgets/wx_infobar.h>
#include <sch_table.h>
#include <sch_commit.h>
#include <tool/tool_manager.h>
#include <dialog_table_properties.h>

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


DIALOG_TABLE_PROPERTIES::DIALOG_TABLE_PROPERTIES( SCH_EDIT_FRAME* aFrame, SCH_TABLE* aTable ) :
        DIALOG_TABLE_PROPERTIES_BASE( aFrame ),
        m_frame( aFrame ),
        m_table( aTable ),
        m_borderWidth( aFrame, m_borderWidthLabel, m_borderWidthCtrl, m_borderWidthUnits ),
        m_separatorsWidth( aFrame, m_separatorsWidthLabel, m_separatorsWidthCtrl,
                           m_separatorsWidthUnits )
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
            const SCH_TABLECELL* cell = m_table->GetCell( row, col );
            wxGridCellAttr*      attr = new wxGridCellAttr;

            if( cell->GetColSpan() == 0 || cell->GetRowSpan() == 0 )
            {
                attr->SetRenderer( new GRID_CELL_COLOR_RENDERER( this ) );
                attr->SetReadOnly();
            }
            else
            {
                attr->SetEditor( new GRID_CELL_STC_EDITOR( true, false,
                        // onCharFn
                        [this]( wxStyledTextEvent& aEvent, SCINTILLA_TRICKS* aScintillaTricks )
                        {
                            aScintillaTricks->DoTextVarAutocomplete(
                                    // getTokensFn
                                    [this]( const wxString& xRef, wxArrayString* tokens )
                                    {
                                        getContextualTextVars( xRef, tokens );
                                    } );
                        } ) );
            }

            m_grid->SetAttr( row, col, attr );
        }
    }

    for( const auto& [lineStyle, lineStyleDesc] : lineTypeNames )
    {
        m_borderStyleCombo->Append( lineStyleDesc.name, KiBitmap( lineStyleDesc.bitmap ) );
        m_separatorsStyleCombo->Append( lineStyleDesc.name, KiBitmap( lineStyleDesc.bitmap ) );
    }

    KIGFX::COLOR4D canvas = aFrame->GetColorSettings()->GetColor( LAYER_SCHEMATIC_BACKGROUND );
    m_borderColorSwatch->SetSwatchBackground( canvas );
    m_separatorsColorSwatch->SetSwatchBackground( canvas );

    if( m_frame->GetColorSettings()->GetOverrideSchItemColors() )
        m_infoBar->ShowMessage( _( "Note: individual item colors overridden in Preferences." ) );

    SetupStandardButtons();
    Layout();

    // Now all widgets have the size fixed, call FinishDialogSettings
    finishDialogSettings();
}


DIALOG_TABLE_PROPERTIES::~DIALOG_TABLE_PROPERTIES()
{
    // Delete the GRID_TRICKS.
    m_grid->PopEventHandler( true );
}


bool DIALOG_TABLE_PROPERTIES::TransferDataToWindow()
{
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
            SCH_TABLECELL* tableCell = m_table->GetCell( row, col );

            if( tableCell->GetColSpan() == 0 || tableCell->GetRowSpan() == 0 )
            {
                m_grid->SetCellValue( row, col, coveredColor.GetAsString() );
                continue;
            }

            wxString text = tableCell->GetText();

            // show text variable cross-references in a human-readable format
            if( SCHEMATIC* schematic = tableCell->Schematic() )
                text = schematic->ConvertKIIDsToRefs( text );

            m_grid->SetCellValue( row, col, text );
        }
    }

    CallAfter( [this]()
               {
                   for( int row = 0; row < m_table->GetRowCount(); ++row )
                   {
                       for( int col = 0; col < m_table->GetColCount(); ++col )
                       {
                           SCH_TABLECELL* tableCell = m_table->GetCell( row, col );

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

    m_borderCheckbox->SetValue( m_table->StrokeExternal() );
    m_headerBorder->SetValue( m_table->StrokeHeaderSeparator() );

    if( m_table->GetBorderStroke().GetWidth() >= 0 )
        m_borderWidth.SetValue( m_table->GetBorderStroke().GetWidth() );

    m_borderColorSwatch->SetSwatchColor( m_table->GetBorderStroke().GetColor(), false );

    int style = static_cast<int>( m_table->GetBorderStroke().GetLineStyle() );

    if( style >= 0 && style < (int) lineTypeNames.size() )
        m_borderStyleCombo->SetSelection( style );
    else
        m_borderStyleCombo->SetSelection( 0 );

    m_borderWidth.Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );
    m_borderColorLabel->Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );
    m_borderColorSwatch->Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );
    m_borderStyleLabel->Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );
    m_borderStyleCombo->Enable( m_table->StrokeExternal() || m_table->StrokeHeaderSeparator() );

    bool rows = m_table->StrokeRows() && m_table->GetSeparatorsStroke().GetWidth() >= 0;
    bool cols = m_table->StrokeColumns() && m_table->GetSeparatorsStroke().GetWidth() >= 0;

    m_rowSeparators->SetValue( rows );
    m_colSeparators->SetValue( cols );

    if( m_table->GetSeparatorsStroke().GetWidth() >= 0 )
        m_separatorsWidth.SetValue( m_table->GetSeparatorsStroke().GetWidth() );

    m_separatorsColorSwatch->SetSwatchColor( m_table->GetSeparatorsStroke().GetColor(), false );

    style = static_cast<int>( m_table->GetSeparatorsStroke().GetLineStyle() );

    if( style >= 0 && style < (int) lineTypeNames.size() )
        m_separatorsStyleCombo->SetSelection( style );
    else
        m_separatorsStyleCombo->SetSelection( 0 );

    m_separatorsWidth.Enable( rows || cols );
    m_separatorsColorLabel->Enable( rows || cols );
    m_separatorsColorSwatch->Enable( rows || cols );
    m_separatorsStyleLabel->Enable( rows || cols );
    m_separatorsStyleCombo->Enable( rows || cols );

    return true;
}


void DIALOG_TABLE_PROPERTIES::getContextualTextVars( const wxString& aCrossRef,
                                                     wxArrayString*  aTokens )
{
    if( !aCrossRef.IsEmpty() )
    {
        SCH_REFERENCE_LIST refs;
        SCH_SYMBOL*        refSymbol = nullptr;

        m_frame->Schematic().Hierarchy().GetSymbols( refs, SYMBOL_FILTER_ALL );

        for( int jj = 0; jj < (int) refs.GetCount(); jj++ )
        {
            SCH_REFERENCE& ref = refs[jj];

            if( ref.GetSymbol()->GetRef( &ref.GetSheetPath(), true ) == aCrossRef )
            {
                refSymbol = ref.GetSymbol();
                break;
            }
        }

        if( refSymbol )
            refSymbol->GetContextualTextVars( aTokens );
    }
    else
    {
        SCHEMATIC* schematic = m_table->Schematic();

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


void DIALOG_TABLE_PROPERTIES::onBorderChecked( wxCommandEvent& aEvent )
{
    bool border = m_borderCheckbox->GetValue();
    bool headerSeparator = m_headerBorder->GetValue();

    if( ( border || headerSeparator ) && m_borderWidth.GetValue() < 0 )
        m_borderWidth.SetValue( m_frame->eeconfig()->m_Drawing.default_line_thickness );

    m_borderWidth.Enable( border || headerSeparator );
    m_borderColorLabel->Enable( border || headerSeparator );
    m_borderColorSwatch->Enable( border || headerSeparator );
    m_borderStyleLabel->Enable( border || headerSeparator );
    m_borderStyleCombo->Enable( border || headerSeparator );

    bool row = m_rowSeparators->GetValue();
    bool col = m_colSeparators->GetValue();

    if( ( row || col ) && m_separatorsWidth.GetValue() < 0 )
        m_separatorsWidth.SetValue( m_frame->eeconfig()->m_Drawing.default_line_thickness );

    m_separatorsWidth.Enable( row || col );
    m_separatorsColorLabel->Enable( row || col );
    m_separatorsColorSwatch->Enable( row || col );
    m_separatorsStyleLabel->Enable( row || col );
    m_separatorsStyleCombo->Enable( row || col );
}


bool DIALOG_TABLE_PROPERTIES::TransferDataFromWindow()
{
    if( !m_grid->CommitPendingChanges() )
        return false;

    if( !wxDialog::TransferDataFromWindow() )
        return false;

    SCH_COMMIT commit( m_frame );

    /* save table in undo list if not already in edit */
    if( m_table->GetEditFlags() == 0 )
        commit.Modify( m_table, m_frame->GetScreen() );

    for( int row = 0; row < m_table->GetRowCount(); ++row )
    {
        for( int col = 0; col < m_table->GetColCount(); ++col )
        {
            SCH_TABLECELL* tableCell = m_table->GetCell( row, col );
            wxString       txt = m_grid->GetCellValue( row, col );

            // convert any text variable cross-references to their UUIDs
            if( SCHEMATIC* schematic = tableCell->Schematic() )
                txt = schematic->ConvertRefsToKIIDs( txt );

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
        }
    }

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

        stroke.SetColor( m_borderColorSwatch->GetSwatchColor() );

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

        stroke.SetColor( m_separatorsColorSwatch->GetSwatchColor() );

        m_table->SetSeparatorsStroke( stroke );
    }

    if( !commit.Empty() )
        commit.Push( _( "Edit Table" ) );

    return true;
}


void DIALOG_TABLE_PROPERTIES::sizeGridToTable()
{
    Layout();   // Make sure we get the current client size for the grid

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


int DIALOG_TABLE_PROPERTIES::ccAskHost()
{
    if( !TransferDataToWindow() )
        return -1;

    const EDA_IU_SCALE& scale = m_frame->GetIuScale();
    const int           rows = m_table->GetRowCount();
    const int           cols = m_table->GetColCount();

    auto covered = [this]( int aRow, int aCol )
    {
        const SCH_TABLECELL* cell = m_table->GetCell( aRow, aCol );
        return cell->GetColSpan() == 0 || cell->GetRowSpan() == 0;
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
                         { "color", ccTableColor( m_borderColorSwatch->GetSwatchColor() ) },
                         { "style", ccTableStyle( m_borderStyleCombo->GetSelection() ) } };
    record["separators"] = { { "rows", m_rowSeparators->GetValue() },
                             { "cols", m_colSeparators->GetValue() },
                             { "width", widthOf( m_table->GetSeparatorsStroke() ) },
                             { "color", ccTableColor( m_separatorsColorSwatch->GetSwatchColor() ) },
                             { "style", ccTableStyle( m_separatorsStyleCombo->GetSelection() ) } };
    record["layer"] = "";
    record["locked"] = false;

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
            SCH_TABLE*               table = dlg->m_table;
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

            if( !ccTableStrokeIn( aAnswer, "border", "external", "header", true, m_scale, border,
                                  aField )
                || !ccTableStrokeIn( aAnswer, "separators", "rows", "cols", true, m_scale,
                                     separators, aField ) )
            {
                return false;
            }

            // A schematic table has no layer and no lock: the record's "" and false are taken.
            if( aAnswer.contains( "layer" )
                && ( !aAnswer["layer"].is_string()
                     || !aAnswer["layer"].get<std::string>().empty() ) )
            {
                aField = "layer";
                return false;
            }

            if( aAnswer.contains( "locked" )
                && ( !aAnswer["locked"].is_boolean() || aAnswer["locked"].get<bool>() ) )
            {
                aField = "locked";
                return false;
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
                        const SCH_TABLECELL*  cell = table->GetCell( row, col );

                        if( text.is_null() || cell->GetColSpan() == 0 || cell->GetRowSpan() == 0 )
                            continue;

                        dlg->m_grid->SetCellValue( row, col,
                                                   wxString::FromUTF8( text.get<std::string>() ) );
                    }
                }
            }

            if( border.first >= 0 )
                dlg->m_borderCheckbox->SetValue( border.first == 1 );

            if( border.second >= 0 )
                dlg->m_headerBorder->SetValue( border.second == 1 );

            if( separators.first >= 0 )
                dlg->m_rowSeparators->SetValue( separators.first == 1 );

            if( separators.second >= 0 )
                dlg->m_colSeparators->SetValue( separators.second == 1 );

            // The dialog's own handler: a stroke switched on with no width takes the default.
            wxCommandEvent dummy;
            dlg->onBorderChecked( dummy );

            if( border.hasWidth )
                dlg->m_borderWidth.SetValue( border.width );

            if( border.hasColor )
                dlg->m_borderColorSwatch->SetSwatchColor( border.color, false );

            if( border.style >= 0 )
                dlg->m_borderStyleCombo->SetSelection( border.style );

            if( separators.hasWidth )
                dlg->m_separatorsWidth.SetValue( separators.width );

            if( separators.hasColor )
                dlg->m_separatorsColorSwatch->SetSwatchColor( separators.color, false );

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
