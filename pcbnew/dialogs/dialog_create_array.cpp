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
 * Modified by Circuit Center Inc on 2026-11-18: under __EMSCRIPTEN__ the dialog asks the host
 * (host_prompt.h, ask array, modeless) before it shows, takes the answer into its own controls
 * and runs its own TransferDataFromWindow, its own center pickers and its own cancel, and a
 * refusal's sentence is captured in place of wxMessageBox; a plain build is unchanged.
 * Modified by Circuit Center Inc on 2026-11-19: the array ask's record keys centerX, centerY
 * and centered take the American spelling.
 */

#include "dialogs/dialog_create_array.h"

#include <wx/msgdlg.h>

#include <base_units.h>
#include <confirm.h>
#include <footprint.h>
#include <pcb_edit_frame.h>
#include <tools/pcb_actions.h>
#include <tools/pcb_picker_tool.h>
#include <tool/tool_manager.h>
#include <widgets/text_ctrl_eval.h>

#ifdef __EMSCRIPTEN__
#include <cmath>
#include <initializer_list>
#include <optional>
#endif


/**
 * Struct containing the last-entered values for the dialog.
 */
struct CREATE_ARRAY_DIALOG_ENTRIES
{
    /**
     * Construct with some sensible defaults.
     * In future, this could be loaded from config?
     */
    CREATE_ARRAY_DIALOG_ENTRIES() {}

    bool     m_OptionsSet = true;

    long     m_GridNx                    = 5;
    long     m_GridNy                    = 5;
    long     m_GridDx                    = pcbIUScale.mmToIU( 2.54 );
    long     m_GridDy                    = pcbIUScale.mmToIU( 2.54 );
    long     m_GridOffsetX               = 0;
    long     m_GridOffsetY               = 0;
    long     m_GridStagger               = 1;
    bool     m_GridStaggerRows           = true;
    bool     m_GridPositionCentreOnItems = true;
    bool     m_GridPositionItemsInPlace  = true;

    bool     m_GridRenumberPads          = true;
    long     m_GridNumberingAxis         = 0;           // h then v
    bool     m_GridNumReverseAlt         = false;
    long     m_GridNumStartSet           = 1;           // use specified start
    long     m_Grid2dArrayNumbering      = 0;           // linear numbering
    long     m_GridPrimaryAxisScheme     = 0;           // numeric
    long     m_GridSecondaryAxisScheme   = 0;           // numeric
    wxString m_GridPrimaryNumOffset      = wxT( "1" );  // numeric
    wxString m_GridSecondaryNumOffset    = wxT( "1" );  // numeric
    long     m_GridPrimaryAxisStep       = 1;
    long     m_GridSecondaryAxisStep     = 1;

    long      m_CircCentreX              = 0;
    long      m_CircCentreY              = 0;
    EDA_ANGLE m_CircAngle                = ANGLE_90;
    long      m_CircleDirection          = 0;        // clockwise
    EDA_ANGLE m_CircOffsetAngle          = ANGLE_0;
    long      m_CircCount                = 4;
    bool      m_CircFullCircle           = 0;
    long      m_CircNumStartSet          = 1;        // use specified start
    long      m_GridCircNumScheme        = 0;
    wxString  m_CircNumberingOffset      = wxT("1");
    long      m_CircNumberingStep        = 1;
    bool      m_CircRotatationStep       = false;
    long      m_ArrayTypeTab             = 0;       // start on grid view
    bool      m_SelectionArrange         = false;
    bool      m_SelectionDuplicate       = true;    // Duplicate by default
    bool      m_FootprintKeepAnnotations = false;
    bool      m_FootprintReannotate      = true;    // Assign unique by default
};

// Persistent options settings
static CREATE_ARRAY_DIALOG_ENTRIES s_arrayOptions;

/**
 * Local mapping for list-box <-> numbering type
 */
struct NUMBERING_LIST_DATA
{
    ARRAY_AXIS::NUMBERING_TYPE m_numbering_type;
    wxString                   m_label;
};

/**
 * List of type <--> name mappings (in order) for the numbering type
 * list boxes
 */
static const std::vector<NUMBERING_LIST_DATA> numberingTypeData {
    {
        ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_NUMERIC,
        _( "Numerals (0,1,2,...,9,10)" ),
    },
    {
        ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_HEX,
        _( "Hexadecimal (0,1,...,F,10,...)" ),
    },
    {
        ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_ALPHA_NO_IOSQXZ,
        _( "Alphabet, minus IOSQXZ" ),
    },
    {
        ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_ALPHA_FULL,
        _( "Alphabet, full 26 characters" ),
    },
};

