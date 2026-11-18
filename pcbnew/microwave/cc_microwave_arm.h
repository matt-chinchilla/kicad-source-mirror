// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

/**
 * The microwave tool's armed values (engine run 15, K-MW).
 *
 * The host arms the next microwave placement through the export kicadMicrowaveArm, then
 * presses the shape's action. MICROWAVE_TOOL takes the armed values ONCE, when that
 * action starts (Begin), and reads them in place of WX_TEXT_ENTRY_DIALOG and
 * MWAVE_POLYGONAL_SHAPE_DLG; an armed run places one item and ends, as on Esc. With
 * nothing armed for the shape pressed, KiCad's own dialogs open as before. Any start of a
 * microwave action consumes what was armed, so a value armed for one shape never reaches
 * another shape or a later press.
 *
 * Header only, included under __EMSCRIPTEN__ by the microwave sources and by lane e2's
 * board face; a plain KiCad build never includes it.
 */

#ifndef CC_MICROWAVE_ARM_H
#define CC_MICROWAVE_ARM_H

#ifdef __EMSCRIPTEN__

#include <utility>
#include <vector>

enum class CC_MICROWAVE_SHAPE
{
    NONE,
    LINE,     ///< the inductor (pcbnew.MicrowaveTool.createLine), m_valueMm the track length
    GAP,      ///< m_valueMm the gap size
    STUB,     ///< m_valueMm the stub size
    ARC_STUB, ///< m_valueMm the radius, m_angleDeg the angle
    POLYGON   ///< the polygonal shape: m_points, m_kind, m_sizeXMm, m_sizeYMm
};

struct CC_MICROWAVE_ARM
{
    CC_MICROWAVE_SHAPE m_shape = CC_MICROWAVE_SHAPE::NONE;
    double             m_valueMm = 0.0;
    double             m_angleDeg = 0.0;

    /// The shape description's $COORD pairs as the file writes them (fractions of the
    /// scale), what MWAVE_POLYGONAL_SHAPE_DLG::ReadDataShapeDescr reads into g_PolyEdges.
    std::vector<std::pair<double, double>> m_points;

    /// MWAVE_POLYGONAL_SHAPE_DLG's radio box: 0 normal (plain), 1 symmetrical, 2 mirrored.
    int m_kind = 0;

    /// The shape's X and Y scale in millimeters (the file's XScale and YScale in its Unit).
    double m_sizeXMm = 0.0;
    double m_sizeYMm = 0.0;
};

namespace cc_microwave
{

inline CC_MICROWAVE_ARM& pendingArm()
{
    static CC_MICROWAVE_ARM s_pending;
    return s_pending;
}

inline CC_MICROWAVE_ARM& activeArm()
{
    static CC_MICROWAVE_ARM s_active;
    return s_active;
}

inline int& activeRun()
{
    static int s_run = 0;
    return s_run;
}

/// The host's arm: replaces whatever was armed before.
inline void Arm( const CC_MICROWAVE_ARM& aArm )
{
    pendingArm() = aArm;
}

/// True while something is armed and not yet taken by a microwave action.
inline bool Armed()
{
    return pendingArm().m_shape != CC_MICROWAVE_SHAPE::NONE;
}

/**
 * A microwave action starts for aShape: the pending arm is consumed. Answers the run's id
 * (above 0) when it named aShape, its values then readable through Active until End; 0 when
 * nothing was armed for this shape (KiCad's dialogs).
 */
inline int Begin( CC_MICROWAVE_SHAPE aShape )
{
    CC_MICROWAVE_ARM taken = pendingArm();
    pendingArm() = CC_MICROWAVE_ARM();

    if( aShape == CC_MICROWAVE_SHAPE::NONE || taken.m_shape != aShape )
        return 0;

    activeArm() = taken;
    activeRun() += 1;

    if( activeRun() <= 0 )
        activeRun() = 1;

    return activeRun();
}

/// The armed values of the running action of aShape, or nullptr (KiCad's dialogs).
inline const CC_MICROWAVE_ARM* Active( CC_MICROWAVE_SHAPE aShape )
{
    if( aShape == CC_MICROWAVE_SHAPE::NONE || activeArm().m_shape != aShape )
        return nullptr;

    return &activeArm();
}

/// The run aRun ended (placed once, Esc, another tool): its values are dropped. A later
/// run's values are left alone.
inline void End( int aRun )
{
    if( aRun > 0 && aRun == activeRun() )
        activeArm() = CC_MICROWAVE_ARM();
}

} // namespace cc_microwave

#endif // __EMSCRIPTEN__

#endif // CC_MICROWAVE_ARM_H
