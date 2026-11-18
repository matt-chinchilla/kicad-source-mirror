
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
 * Modified by Circuit Center Inc on 2026-11-18: under __EMSCRIPTEN__ ShowModal asks the host
 * (host_prompt.h, ask outset, modal) and writes its answer through the dialog's own controls and
 * TransferDataFromWindow; a plain build is unchanged.
 */

#include "dialogs/dialog_outset_items.h"

#include <board.h>
#include <board_design_settings.h>
#include <pcb_layer_box_selector.h>
#include <wx/msgdlg.h>
#include <confirm.h>

#ifdef __EMSCRIPTEN__
#include <cmath>
#include <limits>
#include <optional>
#include <lset.h>
#endif

/**
 * Some handy preset values for common outset distances.
 */
static const std::vector<int> s_outsetPresetValue
{
    // Outsetting a 0.1mm line to touch a 0.1mm line
    pcbIUScale.mmToIU( 0.1 ),
    // 0.12mm line to touch a 0.1mm line
    pcbIUScale.mmToIU( 0.11 ),
    // IPC dense courtyard
    pcbIUScale.mmToIU( 0.15 ),
    // IPC normal courtyard
    pcbIUScale.mmToIU( 0.25 ),
    // Keep 0.12mm silkscreen line 0.2mm from copper
    pcbIUScale.mmToIU( 0.26 ),
    // IPC connector courtyard
    pcbIUScale.mmToIU( 0.5 ),
    // Common router bits
    pcbIUScale.mmToIU( 1.0 ),
    pcbIUScale.mmToIU( 2.0 ),
};

// Ther user can also get the current board design settings widths
// with the "Layer Default" button.
static const std::vector<int> s_presetLineWidths
{
    // Courtyard
    pcbIUScale.mmToIU( 0.05 ),
    pcbIUScale.mmToIU( 0.1 ),
    // Silkscreen
    pcbIUScale.mmToIU( 0.12 ),
    pcbIUScale.mmToIU( 0.15 ),
    pcbIUScale.mmToIU( 0.2 ),
};

static const std::vector<int> s_presetGridRounding
{
    // 0.01 is a common IPC grid round-off value
    pcbIUScale.mmToIU( 0.01 ),
};

static int s_gridRoundValuePersist = s_presetGridRounding[0];

static std::vector<int> s_outsetRecentValues;
static std::vector<int> s_lineWidthRecentValues;
static std::vector<int> s_gridRoundingRecentValues;


DIALOG_OUTSET_ITEMS::DIALOG_OUTSET_ITEMS( PCB_BASE_FRAME& aParent, OUTSET_ROUTINE::PARAMETERS& aParams ) :
        DIALOG_OUTSET_ITEMS_BASE( &aParent ),
        m_parent( aParent ),
        m_params( aParams ),
        m_outset( &aParent, m_outsetLabel, m_outsetEntry, m_outsetUnit ),
        m_lineWidth( &aParent, m_lineWidthLabel, m_lineWidthEntry, m_lineWidthUnit ),
        m_roundingGrid( &aParent, m_gridRoundingLabel, m_gridRoundingEntry, m_gridRoundingUnit )
{
    m_LayerSelectionCtrl->ShowNonActivatedLayers( false );
    m_LayerSelectionCtrl->SetLayersHotkeys( false );
    m_LayerSelectionCtrl->SetBoardFrame( &aParent );
    m_LayerSelectionCtrl->Resync();

    const auto fillOptionList =
            [&]( UNIT_BINDER& aCombo, const std::vector<int>& aPresets, const std::vector<int>& aRecentPresets )
            {
                std::vector<long long int> optionList;
                optionList.reserve( aPresets.size() + aRecentPresets.size() );

                for( const int val : aPresets )
                    optionList.push_back( val );

                for( const int val : aRecentPresets )
                    optionList.push_back( val );

                // Sort the vector and remove duplicates
                std::sort( optionList.begin(), optionList.end() );
                optionList.erase( std::unique( optionList.begin(), optionList.end() ), optionList.end() );

                aCombo.SetOptionsList( optionList );
            };

    fillOptionList( m_outset, s_outsetPresetValue, s_outsetRecentValues );
    fillOptionList( m_lineWidth, s_presetLineWidths, s_lineWidthRecentValues );
    fillOptionList( m_roundingGrid, s_presetGridRounding, s_gridRoundingRecentValues );

    SetupStandardButtons();
    finishDialogSettings();
}