DIALOG_CREATE_ARRAY::DIALOG_CREATE_ARRAY( PCB_BASE_FRAME*                 aParent,
                                          std::unique_ptr<ARRAY_OPTIONS>& aSettings,
                                          bool aIsFootprintEditor, const VECTOR2I& aOrigPos ) :
        DIALOG_CREATE_ARRAY_BASE( aParent ),
        m_frame( aParent ),
        m_settings( aSettings ),
        m_originalItemPosition( aOrigPos ), m_isFootprintEditor( aIsFootprintEditor ),
        m_hSpacing( aParent, m_labelDx, m_entryDx, m_unitLabelDx ),
        m_vSpacing( aParent, m_labelDy, m_entryDy, m_unitLabelDy ),
        m_hOffset( aParent, m_labelOffsetX, m_entryOffsetX, m_unitLabelOffsetX ),
        m_vOffset( aParent, m_labelOffsetY, m_entryOffsetY, m_unitLabelOffsetY ),
        m_hCentre( aParent, m_labelCentreX, m_entryCentreX, m_unitLabelCentreX ),
        m_vCentre( aParent, m_labelCentreY, m_entryCentreY, m_unitLabelCentreY ),
        m_circAngle( aParent, m_labelCircAngle, m_entryCircAngle, m_unitLabelCircAngle ),
        m_circOffset( aParent, m_labelCircOffset, m_entryCircOffset, m_unitLabelCircOffset ),
        m_cfg_persister( pcbIUScale, s_arrayOptions.m_OptionsSet )
{
    // Configure display origin transforms
    m_hSpacing.SetCoordType( ORIGIN_TRANSFORMS::REL_X_COORD );
    m_vSpacing.SetCoordType( ORIGIN_TRANSFORMS::REL_Y_COORD );
    m_hOffset.SetCoordType( ORIGIN_TRANSFORMS::REL_X_COORD );
    m_vOffset.SetCoordType( ORIGIN_TRANSFORMS::REL_Y_COORD );
    m_hCentre.SetCoordType( ORIGIN_TRANSFORMS::ABS_X_COORD );
    m_vCentre.SetCoordType( ORIGIN_TRANSFORMS::ABS_Y_COORD );

    // Set up numbering scheme drop downs character set strings
    for( const auto& numData : numberingTypeData )
    {
        const wxString label = wxGetTranslation( numData.m_label );
        void*          clientData = (void*) &numData;

        m_choicePriAxisNumbering->Append( label, clientData );
        m_choiceSecAxisNumbering->Append( label, clientData );
        m_choiceCircNumbering->Append( label, clientData );
    }

    m_choicePriAxisNumbering->SetSelection( 0 );
    m_choiceSecAxisNumbering->SetSelection( 0 );
    m_choiceCircNumbering->SetSelection( 0 );

    m_circAngle.SetUnits( EDA_UNITS::DEGREES );
    m_circOffset.SetUnits( EDA_UNITS::DEGREES );

    // bind grid options to persister
    m_cfg_persister.Add( *m_entryNx, s_arrayOptions.m_GridNx );
    m_cfg_persister.Add( *m_entryNy, s_arrayOptions.m_GridNy );
    m_cfg_persister.Add( m_hSpacing, s_arrayOptions.m_GridDx );
    m_cfg_persister.Add( m_vSpacing, s_arrayOptions.m_GridDy );

    m_cfg_persister.Add( m_hOffset, s_arrayOptions.m_GridOffsetX );
    m_cfg_persister.Add( m_vOffset, s_arrayOptions.m_GridOffsetY );
    m_cfg_persister.Add( *m_entryStagger, s_arrayOptions.m_GridStagger );

    m_cfg_persister.Add( *m_staggerRows, s_arrayOptions.m_GridStaggerRows );

    m_cfg_persister.Add( *m_rbItemsRemainInPlace, s_arrayOptions.m_GridPositionItemsInPlace );
    m_cfg_persister.Add( *m_rbCentreOnSource, s_arrayOptions.m_GridPositionCentreOnItems );

    m_cfg_persister.Add( *m_cbRenumberPads, s_arrayOptions.m_GridRenumberPads );
    m_cfg_persister.Add( *m_radioBoxGridNumberingAxis, s_arrayOptions.m_GridNumberingAxis );
    m_cfg_persister.Add( *m_checkBoxGridReverseNumbering, s_arrayOptions.m_GridNumReverseAlt );

    m_cfg_persister.Add( *m_rbGridStartNumberingOpt, s_arrayOptions.m_GridNumStartSet );
    m_cfg_persister.Add( *m_radioBoxGridNumberingScheme, s_arrayOptions.m_Grid2dArrayNumbering );
    m_cfg_persister.Add( *m_choicePriAxisNumbering, s_arrayOptions.m_GridPrimaryAxisScheme );
    m_cfg_persister.Add( *m_choiceSecAxisNumbering, s_arrayOptions.m_GridSecondaryAxisScheme );

    m_cfg_persister.Add( *m_entryGridPriNumberingOffset, s_arrayOptions.m_GridPrimaryNumOffset );
    m_cfg_persister.Add( *m_entryGridSecNumberingOffset, s_arrayOptions.m_GridSecondaryNumOffset );
    m_cfg_persister.Add( *m_entryGridPriNumberingStep, s_arrayOptions.m_GridPrimaryAxisStep );
    m_cfg_persister.Add( *m_entryGridSecNumberingStep, s_arrayOptions.m_GridSecondaryAxisStep );

    // bind circular options to persister
    m_cfg_persister.Add( m_hCentre, s_arrayOptions.m_CircCentreX );
    m_cfg_persister.Add( m_vCentre, s_arrayOptions.m_CircCentreY );

    m_cfg_persister.Add( *m_checkBoxFullCircle, s_arrayOptions.m_CircFullCircle );
    m_cfg_persister.Add( m_circAngle, s_arrayOptions.m_CircAngle );
    m_cfg_persister.Add( m_circOffset, s_arrayOptions.m_CircOffsetAngle );
    m_cfg_persister.Add( *m_rbCircDirection, s_arrayOptions.m_CircleDirection );
    m_cfg_persister.Add( *m_entryCircCount, s_arrayOptions.m_CircCount );
    m_cfg_persister.Add( *m_entryRotateItemsCb, s_arrayOptions.m_CircRotatationStep );

    m_cfg_persister.Add( *m_rbCircStartNumberingOpt, s_arrayOptions.m_CircNumStartSet );
    m_cfg_persister.Add( *m_choiceCircNumbering, s_arrayOptions.m_GridCircNumScheme );
    m_cfg_persister.Add( *m_entryCircNumberingStart, s_arrayOptions.m_CircNumberingOffset );
    m_cfg_persister.Add( *m_entryCircNumberingStep, s_arrayOptions.m_CircNumberingStep );

    m_cfg_persister.Add( *m_gridTypeNotebook, s_arrayOptions.m_ArrayTypeTab );

    m_cfg_persister.Add( *m_radioBtnArrangeSelection, s_arrayOptions.m_SelectionArrange );
    m_cfg_persister.Add( *m_radioBtnDuplicateSelection, s_arrayOptions.m_SelectionDuplicate );

    m_cfg_persister.Add( *m_radioBtnKeepRefs, s_arrayOptions.m_FootprintKeepAnnotations );
    m_cfg_persister.Add( *m_radioBtnUniqueRefs, s_arrayOptions.m_FootprintReannotate );

    m_cfg_persister.RestoreConfigToControls();

    // Run the callbacks once to process the dialog contents
    setControlEnablement();
    calculateCircularArrayProperties();

    SetupStandardButtons();
    Fit();
    SetMinSize( GetSize() );
}


