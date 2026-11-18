// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_HOST_BLOCK_LAYOUT_H
#define CC_HOST_BLOCK_LAYOUT_H

#ifdef __EMSCRIPTEN__

#include <vector>
#include <kiid.h>
#include <wx/string.h>

/**
 * K-B2 (engine run 15, kicadDesignBlockApplyLayout): the host's reporter for
 * PCB_CONTROL::ApplyDesignBlockLayout. While one is set the tool shows no message box, no
 * progress window and no info bar: its summary goes into the record below (a failure per group
 * it could not lay out, with KiCad's own reason) and its info bar lines into lines.
 */
struct CC_HOST_BLOCK_LAYOUT_FAILURE
{
    KIID     uuid;
    wxString reason;
};

struct CC_HOST_BLOCK_LAYOUT_REPORT
{
    int                                       applied = 0;
    int                                       linked = 0;
    int                                       skippedNoLink = 0;
    std::vector<CC_HOST_BLOCK_LAYOUT_FAILURE> failures;
    std::vector<wxString>                     lines;
};

inline CC_HOST_BLOCK_LAYOUT_REPORT*& ccHostBlockLayoutReportSlot()
{
    static CC_HOST_BLOCK_LAYOUT_REPORT* s_report = nullptr;
    return s_report;
}

/**
 * The reporter set by the host for the current run, or nullptr (KiCad's own windows then show).
 */
inline CC_HOST_BLOCK_LAYOUT_REPORT* ccHostBlockLayoutReport()
{
    return ccHostBlockLayoutReportSlot();
}

/**
 * Sets the reporter for one run of the tool and clears it again when it goes out of scope.
 */
struct CC_HOST_BLOCK_LAYOUT_SCOPE
{
    explicit CC_HOST_BLOCK_LAYOUT_SCOPE( CC_HOST_BLOCK_LAYOUT_REPORT* aReport )
    {
        ccHostBlockLayoutReportSlot() = aReport;
    }

    ~CC_HOST_BLOCK_LAYOUT_SCOPE() { ccHostBlockLayoutReportSlot() = nullptr; }
};

/**
 * An info bar line of the tool: taken into the host's lines when a reporter is set (true), else
 * false and the caller shows KiCad's info bar.
 */
inline bool ccHostBlockLayoutLine( const wxString& aLine )
{
    if( CC_HOST_BLOCK_LAYOUT_REPORT* report = ccHostBlockLayoutReport() )
    {
        report->lines.push_back( aLine );
        return true;
    }

    return false;
}

#endif // __EMSCRIPTEN__

#endif // CC_HOST_BLOCK_LAYOUT_H