DIALOG_OUTSET_ITEMS::~DIALOG_OUTSET_ITEMS()
{
}


void DIALOG_OUTSET_ITEMS::OnLayerDefaultClick( wxCommandEvent& event )
{
    const BOARD_DESIGN_SETTINGS& settings = m_parent.GetBoard()->GetDesignSettings();

    const PCB_LAYER_ID selLayer = ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() );
    const int          defaultWidth = settings.GetLineThickness( selLayer );

    m_lineWidth.SetValue( defaultWidth );
}


void DIALOG_OUTSET_ITEMS::OnCopyLayersChecked( wxCommandEvent& event )
{
    m_LayerSelectionCtrl->Enable( !m_copyLayers->GetValue() );
}


void DIALOG_OUTSET_ITEMS::OnRoundToGridChecked( wxCommandEvent& event )
{
    m_gridRoundingEntry->Enable( m_roundToGrid->IsChecked() );
}


bool DIALOG_OUTSET_ITEMS::TransferDataToWindow()
{
    m_LayerSelectionCtrl->SetLayerSelection( m_params.layer );
    m_outset.SetValue( m_params.outsetDistance );
    m_roundCorners->SetValue( m_params.roundCorners );
    m_lineWidth.SetValue( m_params.lineWidth );

    m_roundToGrid->SetValue( m_params.gridRounding.has_value() );

    m_roundingGrid.SetValue( m_params.gridRounding.value_or( s_gridRoundValuePersist ) );

    m_copyLayers->SetValue( m_params.useSourceLayers );
    m_copyWidths->SetValue( m_params.useSourceWidths );

    m_gridRoundingEntry->Enable( m_roundToGrid->IsChecked() );
    m_LayerSelectionCtrl->Enable( !m_copyLayers->GetValue() );

    m_deleteSourceItems->SetValue( m_params.deleteSourceItems );

    return true;
}


bool DIALOG_OUTSET_ITEMS::TransferDataFromWindow()
{
    if( m_lineWidth.GetIntValue() <= 0 )
    {
        DisplayErrorMessage( this, _( "Line width must be a positive value." ) );
        return false;
    }

    m_params.layer = ToLAYER_ID( m_LayerSelectionCtrl->GetLayerSelection() );
    m_params.outsetDistance = m_outset.GetIntValue();
    m_params.roundCorners = m_roundCorners->GetValue();
    m_params.lineWidth = m_lineWidth.GetIntValue();

    m_params.useSourceLayers = m_copyLayers->GetValue();
    m_params.useSourceWidths = m_copyWidths->GetValue();

    if( m_roundToGrid->IsChecked() )
        m_params.gridRounding = m_roundingGrid.GetValue();
    else
        m_params.gridRounding = std::nullopt;

    s_gridRoundValuePersist = m_roundingGrid.GetIntValue();

    m_params.deleteSourceItems = m_deleteSourceItems->GetValue();

    // Keep the recent values list up to date
    const auto saveRecentValue =
            []( std::vector<int>& aRecentValues, int aValue )
            {
                const auto it = std::find( aRecentValues.begin(), aRecentValues.end(), aValue );

                // Already have it
                if( it != aRecentValues.end() )
                    return;

                aRecentValues.push_back( aValue );
            };

    saveRecentValue( s_outsetRecentValues, m_params.outsetDistance );
    saveRecentValue( s_lineWidthRecentValues, m_params.lineWidth );

    if( m_params.gridRounding )
        saveRecentValue( s_gridRoundingRecentValues, m_params.gridRounding.value() );

    return true;
}