DIALOG_CREATE_ARRAY::~DIALOG_CREATE_ARRAY()
{
#ifdef __EMSCRIPTEN__
    // A prompt still parked on this dialog ends ("gone").
    CcHostPromptForget( this );
#endif
}


void DIALOG_CREATE_ARRAY::OnParameterChanged( wxCommandEvent& event )
{
    if( m_checkBoxFullCircle->GetValue() && m_entryCircAngle == event.GetEventObject() )
    {
        return;
    }

    setControlEnablement();
    calculateCircularArrayProperties();
}


void DIALOG_CREATE_ARRAY::OnSelectCenterButton( wxCommandEvent& event )
{
    event.Skip();

    TOOL_MANAGER*    toolMgr = m_frame->GetToolManager();
    PCB_PICKER_TOOL* pickerTool = toolMgr->GetTool<PCB_PICKER_TOOL>();
    wxCHECK( pickerTool, /* void */ );

    // Keep the dialog visible, but disable while picking.
    // Hiding the dialog on GTK causes the controls to not accept their new values
    Disable();

    if( event.GetEventObject() == m_btnSelectCenterItem )
    {
        toolMgr->RunAction( PCB_ACTIONS::selectItemInteractively,
                            PCB_PICKER_TOOL::INTERACTIVE_PARAMS { this, _( "Select center item..." ) } );
    }
    else if( event.GetEventObject() == m_btnSelectCenterPoint )
    {
        toolMgr->RunAction( PCB_ACTIONS::selectPointInteractively,
                            PCB_PICKER_TOOL::INTERACTIVE_PARAMS { this, _( "Select center point..." ) } );
    }
    else
    {
        wxFAIL_MSG( "Unknown event source" );
    }
}


void DIALOG_CREATE_ARRAY::OnAxisNumberingChange( wxCommandEvent& aEvent )
{
    // On an alphabet change, make sure the offset control is valid by default.

    const int newAlphabet = aEvent.GetSelection();

    wxCHECK( newAlphabet >= 0 && newAlphabet < static_cast<int>( numberingTypeData.size() ),
             /* void */ );

    const ARRAY_AXIS::NUMBERING_TYPE numberingType =
            numberingTypeData[newAlphabet].m_numbering_type;

    wxTextCtrl* matchingTextCtrl = nullptr;

    if( aEvent.GetEventObject() == m_choicePriAxisNumbering )
        matchingTextCtrl = m_entryGridPriNumberingOffset;
    else if( aEvent.GetEventObject() == m_choiceSecAxisNumbering )
        matchingTextCtrl = m_entryGridSecNumberingOffset;
    else if( aEvent.GetEventObject() == m_choiceCircNumbering )
        matchingTextCtrl = m_entryCircNumberingStart;

    wxCHECK( matchingTextCtrl, /* void */ );

    ARRAY_AXIS dummyAxis;
    dummyAxis.SetAxisType( numberingType );

    // If the text control has a valid value for the new alphabet, keep it
    // else reset to the first value in the new alphabet.

    const bool isAlreadyOK = dummyAxis.SetOffset( matchingTextCtrl->GetValue() );

    if( !isAlreadyOK )
    {
        dummyAxis.SetOffset( ARRAY_AXIS::TypeIsNumeric( numberingType ) ? 1 : 0 );
        matchingTextCtrl->SetValue( dummyAxis.GetItemNumber( 0 ) );
    }
}


// Implement the RECEIVER interface for the callback from the TOOL
void DIALOG_CREATE_ARRAY::UpdatePickedItem( const EDA_ITEM* aItem )
{
    if( aItem )
    {
        m_hCentre.SetValue( aItem->GetPosition().x );
        m_vCentre.SetValue( aItem->GetPosition().y );
    }

    Enable( true );

#ifdef __EMSCRIPTEN__
    // The host's pick ended: its card shows the new center.
    if( m_ccToken )
        CcHostPromptUpdate( m_ccToken, ccRecord() );
#endif
}


void DIALOG_CREATE_ARRAY::UpdatePickedPoint( const std::optional<VECTOR2I>& aPoint )
{
    if( aPoint )
    {
        m_hCentre.SetValue( aPoint->x );
        m_vCentre.SetValue( aPoint->y );
    }

    Enable( true );

#ifdef __EMSCRIPTEN__
    // The host's pick ended: its card shows the new center.
    if( m_ccToken )
        CcHostPromptUpdate( m_ccToken, ccRecord() );
#endif
}


/**
 * Validate and save a long integer entry
 *
 * @param entry the text entry to read from
 * @param dest the value destination
 * @param description description of the field (used if the value is not OK)
 * @param errors a list of errors to add any error to
 * @return valid
 */
static bool validateLongEntry( const wxTextEntry& entry, long& dest, const wxString& description,
                               wxArrayString& errors )
{
    bool ok = true;

    if( !entry.GetValue().ToLong( &dest ) )
    {
        wxString err;
        err.Printf( _( "Bad numeric value for %s: %s" ), description, entry.GetValue() );
        errors.Add( err );
        ok = false;
    }

    return ok;
}


/**
 * Validates and saves (if valid) the type and offset of an array axis numbering
 *
 * @param offsetEntry the entry of the offset (text)
 * @param typeEntry the entry of the axis nmbering scheme (choice)
 * @param type the destination of the type if valid
 * @param offset the destination of the offset if valid
 * @param errors error string accumulator
 * @return if all valid
 */
static bool validateAxisOptions( const wxTextCtrl& offsetEntry, const wxChoice& typeEntry,
                                 const wxTextCtrl& aStepEntry, ARRAY_AXIS& aAxis,
                                 wxArrayString& errors )
{
    void*                      clientData = typeEntry.GetClientData( typeEntry.GetSelection() );
    const NUMBERING_LIST_DATA* numberingData = static_cast<NUMBERING_LIST_DATA*>( clientData );

    wxCHECK_MSG( numberingData, false, wxT( "Failed to get client data from list control." ) );

    aAxis.SetAxisType( numberingData->m_numbering_type );

    const wxString text = offsetEntry.GetValue();

    bool ok = aAxis.SetOffset( text );

    if( !ok )
    {
        errors.Add( wxString::Format( _( "Could not determine numbering start from '%s': "
                                         "expected value consistent with alphabet '%s'." ),
                                      text,
                                      aAxis.GetAlphabet() ) );
        return false;
    }

    long step;
    ok = validateLongEntry( aStepEntry, step, _( "step value" ), errors );

    if( ok )
        aAxis.SetStep( step );

    return ok;
}


