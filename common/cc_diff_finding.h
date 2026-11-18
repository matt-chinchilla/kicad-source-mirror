// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_DIFF_FINDING_H
#define CC_DIFF_FINDING_H

#ifdef __EMSCRIPTEN__

#include <string>
#include <vector>
#include <kiid.h>
#include <reporter.h>
#include <wx/string.h>

/**
 * One line of a comparison against an item's source (LIB_SYMBOL::Compare,
 * FOOTPRINT::FootprintNeedsUpdate) as a record, for the WebAssembly editor's host (engine run
 * 15, K-DIFF): the code of the REPORT site that said it, the item it is about, the pin or pad
 * number, the field name. KiCad's sentences are unchanged and still reach the reporter.
 */
struct CC_DIFF_FINDING
{
    std::string code;        ///< the site's code ("pin", "fieldMissing", "pad", ...); "other" if untagged
    bool        hasUuid = false;
    KIID        uuid;        ///< the item, when the site names one
    wxString    pin;         ///< a pin or pad number
    wxString    field;       ///< a field name, or the attribute a code covers
};

/**
 * A reporter that keeps one CC_DIFF_FINDING per Report, carrying the tag the comparing code
 * set last (CcDiffTag), and forwards every line to an inner reporter when one is given.
 * Passing one as the comparison's reporter also puts the comparison in its report-everything
 * mode (KiCad's own rule: a reporter means every difference is checked and said).
 */
class CC_DIFF_REPORTER : public REPORTER
{
public:
    explicit CC_DIFF_REPORTER( REPORTER* aInner = nullptr ) : m_inner( aInner )
    {
        m_tag.code = "other";
    }

    using REPORTER::Report;

    REPORTER& Report( const wxString& aText,
                      SEVERITY aSeverity = RPT_SEVERITY_UNDEFINED ) override
    {
        REPORTER::Report( aText, aSeverity );
        m_findings.push_back( m_tag );
        m_lines.push_back( aText );

        if( m_inner )
            m_inner->Report( aText, aSeverity );

        return *this;
    }

    void Tag( const char* aCode, const KIID* aUuid, const wxString& aPin, const wxString& aField )
    {
        m_tag = CC_DIFF_FINDING();
        m_tag.code = aCode ? aCode : "other";
        m_tag.hasUuid = aUuid != nullptr;

        if( aUuid )
            m_tag.uuid = *aUuid;

        m_tag.pin = aPin;
        m_tag.field = aField;
    }

    const std::vector<CC_DIFF_FINDING>& Findings() const { return m_findings; }
    const std::vector<wxString>&        Lines() const { return m_lines; }

private:
    REPORTER*                    m_inner;
    CC_DIFF_FINDING              m_tag;
    std::vector<CC_DIFF_FINDING> m_findings;
    std::vector<wxString>        m_lines;
};

/// Tag the next lines \a aReporter takes, when it is a CC_DIFF_REPORTER (else nothing).
inline void CcDiffTag( REPORTER* aReporter, const char* aCode, const KIID* aUuid = nullptr,
                       const wxString& aPin = wxEmptyString,
                       const wxString& aField = wxEmptyString )
{
    if( CC_DIFF_REPORTER* r = dynamic_cast<CC_DIFF_REPORTER*>( aReporter ) )
        r->Tag( aCode, aUuid, aPin, aField );
}

#endif // __EMSCRIPTEN__

#endif // CC_DIFF_FINDING_H