#ifdef __EMSCRIPTEN__

// The host prompt (host_prompt.h, ask "outset", modal). The record is what the dialog would show,
// OUTSET_ROUTINE::PARAMETERS' keys (lengths in millimeters, gridRounding a length or null, the
// layer by canonical name) with layers the ones its layer list offers; the answer (the same keys
// but layers, any subset, closed) is written into its own controls and its own
// TransferDataFromWindow fills the parameters (its "Line width must be a positive value." is
// checked first and becomes the refusal's message).

namespace
{
bool ccOutsetLength( const nlohmann::json& aValue, bool aPositive, long long& aIU )
{
    if( !aValue.is_number() || !std::isfinite( aValue.get<double>() ) )
        return false;

    const double iu = std::round( pcbIUScale.mmToIU( aValue.get<double>() ) );

    if( iu < ( aPositive ? 1.0 : 0.0 )
            || iu > static_cast<double>( std::numeric_limits<int>::max() ) )
    {
        return false;
    }

    aIU = static_cast<long long>( iu );
    return true;
}
} // namespace


int DIALOG_OUTSET_ITEMS::ShowModal()
{
    LoadControlState();
    TransferDataToWindow();

    nlohmann::json layers = nlohmann::json::array();

    for( unsigned ii = 0; ii < m_LayerSelectionCtrl->GetCount(); ++ii )
    {
        const int layer = (int) (intptr_t) m_LayerSelectionCtrl->GetClientData( ii );

        if( layer >= 0 && layer < PCB_LAYER_ID_COUNT )
            layers.push_back( LSET::Name( ToLAYER_ID( layer ) ).ToStdString( wxConvUTF8 ) );
    }

    const int selected = m_LayerSelectionCtrl->GetLayerSelection();

    nlohmann::json record;
    record["outsetDistance"] = pcbIUScale.IUTomm( m_outset.GetIntValue() );
    record["roundCorners"] = m_roundCorners->GetValue();
    record["useSourceLayers"] = m_copyLayers->GetValue();
    record["useSourceWidths"] = m_copyWidths->GetValue();
    record["layer"] = selected >= 0 && selected < PCB_LAYER_ID_COUNT
                              ? nlohmann::json( LSET::Name( ToLAYER_ID( selected ) )
                                                        .ToStdString( wxConvUTF8 ) )
                              : nlohmann::json( nullptr );
    record["layers"] = layers;
    record["lineWidth"] = pcbIUScale.IUTomm( m_lineWidth.GetIntValue() );
    record["gridRounding"] = m_roundToGrid->IsChecked()
                                     ? nlohmann::json( pcbIUScale.IUTomm(
                                               m_roundingGrid.GetIntValue() ) )
                                     : nlohmann::json( nullptr );
    record["deleteSourceItems"] = m_deleteSourceItems->GetValue();

    class RECEIVER : public CC_PROMPT_RECEIVER
    {
    public:
        explicit RECEIVER( DIALOG_OUTSET_ITEMS* aDialog ) : m_dialog( aDialog ) {}

        bool CcApply( const nlohmann::json& aAnswer, std::string& aField ) override
        {
            if( !aAnswer.is_object() )
                return false;

            // Read the whole answer first.
            std::optional<long long> outset, lineWidth;
            std::optional<int>       layer;
            std::optional<bool>      roundCorners, useSourceLayers, useSourceWidths, deleteSources;
            bool                     gridGiven = false;
            std::optional<long long> grid;

            for( auto it = aAnswer.begin(); it != aAnswer.end(); ++it )
            {
                const std::string& key = it.key();
                bool               ok = true;
                long long          iu = 0;

                if( key == "outsetDistance" )
                {
                    ok = ccOutsetLength( *it, false, iu );
                    outset = iu;
                }
                else if( key == "lineWidth" )
                {
                    ok = ccOutsetLength( *it, false, iu );
                    lineWidth = iu;
                }
                else if( key == "gridRounding" )
                {
                    gridGiven = true;

                    if( !it->is_null() )
                    {
                        ok = ccOutsetLength( *it, true, iu );
                        grid = iu;
                    }
                }
                else if( key == "layer" )
                {
                    ok = false;

                    for( unsigned ii = 0; it->is_string()
                                          && ii < m_dialog->m_LayerSelectionCtrl->GetCount();
                         ++ii )
                    {
                        const int candidate =
                                (int) (intptr_t) m_dialog->m_LayerSelectionCtrl->GetClientData( ii );

                        if( candidate >= 0 && candidate < PCB_LAYER_ID_COUNT
                                && LSET::Name( ToLAYER_ID( candidate ) )
                                                   .ToStdString( wxConvUTF8 )
                                           == it->get<std::string>() )
                        {
                            layer = candidate;
                            ok = true;
                            break;
                        }
                    }
                }
                else if( key == "roundCorners" || key == "useSourceLayers"
                         || key == "useSourceWidths" || key == "deleteSourceItems" )
                {
                    ok = it->is_boolean();

                    if( ok )
                    {
                        std::optional<bool>& slot = key == "roundCorners"      ? roundCorners
                                                    : key == "useSourceLayers" ? useSourceLayers
                                                    : key == "useSourceWidths" ? useSourceWidths
                                                                               : deleteSources;
                        slot = it->get<bool>();
                    }
                }
                else
                {
                    ok = false;
                }

                if( !ok )
                {
                    aField = key;
                    return false;
                }
            }

            // KiCad's one refusal, made before anything is written so no message box is raised:
            // its own sentence becomes the refusal's message.
            if( ( lineWidth ? *lineWidth : m_dialog->m_lineWidth.GetIntValue() ) <= 0 )
            {
                aField = "lineWidth";
                CcPromptCaptureMessage(
                        _( "Line width must be a positive value." ).ToStdString( wxConvUTF8 ) );
                return false;
            }

            wxCommandEvent dummy;

            if( outset )
                m_dialog->m_outset.SetValue( *outset );

            if( lineWidth )
                m_dialog->m_lineWidth.SetValue( *lineWidth );

            if( layer )
                m_dialog->m_LayerSelectionCtrl->SetLayerSelection( *layer );

            if( roundCorners )
                m_dialog->m_roundCorners->SetValue( *roundCorners );

            if( useSourceLayers )
            {
                m_dialog->m_copyLayers->SetValue( *useSourceLayers );
                m_dialog->OnCopyLayersChecked( dummy );
            }

            if( useSourceWidths )
                m_dialog->m_copyWidths->SetValue( *useSourceWidths );

            if( gridGiven )
            {
                m_dialog->m_roundToGrid->SetValue( grid.has_value() );

                if( grid )
                    m_dialog->m_roundingGrid.SetValue( *grid );

                m_dialog->OnRoundToGridChecked( dummy );
            }

            if( deleteSources )
                m_dialog->m_deleteSourceItems->SetValue( *deleteSources );

            if( !m_dialog->Validate() || !m_dialog->TransferDataFromWindow() )
            {
                aField = "lineWidth";
                return false;
            }

            m_dialog->SaveControlState();
            return true;
        }

    private:
        DIALOG_OUTSET_ITEMS* m_dialog;
    };

    RECEIVER receiver( this );

    switch( CcHostPromptModal( "outset", record, receiver ) )
    {
    case 1:  SetReturnCode( wxID_OK );     return wxID_OK;
    case 0:  SetReturnCode( wxID_CANCEL ); return wxID_CANCEL;
    default: return DIALOG_OUTSET_ITEMS_BASE::ShowModal();
    }
}

#endif // __EMSCRIPTEN__