bool DIALOG_CREATE_ARRAY::TransferDataFromWindow()
{
    std::unique_ptr<ARRAY_OPTIONS> newSettings;

    wxArrayString   errors;
    wxArrayString   stackWarnings;
    const wxWindow* page = m_gridTypeNotebook->GetCurrentPage();

    if( page == m_gridPanel )
    {
        auto newGrid = std::make_unique<ARRAY_GRID_OPTIONS>();
        bool ok = true;

        // ints
        ok &= validateLongEntry(*m_entryNx, newGrid->m_nx, _("horizontal count"), errors);
        ok &= validateLongEntry(*m_entryNy, newGrid->m_ny, _("vertical count"), errors);

        newGrid->m_delta.x = m_hSpacing.GetIntValue();
        newGrid->m_delta.y = m_vSpacing.GetIntValue();

        newGrid->m_offset.x = m_hOffset.GetIntValue();
        newGrid->m_offset.y = m_vOffset.GetIntValue();

        // Collect (but don't block on) configurations that would stack items in place.
        // The user is warned and asked to confirm after all settings are validated.
        if( newGrid->m_nx > 1 && newGrid->m_delta.x == 0 && newGrid->m_offset.x == 0 )
        {
            stackWarnings.Add( wxString::Format( _( "horizontal delta of zero with %ld objects" ),
                                                 newGrid->m_nx ) );
        }

        if( newGrid->m_ny > 1 && newGrid->m_delta.y == 0 && newGrid->m_offset.y == 0 )
        {
            stackWarnings.Add( wxString::Format( _( "vertical delta of zero with %ld objects" ),
                                                 newGrid->m_ny ) );
        }

        newGrid->m_centred = m_rbCentreOnSource->GetValue();

        ok &= validateLongEntry(*m_entryStagger, newGrid->m_stagger, _("stagger"), errors);

        newGrid->m_stagger_rows = m_staggerRows->GetValue();

        newGrid->m_horizontalThenVertical = m_radioBoxGridNumberingAxis->GetSelection() == 0;
        newGrid->m_reverseNumberingAlternate = m_checkBoxGridReverseNumbering->GetValue();

        newGrid->SetShouldNumber( m_isFootprintEditor && m_cbRenumberPads->GetValue() );

        if( m_isFootprintEditor )
        {
            newGrid->SetNumberingStartIsSpecified( m_rbGridStartNumberingOpt->GetSelection() == 1 );

            if( newGrid->GetNumberingStartIsSpecified() )
            {
                newGrid->m_2dArrayNumbering = m_radioBoxGridNumberingScheme->GetSelection() != 0;

                // validate from the input fields
                bool numOk = validateAxisOptions( *m_entryGridPriNumberingOffset,
                                                  *m_choicePriAxisNumbering,
                                                  *m_entryGridPriNumberingStep,
                                                  newGrid->m_pri_axis, errors );

                if( newGrid->m_2dArrayNumbering )
                {
                    numOk &= validateAxisOptions( *m_entryGridSecNumberingOffset,
                                                  *m_choiceSecAxisNumbering,
                                                  *m_entryGridSecNumberingStep,
                                                  newGrid->m_sec_axis, errors );
                }

                ok &= numOk;
            }
            else
            {
                // artificial linear numeric scheme from 1
                newGrid->m_2dArrayNumbering = false;
                newGrid->m_pri_axis.SetAxisType( ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_NUMERIC );
                newGrid->m_pri_axis.SetOffset( 1 );
            }
        }

        // Only use settings if all values are good
        if( ok )
            newSettings = std::move( newGrid );
    }
    else if( page == m_circularPanel )
    {
        auto   newCirc = std::make_unique<ARRAY_CIRCULAR_OPTIONS>();
        bool   ok = true;
        double angle = EDA_UNIT_UTILS::UI::DoubleValueFromString( m_entryCircAngle->GetValue() );
        double offset = EDA_UNIT_UTILS::UI::DoubleValueFromString( m_entryCircOffset->GetValue() );

        newCirc->m_centre.x = m_hCentre.GetIntValue();
        newCirc->m_centre.y = m_vCentre.GetIntValue();
        newCirc->m_angle = EDA_ANGLE( angle, DEGREES_T );
        newCirc->m_angleOffset = EDA_ANGLE( offset, DEGREES_T );
        newCirc->m_clockwise = m_rbCircDirection->GetSelection() == 0;

        ok = validateLongEntry(*m_entryCircCount, newCirc->m_nPts, _("point count"), errors);

        newCirc->m_rotateItems = m_entryRotateItemsCb->GetValue();
        newCirc->SetShouldNumber( m_isFootprintEditor );

        if( newCirc->m_nPts > 1 && newCirc->m_angle.IsZero() )
        {
            stackWarnings.Add( wxString::Format( _( "angular delta of zero with %ld objects" ),
                                                 newCirc->m_nPts ) );
        }

        if( m_isFootprintEditor )
        {
            newCirc->SetNumberingStartIsSpecified( m_rbCircStartNumberingOpt->GetSelection() == 1 );

            if( newCirc->GetNumberingStartIsSpecified() )
            {
                ok &= validateAxisOptions( *m_entryCircNumberingStart, *m_choiceCircNumbering,
                                           *m_entryCircNumberingStep, newCirc->m_axis, errors );
            }
            else
            {
                // artificial linear numeric scheme from 1
                newCirc->m_axis.SetAxisType( ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_NUMERIC );
                newCirc->m_axis.SetOffset( 1 ); // Start at "1"
            }
        }

        // Only use settings if all values are good
        if( ok )
            newSettings = std::move( newCirc );
    }

    bool ret = false;

    // Warn (but don't block) when the array would stack items on top of one another.
    // Zero-spacing arrays are legitimate when intentionally duplicating in place, but
    // accidental zero spacing on a large count can stamp out thousands of overlapping
    // objects. Confirm before proceeding.
    if( newSettings && !stackWarnings.IsEmpty() )
    {
        wxString msg;

        for( const wxString& warning : stackWarnings )
            msg << warning << wxT( "\n" );

        if( !IsOK( this, msg ) )
            return false;
    }

    // If we got good settings, send them out and finish
    if( newSettings )
    {
        // assign pointer and ownership here
        m_settings = std::move( newSettings );

        m_settings->SetShouldArrangeSelection( m_radioBtnArrangeSelection->GetValue() );
        m_settings->SetSShouldReannotateFootprints( m_radioBtnUniqueRefs->GetValue() );

        // persist the control state for next time
        m_cfg_persister.ReadConfigFromControls();

        ret = true;
    }
    else
    {
        wxString errorStr;

        if( errors.IsEmpty() )
            errorStr = _("Bad parameters");
        else
            errorStr = wxJoin( errors, '\n' );

#ifdef __EMSCRIPTEN__
        // While the host's answer is applied, KiCad's sentence is the refusal's message.
        if( CcPromptCapturing() )
            CcPromptCaptureMessage( errorStr.ToStdString( wxConvUTF8 ) );
        else
#endif
        wxMessageBox( errorStr );
        ret = false;
    }

    // This dialog is not modal, so close it now if successful
    if( ret )
        Close();

    return ret;
}


