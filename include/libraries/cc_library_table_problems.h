// Copyright (C) 2026 Circuit Center Inc
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CC_LIBRARY_TABLE_PROBLEMS_H
#define CC_LIBRARY_TABLE_PROBLEMS_H

// The library tables KiCad could not read in the browser build. LIBRARY_MANAGER::loadTables
// skips a table whose parse failed (its type stays UNINITIALIZED) and, on the desktop, logs a
// warning; in the browser that log is a window that holds the frame until it is answered. There
// the warning is not logged and the fact is kept here for the page, which reads it through the
// export kicadLibraryTableProblems. Header only, no wx: the dispatcher face includes it too.

#include <algorithm>
#include <string>
#include <vector>

struct CC_LIBRARY_TABLE_PROBLEM
{
    std::string path;    // the table file, as KiCad named it
    std::string kind;    // "symbol", "footprint" or "designBlock"
    std::string message; // KiCad's own sentence for why it could not be read
};

// The one list, in the order the tables were first found unreadable.
inline std::vector<CC_LIBRARY_TABLE_PROBLEM>& CcLibraryTableProblems()
{
    static std::vector<CC_LIBRARY_TABLE_PROBLEM> problems;
    return problems;
}

// A pass of loadTables over this path: whatever was said about it before no longer stands.
inline void CcLibraryTableForget( const std::string& aPath )
{
    std::vector<CC_LIBRARY_TABLE_PROBLEM>& problems = CcLibraryTableProblems();

    problems.erase( std::remove_if( problems.begin(), problems.end(),
                                    [&]( const CC_LIBRARY_TABLE_PROBLEM& aProblem )
                                    {
                                        return aProblem.path == aPath;
                                    } ),
                    problems.end() );
}

// The table at this path could not be read on this pass.
inline void CcLibraryTableProblem( const std::string& aPath, const std::string& aKind,
                                   const std::string& aMessage )
{
    CcLibraryTableForget( aPath );
    CcLibraryTableProblems().push_back( { aPath, aKind, aMessage } );
}

#endif // CC_LIBRARY_TABLE_PROBLEMS_H