void DIALOG_CREATE_ARRAY::setControlEnablement()
{
    if( m_checkBoxFullCircle->GetValue() )
    {
        m_entryCircAngle->Disable();
    }
    else
    {
        m_entryCircAngle->Enable();
    }

    if( m_isFootprintEditor )
    {
        m_footprintReannotatePanel->Show( false );

        m_gridPadNumberingPanel->Show( true );
        m_circularPadNumberingPanel->Show( true );

        // In no pad re-numbering, everything is disabled
        bool renumber_pads = m_cbRenumberPads->GetValue();

        m_radioBoxGridNumberingAxis->Enable( renumber_pads );
        m_checkBoxGridReverseNumbering->Enable( renumber_pads );
        m_rbGridStartNumberingOpt->Enable( renumber_pads );

        // If we set the start number, we can set the other options,
        // otherwise it's a hardcoded linear array
        const bool use_set_start_grid = renumber_pads && m_rbGridStartNumberingOpt->GetSelection() == 1;

        m_radioBoxGridNumberingScheme->Enable( use_set_start_grid );
        m_labelPriAxisNumbering->Enable( use_set_start_grid );
        m_choicePriAxisNumbering->Enable( use_set_start_grid );

        // Disable the secondary axis numbering option if the
        // numbering scheme doesn't have two axes
        const bool num2d = m_radioBoxGridNumberingScheme->GetSelection() != 0;

        m_labelSecAxisNumbering->Enable( use_set_start_grid && num2d );
        m_choiceSecAxisNumbering->Enable( use_set_start_grid && num2d );

        // We can only set an offset if we're setting the start number
        m_labelGridNumberingOffset->Enable( use_set_start_grid );
        m_labelGridNumberingStep->Enable( use_set_start_grid );
        m_entryGridPriNumberingOffset->Enable( use_set_start_grid );
        m_entryGridPriNumberingStep->Enable( use_set_start_grid );
        m_entryGridSecNumberingOffset->Enable( use_set_start_grid && num2d );
        m_entryGridSecNumberingStep->Enable( use_set_start_grid && num2d );

        // disable the circular number offset in the same way
        const bool use_set_start_circ = renumber_pads && m_rbCircStartNumberingOpt->GetSelection() == 1;
        m_entryCircNumberingStart->Enable( use_set_start_circ );
    }
    else
    {
        // grid
        m_rbGridStartNumberingOpt->Enable( false );
        m_radioBoxGridNumberingScheme->Enable( false );

        m_labelPriAxisNumbering->Enable( false );
        m_labelSecAxisNumbering->Enable( false );

        m_choiceSecAxisNumbering->Enable( false );
        m_choicePriAxisNumbering->Enable( false );

        m_labelGridNumberingOffset->Enable( false );
        m_entryGridPriNumberingOffset->Enable( false );
        m_entryGridSecNumberingOffset->Enable( false );

        m_gridPadNumberingPanel->Show( false );

        // circular
        m_rbCircStartNumberingOpt->Enable( false );
        m_entryCircNumberingStart->Enable( false );

        m_circularPadNumberingPanel->Show( false );

        m_footprintReannotatePanel->Show( true );
    }

    if( m_radioBtnArrangeSelection->GetValue() )
    {
        m_footprintReannotatePanel->Show( false );
    }
}


void DIALOG_CREATE_ARRAY::calculateCircularArrayProperties()
{
    // In full circle mode, the division angle is computed from the number of points
    if( m_checkBoxFullCircle->GetValue() )
    {
        long nPts;
        if( m_entryCircCount->GetValue().ToLong( &nPts ) )
        {
            EDA_ANGLE division = EDA_ANGLE( 360, DEGREES_T ) / nPts;
            m_circAngle.SetAngleValue( division );
        }
    }
}


#ifdef __EMSCRIPTEN__

// The host prompt (host_prompt.h, ask "array", modeless): the record is what the controls hold
// (lengths in millimeters, angles in degrees), the answer is written into the same controls and
// KiCad's own TransferDataFromWindow decides, then closes the dialog so ARRAY_TOOL makes the
// array as for its own OK button.

namespace
{

/// A refused answer names its key.
bool ccRefuse( std::string& aField, const char* aKey )
{
    aField = aKey;
    return false;
}


const char* ccNumberingName( ARRAY_AXIS::NUMBERING_TYPE aType )
{
    switch( aType )
    {
    case ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_NUMERIC:         return "numeric";
    case ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_HEX:             return "hex";
    case ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_ALPHA_NO_IOSQXZ: return "alphaShort";
    case ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_ALPHA_FULL:      return "alphaFull";
    }

    return "numeric";
}


long ccLongOf( const wxTextCtrl& aCtrl, long aDefault )
{
    long value = aDefault;

    if( !aCtrl.GetValue().ToLong( &value ) )
        return aDefault;

    return value;
}


nlohmann::json ccAxisRecord( const wxChoice& aType, const wxTextCtrl& aStart,
                             const wxTextCtrl& aStep )
{
    const int sel = aType.GetSelection();
    const ARRAY_AXIS::NUMBERING_TYPE type =
            ( sel >= 0 && sel < static_cast<int>( numberingTypeData.size() ) )
                    ? numberingTypeData[sel].m_numbering_type
                    : ARRAY_AXIS::NUMBERING_TYPE::NUMBERING_NUMERIC;

    return nlohmann::json{ { "type", ccNumberingName( type ) },
                           { "start", aStart.GetValue().ToStdString( wxConvUTF8 ) },
                           { "step", ccLongOf( aStep, 1 ) } };
}


// Bounds of an answer: a length within the board's integer range, an angle of at most ten
// turns, a count or step that fits KiCad's long.
constexpr double CC_MAX_MM = 2000.0;
constexpr double CC_MAX_DEG = 3600.0;
constexpr long   CC_MAX_COUNT = 1000000;


bool ccReadBool( const nlohmann::json& aObj, const char* aKey, std::optional<bool>& aOut )
{
    auto it = aObj.find( aKey );

    if( it == aObj.end() )
        return true;

    if( !it->is_boolean() )
        return false;

    aOut = it->get<bool>();
    return true;
}


bool ccReadLong( const nlohmann::json& aObj, const char* aKey, std::optional<long>& aOut )
{
    auto it = aObj.find( aKey );

    if( it == aObj.end() )
        return true;

    if( !it->is_number_integer() )
        return false;

    const long long v = it->get<long long>();

    if( v < -CC_MAX_COUNT || v > CC_MAX_COUNT )
        return false;

    aOut = static_cast<long>( v );
    return true;
}


bool ccReadNumber( const nlohmann::json& aObj, const char* aKey, double aMax,
                   std::optional<double>& aOut )
{
    auto it = aObj.find( aKey );

    if( it == aObj.end() )
        return true;

    if( !it->is_number() )
        return false;

    const double v = it->get<double>();

    if( !std::isfinite( v ) || v < -aMax || v > aMax )
        return false;

    aOut = v;
    return true;
}


bool ccOnlyKeys( const nlohmann::json& aObj, std::initializer_list<const char*> aKeys,
                 std::string& aField )
{
    for( auto it = aObj.begin(); it != aObj.end(); ++it )
    {
        bool known = false;

        for( const char* key : aKeys )
            known = known || it.key() == key;

        if( !known )
        {
            aField = it.key();
            return false;
        }
    }

    return true;
}


/// An axis { type, start, step } checked, then (aWrite) written into its three controls.
bool ccWriteAxis( const nlohmann::json& aAxis, wxChoice& aType, wxTextCtrl& aStart,
                  wxTextCtrl& aStep, bool aWrite )
{
    std::string ignored;

    if( !aAxis.is_object() || !ccOnlyKeys( aAxis, { "type", "start", "step" }, ignored ) )
        return false;

    auto type = aAxis.find( "type" );

    if( type != aAxis.end() )
    {
        if( !type->is_string() )
            return false;

        const std::string name = type->get<std::string>();
        int               index = -1;

        for( size_t ii = 0; ii < numberingTypeData.size(); ++ii )
        {
            if( name == ccNumberingName( numberingTypeData[ii].m_numbering_type ) )
                index = static_cast<int>( ii );
        }

        if( index < 0 )
            return false;

        if( aWrite )
            aType.SetSelection( index );
    }

    auto start = aAxis.find( "start" );

    if( start != aAxis.end() )
    {
        if( !start->is_string() || start->get<std::string>().size() > 64 )
            return false;

        if( aWrite )
            aStart.ChangeValue( wxString::FromUTF8( start->get<std::string>() ) );
    }

    std::optional<long> step;

    if( !ccReadLong( aAxis, "step", step ) )
        return false;

    if( step && aWrite )
        aStep.ChangeValue( wxString::Format( wxT( "%ld" ), *step ) );

    return true;
}

} // namespace


nlohmann::json DIALOG_CREATE_ARRAY::ccRecord()
{
    nlohmann::json grid;
    grid["nx"] = ccLongOf( *m_entryNx, 0 );
    grid["ny"] = ccLongOf( *m_entryNy, 0 );
    grid["dx"] = pcbIUScale.IUTomm( m_hSpacing.GetIntValue() );
    grid["dy"] = pcbIUScale.IUTomm( m_vSpacing.GetIntValue() );
    grid["offsetX"] = pcbIUScale.IUTomm( m_hOffset.GetIntValue() );
    grid["offsetY"] = pcbIUScale.IUTomm( m_vOffset.GetIntValue() );
    grid["centered"] = m_rbCentreOnSource->GetValue();
    grid["stagger"] = ccLongOf( *m_entryStagger, 1 );
    grid["staggerRows"] = m_staggerRows->GetValue();
    grid["horizontalThenVertical"] = m_radioBoxGridNumberingAxis->GetSelection() == 0;
    grid["reverseNumberingAlternate"] = m_checkBoxGridReverseNumbering->GetValue();
    grid["numberingStartSpecified"] = m_rbGridStartNumberingOpt->GetSelection() == 1;
    grid["numbering2d"] = m_radioBoxGridNumberingScheme->GetSelection() != 0;
    grid["primary"] = ccAxisRecord( *m_choicePriAxisNumbering, *m_entryGridPriNumberingOffset,
                                    *m_entryGridPriNumberingStep );
    grid["secondary"] = ccAxisRecord( *m_choiceSecAxisNumbering, *m_entryGridSecNumberingOffset,
                                      *m_entryGridSecNumberingStep );

    nlohmann::json circular;
    circular["centerX"] = pcbIUScale.IUTomm( m_hCentre.GetIntValue() );
    circular["centerY"] = pcbIUScale.IUTomm( m_vCentre.GetIntValue() );
    circular["angle"] = EDA_UNIT_UTILS::UI::DoubleValueFromString( m_entryCircAngle->GetValue() );
    circular["angleOffset"] =
            EDA_UNIT_UTILS::UI::DoubleValueFromString( m_entryCircOffset->GetValue() );
    circular["clockwise"] = m_rbCircDirection->GetSelection() == 0;
    circular["count"] = ccLongOf( *m_entryCircCount, 0 );
    circular["rotateItems"] = m_entryRotateItemsCb->GetValue();
    circular["numberingStartSpecified"] = m_rbCircStartNumberingOpt->GetSelection() == 1;
    circular["axis"] = ccAxisRecord( *m_choiceCircNumbering, *m_entryCircNumberingStart,
                                     *m_entryCircNumberingStep );

    nlohmann::json record;
    record["shape"] = m_gridTypeNotebook->GetCurrentPage() == m_circularPanel ? "circular" : "grid";
    record["arrangeSelection"] = m_radioBtnArrangeSelection->GetValue();
    record["uniqueReferences"] = m_radioBtnUniqueRefs->GetValue();
    record["padNumbering"] = m_isFootprintEditor;
    record["grid"] = grid;
    record["circular"] = circular;
    return record;
}


bool DIALOG_CREATE_ARRAY::ccWriteAnswer( const nlohmann::json& aAnswer, std::string& aField )
{
    if( !aAnswer.is_object()
            || !ccOnlyKeys( aAnswer, { "shape", "arrangeSelection", "uniqueReferences",
                                       "padNumbering", "grid", "circular" }, aField ) )
    {
        return false;
    }

    // Read the whole answer first: nothing is written unless every key is good.
    std::optional<bool> arrange, unique, padNumbering;

    if( !ccReadBool( aAnswer, "arrangeSelection", arrange ) )
        return ccRefuse( aField, "arrangeSelection" );

    if( !ccReadBool( aAnswer, "uniqueReferences", unique ) )
        return ccRefuse( aField, "uniqueReferences" );

    if( !ccReadBool( aAnswer, "padNumbering", padNumbering ) )   // KiCad's, never written
        return ccRefuse( aField, "padNumbering" );

    int page = -1;
    auto shape = aAnswer.find( "shape" );

    if( shape != aAnswer.end() )
    {
        if( !shape->is_string()
                || ( shape->get<std::string>() != "grid" && shape->get<std::string>() != "circular" ) )
        {
            return ccRefuse( aField, "shape" );
        }

        page = m_gridTypeNotebook->FindPage( shape->get<std::string>() == "grid" ? m_gridPanel
                                                                                 : m_circularPanel );
    }

    struct GRID_IN
    {
        std::optional<long>   nx, ny, stagger;
        std::optional<double> dx, dy, offsetX, offsetY;
        std::optional<bool>   centered, staggerRows, hThenV, reverse, startSpecified, twoD;
    } g;

    auto grid = aAnswer.find( "grid" );

    if( grid != aAnswer.end() )
    {
        std::string sub;

        if( !grid->is_object()
                || !ccOnlyKeys( *grid, { "nx", "ny", "dx", "dy", "offsetX", "offsetY", "centered",
                                         "stagger", "staggerRows", "horizontalThenVertical",
                                         "reverseNumberingAlternate", "numberingStartSpecified",
                                         "numbering2d", "primary", "secondary" }, sub )
                || !ccReadLong( *grid, "nx", g.nx ) || !ccReadLong( *grid, "ny", g.ny )
                || !ccReadLong( *grid, "stagger", g.stagger )
                || !ccReadNumber( *grid, "dx", CC_MAX_MM, g.dx )
                || !ccReadNumber( *grid, "dy", CC_MAX_MM, g.dy )
                || !ccReadNumber( *grid, "offsetX", CC_MAX_MM, g.offsetX )
                || !ccReadNumber( *grid, "offsetY", CC_MAX_MM, g.offsetY )
                || !ccReadBool( *grid, "centered", g.centered )
                || !ccReadBool( *grid, "staggerRows", g.staggerRows )
                || !ccReadBool( *grid, "horizontalThenVertical", g.hThenV )
                || !ccReadBool( *grid, "reverseNumberingAlternate", g.reverse )
                || !ccReadBool( *grid, "numberingStartSpecified", g.startSpecified )
                || !ccReadBool( *grid, "numbering2d", g.twoD ) )
        {
            return ccRefuse( aField, "grid" );
        }

        auto pri = grid->find( "primary" );
        auto sec = grid->find( "secondary" );

        if( ( pri != grid->end()
              && !ccWriteAxis( *pri, *m_choicePriAxisNumbering, *m_entryGridPriNumberingOffset,
                               *m_entryGridPriNumberingStep, false ) )
                || ( sec != grid->end()
                     && !ccWriteAxis( *sec, *m_choiceSecAxisNumbering,
                                      *m_entryGridSecNumberingOffset,
                                      *m_entryGridSecNumberingStep, false ) ) )
        {
            return ccRefuse( aField, "grid" );
        }
    }

    struct CIRC_IN
    {
        std::optional<double> centerX, centerY, angle, angleOffset;
        std::optional<long>   count;
        std::optional<bool>   clockwise, rotateItems, startSpecified;
    } c;

    auto circular = aAnswer.find( "circular" );

    if( circular != aAnswer.end() )
    {
        std::string sub;

        if( !circular->is_object()
                || !ccOnlyKeys( *circular, { "centerX", "centerY", "angle", "angleOffset",
                                             "clockwise", "count", "rotateItems",
                                             "numberingStartSpecified", "axis" }, sub )
                || !ccReadNumber( *circular, "centerX", CC_MAX_MM, c.centerX )
                || !ccReadNumber( *circular, "centerY", CC_MAX_MM, c.centerY )
                || !ccReadNumber( *circular, "angle", CC_MAX_DEG, c.angle )
                || !ccReadNumber( *circular, "angleOffset", CC_MAX_DEG, c.angleOffset )
                || !ccReadLong( *circular, "count", c.count )
                || !ccReadBool( *circular, "clockwise", c.clockwise )
                || !ccReadBool( *circular, "rotateItems", c.rotateItems )
                || !ccReadBool( *circular, "numberingStartSpecified", c.startSpecified ) )
        {
            return ccRefuse( aField, "circular" );
        }

        auto axis = circular->find( "axis" );

        if( axis != circular->end()
                && !ccWriteAxis( *axis, *m_choiceCircNumbering, *m_entryCircNumberingStart,
                                 *m_entryCircNumberingStep, false ) )
        {
            return ccRefuse( aField, "circular" );
        }
    }

    // Every key read: write them into the dialog's own controls.
    if( page >= 0 )
        m_gridTypeNotebook->ChangeSelection( page );

    if( arrange )
        ( *arrange ? m_radioBtnArrangeSelection : m_radioBtnDuplicateSelection )->SetValue( true );

    if( unique )
        ( *unique ? m_radioBtnUniqueRefs : m_radioBtnKeepRefs )->SetValue( true );

    if( g.nx )
        m_entryNx->ChangeValue( wxString::Format( wxT( "%ld" ), *g.nx ) );

    if( g.ny )
        m_entryNy->ChangeValue( wxString::Format( wxT( "%ld" ), *g.ny ) );

    if( g.dx )
        m_hSpacing.ChangeValue( pcbIUScale.mmToIU( *g.dx ) );

    if( g.dy )
        m_vSpacing.ChangeValue( pcbIUScale.mmToIU( *g.dy ) );

    if( g.offsetX )
        m_hOffset.ChangeValue( pcbIUScale.mmToIU( *g.offsetX ) );

    if( g.offsetY )
        m_vOffset.ChangeValue( pcbIUScale.mmToIU( *g.offsetY ) );

    if( g.centered )
        ( *g.centered ? m_rbCentreOnSource : m_rbItemsRemainInPlace )->SetValue( true );

    if( g.stagger )
        m_entryStagger->ChangeValue( wxString::Format( wxT( "%ld" ), *g.stagger ) );

    if( g.staggerRows )
        ( *g.staggerRows ? m_staggerRows : m_staggerCols )->SetValue( true );

    if( g.hThenV )
        m_radioBoxGridNumberingAxis->SetSelection( *g.hThenV ? 0 : 1 );

    if( g.reverse )
        m_checkBoxGridReverseNumbering->SetValue( *g.reverse );

    if( g.startSpecified )
        m_rbGridStartNumberingOpt->SetSelection( *g.startSpecified ? 1 : 0 );

    if( g.twoD )
        m_radioBoxGridNumberingScheme->SetSelection( *g.twoD ? 1 : 0 );

    if( grid != aAnswer.end() )
    {
        auto pri = grid->find( "primary" );
        auto sec = grid->find( "secondary" );

        if( pri != grid->end() )
            ccWriteAxis( *pri, *m_choicePriAxisNumbering, *m_entryGridPriNumberingOffset,
                         *m_entryGridPriNumberingStep, true );

        if( sec != grid->end() )
            ccWriteAxis( *sec, *m_choiceSecAxisNumbering, *m_entryGridSecNumberingOffset,
                         *m_entryGridSecNumberingStep, true );
    }

    if( c.centerX )
        m_hCentre.ChangeValue( pcbIUScale.mmToIU( *c.centerX ) );

    if( c.centerY )
        m_vCentre.ChangeValue( pcbIUScale.mmToIU( *c.centerY ) );

    if( c.count )
        m_entryCircCount->ChangeValue( wxString::Format( wxT( "%ld" ), *c.count ) );

    if( c.angle )
    {
        // The answer's angle governs: KiCad's full circle box would derive it from the count.
        m_checkBoxFullCircle->SetValue( false );
        m_circAngle.ChangeAngleValue( EDA_ANGLE( *c.angle, DEGREES_T ) );
    }

    if( c.angleOffset )
        m_circOffset.ChangeAngleValue( EDA_ANGLE( *c.angleOffset, DEGREES_T ) );

    if( c.clockwise )
        m_rbCircDirection->SetSelection( *c.clockwise ? 0 : 1 );

    if( c.rotateItems )
        m_entryRotateItemsCb->SetValue( *c.rotateItems );

    if( c.startSpecified )
        m_rbCircStartNumberingOpt->SetSelection( *c.startSpecified ? 1 : 0 );

    if( circular != aAnswer.end() )
    {
        auto axis = circular->find( "axis" );

        if( axis != circular->end() )
            ccWriteAxis( *axis, *m_choiceCircNumbering, *m_entryCircNumberingStart,
                         *m_entryCircNumberingStep, true );
    }

    setControlEnablement();
    calculateCircularArrayProperties();
    return true;
}


bool DIALOG_CREATE_ARRAY::Show( bool aShow )
{
    if( aShow )
    {
        // Already the host's: its card is open.
        if( m_ccToken && CcParkedPrompts().count( m_ccToken ) )
            return true;

        m_ccToken = CcHostPromptModeless( "array", ccRecord(), this );

        if( m_ccToken )
            return true;
    }

    return DIALOG_CREATE_ARRAY_BASE::Show( aShow );
}


bool DIALOG_CREATE_ARRAY::CcApply( const nlohmann::json& aAnswer, std::string& aField )
{
    if( !ccWriteAnswer( aAnswer, aField ) )
        return false;

    // KiCad's own OK: validate, transfer (which closes the dialog, and ARRAY_TOOL then makes
    // the array in one commit). A refusal keeps the prompt parked.
    if( !Validate() || !TransferDataFromWindow() )
        return false;

    // The prompt ends with the dialog; the host's accepted answer closes its card.
    CcParkedPrompts().erase( m_ccToken );
    m_ccToken = 0;
    return true;
}


bool DIALOG_CREATE_ARRAY::CcPick( const std::string& aKind )
{
    wxButton* button = aKind == "point" ? m_btnSelectCenterPoint
                     : aKind == "item"  ? m_btnSelectCenterItem
                                        : nullptr;

    if( !button )
        return false;

    // The dialog's own picker: KiCad's PCB_PICKER_TOOL; its result arrives in
    // UpdatePickedItem or UpdatePickedPoint, which sends the host the new record.
    wxCommandEvent event( wxEVT_BUTTON, button->GetId() );
    event.SetEventObject( button );
    OnSelectCenterButton( event );
    return true;
}


void DIALOG_CREATE_ARRAY::CcCancel()
{
    // KiCad's own cancel of this modeless dialog: close it with no options, so ARRAY_TOOL
    // destroys it and makes nothing.
    m_ccToken = 0;
    Close();
}

#endif // __EMSCRIPTEN__
