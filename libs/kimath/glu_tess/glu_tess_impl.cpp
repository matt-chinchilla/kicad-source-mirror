/**
 * GLU Tesselator implementation using Mapbox earcut
 *
 * Provides GLU tessellation API for WebGL/WASM builds.
 * Uses earcut.hpp for polygon triangulation instead of native GLU library.
 *
 * Modified by Circuit Center Inc on 2026-11-18: the tessellator keeps GLU's contract for every
 * call KiCad makes. The begin and end callbacks (and their DATA forms) frame each primitive,
 * GLU_TESS_BOUNDARY_ONLY gives the region's boundary as GL_LINE_LOOP contours, and the winding
 * rule chooses the region of a polygon of several contours: Clipper2 (KiCad's own polygon
 * library) resolves the contours under the rule, each crossing it finds reaches the combine
 * callback as GLU's own crossings do, and earcut triangulates each resolved region. A polygon of
 * one contour in triangle mode keeps the earcut triangulation it always had. Triangles and
 * exterior contours turn counterclockwise about the normal, holes clockwise, as GLU's do.
 * Before this, nothing called the begin and end callbacks and the boundary property was
 * ignored, so a caller that collects between them (VRML_LAYER, behind the VRML export and the
 * IDF model reader) got nothing.
 */

#include "earcut.hpp"
#include <clipper2/clipper.engine.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

// GL types
typedef double GLdouble;
typedef float GLfloat;
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef void GLvoid;
typedef void (*_GLUfuncptr)(void);

// GLU constants
#define GLU_TESS_BEGIN          100100
#define GLU_TESS_VERTEX         100101
#define GLU_TESS_END            100102
#define GLU_TESS_ERROR          100103
#define GLU_TESS_EDGE_FLAG      100104
#define GLU_TESS_COMBINE        100105
#define GLU_TESS_BEGIN_DATA     100106
#define GLU_TESS_VERTEX_DATA    100107
#define GLU_TESS_END_DATA       100108
#define GLU_TESS_ERROR_DATA     100109
#define GLU_TESS_EDGE_FLAG_DATA 100110
#define GLU_TESS_COMBINE_DATA   100111

#define GLU_TESS_WINDING_RULE       100140
#define GLU_TESS_WINDING_ODD        100130
#define GLU_TESS_WINDING_NONZERO    100131
#define GLU_TESS_WINDING_POSITIVE   100132
#define GLU_TESS_WINDING_NEGATIVE   100133
#define GLU_TESS_WINDING_ABS_GEQ_TWO 100134

#ifndef GLU_TESS_BOUNDARY_ONLY
#define GLU_TESS_BOUNDARY_ONLY      100141
#endif
#ifndef GLU_TESS_TOLERANCE
#define GLU_TESS_TOLERANCE          100142
#endif
#ifndef GLU_TESS_COORD_TOO_LARGE
#define GLU_TESS_COORD_TOO_LARGE    100155
#endif
#ifndef GLU_TESS_NEED_COMBINE_CALLBACK
#define GLU_TESS_NEED_COMBINE_CALLBACK 100156
#endif

#ifndef GL_TRUE
#define GL_TRUE 1
#endif
#ifndef GL_FALSE
#define GL_FALSE 0
#endif
#ifndef GL_LINE_LOOP
#define GL_LINE_LOOP 0x0002
#endif
#ifndef GL_TRIANGLES
#define GL_TRIANGLES 0x0004
#endif

// Vertex data stored during tessellation
struct TessVertex
{
    std::array<GLdouble, 3> coords;
    void* userData;  // The data pointer passed to gluTessVertex
};

struct GLUtesselator
{
    // Callbacks
    void (*vertexCallback)(void* vertex) = nullptr;
    void (*vertexDataCallback)(void* vertex, void* userData) = nullptr;
    void (*combineCallback)(GLdouble coords[3], void* vertex_data[4],
                            GLfloat weight[4], void** dataOut) = nullptr;
    void (*combineDataCallback)(GLdouble coords[3], void* vertex_data[4],
                                GLfloat weight[4], void** dataOut, void* userData) = nullptr;
    void (*edgeFlagCallback)(GLboolean flag) = nullptr;
    void (*edgeFlagDataCallback)(GLboolean flag, void* userData) = nullptr;
    void (*errorCallback)(GLenum error) = nullptr;
    void (*errorDataCallback)(GLenum error, void* userData) = nullptr;
    void (*beginCallback)(GLenum type) = nullptr;
    void (*beginDataCallback)(GLenum type, void* userData) = nullptr;
    void (*endCallback)() = nullptr;
    void (*endDataCallback)(void* userData) = nullptr;

    // Contour data - each contour is a list of vertices
    std::vector<std::vector<TessVertex>> contours;
    std::vector<TessVertex>* currentContour = nullptr;

    // User data passed to gluTessBeginPolygon
    void* polygonUserData = nullptr;

    // Properties (GLU's defaults)
    GLenum   windingRule = GLU_TESS_WINDING_ODD;
    bool     boundaryOnly = false;
    GLdouble tolerance = 0.0;
    GLdouble normal[3] = { 0.0, 0.0, 0.0 };
};

namespace
{

using Point = std::array<double, 2>;

// The output side: the DATA form of a callback when one is registered, else the plain form.
void callBegin( GLUtesselator* t, GLenum type )
{
    if( t->beginDataCallback )
        t->beginDataCallback( type, t->polygonUserData );
    else if( t->beginCallback )
        t->beginCallback( type );
}


void callEnd( GLUtesselator* t )
{
    if( t->endDataCallback )
        t->endDataCallback( t->polygonUserData );
    else if( t->endCallback )
        t->endCallback();
}


void callVertex( GLUtesselator* t, void* data )
{
    if( t->vertexDataCallback )
        t->vertexDataCallback( data, t->polygonUserData );
    else if( t->vertexCallback )
        t->vertexCallback( data );
}


bool takesEdgeFlags( const GLUtesselator* t )
{
    return t->edgeFlagDataCallback || t->edgeFlagCallback;
}


void callEdgeFlag( GLUtesselator* t, bool flag )
{
    if( t->edgeFlagDataCallback )
        t->edgeFlagDataCallback( flag ? GL_TRUE : GL_FALSE, t->polygonUserData );
    else if( t->edgeFlagCallback )
        t->edgeFlagCallback( flag ? GL_TRUE : GL_FALSE );
}


void callError( GLUtesselator* t, GLenum error )
{
    if( t->errorDataCallback )
        t->errorDataCallback( error, t->polygonUserData );
    else if( t->errorCallback )
        t->errorCallback( error );
}


bool combine( GLUtesselator* t, GLdouble coords[3], void* data[4], GLfloat weight[4],
              void** out )
{
    *out = nullptr;

    if( t->combineDataCallback )
        t->combineDataCallback( coords, data, weight, out, t->polygonUserData );
    else if( t->combineCallback )
        t->combineCallback( coords, data, weight, out );

    return *out != nullptr;
}


bool ruleAccepts( GLenum rule, int winding )
{
    switch( rule )
    {
    case GLU_TESS_WINDING_ODD:         return ( winding & 1 ) != 0;
    case GLU_TESS_WINDING_NONZERO:     return winding != 0;
    case GLU_TESS_WINDING_POSITIVE:    return winding > 0;
    case GLU_TESS_WINDING_NEGATIVE:    return winding < 0;
    case GLU_TESS_WINDING_ABS_GEQ_TWO: return winding >= 2 || winding <= -2;
    default:                           return false;
    }
}


double signedArea( const std::vector<TessVertex>& c )
{
    double area = 0.0;

    for( size_t i = 0, j = c.size() - 1; i < c.size(); j = i++ )
        area += ( c[j].coords[0] - c[i].coords[0] ) * ( c[j].coords[1] + c[i].coords[1] );

    return area / 2.0;
}


// GLU works in the plane of the normal and turns everything counterclockwise about it. KiCad's
// polygons lie in z = 0 and give the normal (0, 0, 1) or none; with none GLU computes it and
// turns it so the polygon's signed area is positive (its CheckOrientation), which here is the
// sign of the area in the xy plane. The frame is the xy plane with y negated when the normal
// points down.
double frameSign( const GLUtesselator* t )
{
    if( t->normal[2] > 0.0 )
        return 1.0;

    if( t->normal[2] < 0.0 )
        return -1.0;

    double area = 0.0;

    for( const std::vector<TessVertex>& c : t->contours )
    {
        if( c.size() >= 3 )
            area += signedArea( c );
    }

    return area < 0.0 ? -1.0 : 1.0;
}


struct OutTri
{
    void* data[3];
    bool  boundary[3];  // the edge from this vertex to the next one lies on the polygon's boundary
};


// Triangulates one region (its first ring the outline, the rest its holes) with earcut, and
// adds its triangles turned counterclockwise in the frame, each edge marked when it lies on a
// ring. 'rings' are the coordinates earcut reads, 'frame' and 'data' each ring vertex's frame
// point and data pointer in earcut's flat order.
void addTriangles( const std::vector<std::vector<Point>>& rings, const std::vector<Point>& frame,
                   const std::vector<void*>& data, std::vector<OutTri>& out )
{
    const std::vector<uint32_t> indices = mapbox::earcut<uint32_t>( rings );

    std::vector<uint32_t> next( frame.size() );
    uint32_t start = 0;

    for( const std::vector<Point>& ring : rings )
    {
        const uint32_t n = static_cast<uint32_t>( ring.size() );

        for( uint32_t i = 0; i < n; ++i )
            next[start + i] = start + ( i + 1 ) % n;

        start += n;
    }

    auto onRing = [&]( uint32_t a, uint32_t b )
    {
        return next[a] == b || next[b] == a;
    };

    for( size_t i = 0; i + 2 < indices.size(); i += 3 )
    {
        uint32_t v[3] = { indices[i], indices[i + 1], indices[i + 2] };
        const Point& p0 = frame[v[0]];
        const Point& p1 = frame[v[1]];
        const Point& p2 = frame[v[2]];
        const double cross = ( p1[0] - p0[0] ) * ( p2[1] - p0[1] )
                             - ( p1[1] - p0[1] ) * ( p2[0] - p0[0] );

        if( cross < 0.0 )
            std::swap( v[1], v[2] );

        OutTri tri;

        for( int k = 0; k < 3; ++k )
        {
            tri.data[k] = data[v[k]];
            tri.boundary[k] = onRing( v[k], v[( k + 1 ) % 3] );
        }

        out.push_back( tri );
    }
}


// GLU's renderer when it is asked for triangles: one GL_TRIANGLES primitive and, when an edge
// flag callback is registered, the flag before each vertex whenever it changes.
void emitTriangles( GLUtesselator* t, const std::vector<OutTri>& tris )
{
    if( tris.empty() )
        return;

    const bool flags = takesEdgeFlags( t );
    int        state = -1;

    callBegin( t, GL_TRIANGLES );

    for( const OutTri& tri : tris )
    {
        for( int k = 0; k < 3; ++k )
        {
            if( flags && state != static_cast<int>( tri.boundary[k] ) )
            {
                state = tri.boundary[k];
                callEdgeFlag( t, tri.boundary[k] );
            }

            callVertex( t, tri.data[k] );
        }
    }

    callEnd( t );
}


// One contour in triangle mode: earcut on the contour as given, as this shim always did (the
// WebGL canvas fills every polygon it draws this way), kept or dropped by the winding rule.
void tessellateOneContour( GLUtesselator* t, double sign )
{
    const std::vector<TessVertex>& c = t->contours[0];

    if( c.size() < 3 )
        return;

    if( !ruleAccepts( t->windingRule, signedArea( c ) * sign < 0.0 ? -1 : 1 ) )
        return;

    std::vector<std::vector<Point>> rings( 1 );
    std::vector<Point>              frame;
    std::vector<void*>              data;

    for( const TessVertex& v : c )
    {
        rings[0].push_back( { v.coords[0], v.coords[1] } );
        frame.push_back( { v.coords[0], sign * v.coords[1] } );
        data.push_back( v.userData );
    }

    std::vector<OutTri> tris;
    addTriangles( rings, frame, data, tris );
    emitTriangles( t, tris );
}


struct PointKey
{
    size_t operator()( const std::pair<int64_t, int64_t>& p ) const
    {
        return std::hash<uint64_t>()( static_cast<uint64_t>( p.first ) * 0x9E3779B97F4A7C15ULL
                                      ^ static_cast<uint64_t>( p.second ) );
    }
};


// Several contours, or a boundary: the region the winding rule chooses, resolved by Clipper2 in
// integer frame coordinates. Each output point carries in z the number (plus one) of the vertex
// it is: an input vertex, or one the combine callback made at a crossing.
void tessellateResolved( GLUtesselator* t, double sign )
{
    Clipper2Lib::FillRule rule;

    switch( t->windingRule )
    {
    case GLU_TESS_WINDING_ODD:      rule = Clipper2Lib::FillRule::EvenOdd;  break;
    case GLU_TESS_WINDING_NONZERO:  rule = Clipper2Lib::FillRule::NonZero;  break;
    case GLU_TESS_WINDING_POSITIVE: rule = Clipper2Lib::FillRule::Positive; break;
    case GLU_TESS_WINDING_NEGATIVE: rule = Clipper2Lib::FillRule::Negative; break;
    default:
        // GLU_TESS_WINDING_ABS_GEQ_TWO has no Clipper2 fill rule, and no KiCad caller sets it.
        return;
    }

    struct PolyVertex
    {
        double x, y, z;
        void*  data;
    };

    std::vector<PolyVertex> verts;
    double minX = 0.0, minY = 0.0, maxX = 0.0, maxY = 0.0;

    for( const std::vector<TessVertex>& c : t->contours )
    {
        for( const TessVertex& v : c )
        {
            if( verts.empty() )
            {
                minX = maxX = v.coords[0];
                minY = maxY = v.coords[1];
            }

            minX = std::min( minX, v.coords[0] );
            maxX = std::max( maxX, v.coords[0] );
            minY = std::min( minY, v.coords[1] );
            maxY = std::max( maxY, v.coords[1] );
            verts.push_back( { v.coords[0], v.coords[1], v.coords[2], v.userData } );
        }
    }

    if( verts.empty() )
        return;

    // The frame: centered on the input, scaled by a power of two so the farthest point lies
    // between 2^30 and 2^31 units from the center, the range of KiCad's own integer geometry.
    const double cx = ( minX + maxX ) / 2.0;
    const double cy = ( minY + maxY ) / 2.0;
    const double extent = std::max( maxX - minX, maxY - minY ) / 2.0;

    if( !std::isfinite( extent ) )
    {
        callError( t, GLU_TESS_COORD_TOO_LARGE );
        return;
    }

    const double scale = extent > 0.0 ? std::ldexp( 1.0, 30 - std::ilogb( extent ) ) : 1.0;

    using Key = std::pair<int64_t, int64_t>;
    std::unordered_map<Key, int64_t, PointKey> at;   // the first vertex at each frame point
    Clipper2Lib::Paths64 paths;
    int64_t id = 0;

    for( const std::vector<TessVertex>& c : t->contours )
    {
        Clipper2Lib::Path64 path;

        for( size_t i = 0; i < c.size(); ++i, ++id )
        {
            const int64_t x = std::llround( ( verts[id].x - cx ) * scale );
            const int64_t y = std::llround( sign * ( verts[id].y - cy ) * scale );

            path.emplace_back( x, y, id + 1 );
            at.emplace( Key( x, y ), id );
        }

        paths.push_back( std::move( path ) );
    }

    auto known = [&]( int64_t z )
    {
        return z >= 1 && z <= static_cast<int64_t>( verts.size() );
    };

    bool needCombine = false;

    auto onCrossing = [&]( const Clipper2Lib::Point64& e1b, const Clipper2Lib::Point64& e1t,
                           const Clipper2Lib::Point64& e2b, const Clipper2Lib::Point64& e2t,
                           Clipper2Lib::Point64& ip )
    {
        if( known( ip.z ) )
            return;     // Clipper2 matched the end of an edge

        const Clipper2Lib::Point64* ends[4] = { &e1b, &e1t, &e2b, &e2t };

        for( const Clipper2Lib::Point64* e : ends )
        {
            if( e->x == ip.x && e->y == ip.y && known( e->z ) )
            {
                ip.z = e->z;
                return;
            }
        }

        auto found = at.find( Key( ip.x, ip.y ) );

        if( found != at.end() )
        {
            ip.z = found->second + 1;
            return;
        }

        // A crossing: GLU makes a new vertex there through the combine callback, from the ends
        // of the two edges weighted by where the crossing lies along each.
        double w[4];

        for( int e = 0; e < 2; ++e )
        {
            const Clipper2Lib::Point64& a = *ends[2 * e];
            const Clipper2Lib::Point64& b = *ends[2 * e + 1];
            const double dx = double( b.x - a.x );
            const double dy = double( b.y - a.y );
            const double len2 = dx * dx + dy * dy;
            double along = 0.5;

            if( len2 > 0.0 )
            {
                along = ( double( ip.x - a.x ) * dx + double( ip.y - a.y ) * dy ) / len2;
                along = std::clamp( along, 0.0, 1.0 );
            }

            w[2 * e] = 0.5 * ( 1.0 - along );
            w[2 * e + 1] = 0.5 * along;
        }

        void*   data[4] = { nullptr, nullptr, nullptr, nullptr };
        GLfloat weight[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
        double  total = 0.0;
        double  z = 0.0;
        int     n = 0;

        for( int k = 0; k < 4; ++k )
        {
            if( known( ends[k]->z ) )
                total += w[k];
        }

        for( int k = 0; k < 4; ++k )
        {
            if( !known( ends[k]->z ) )
                continue;

            const PolyVertex& v = verts[ends[k]->z - 1];
            const double      share = total > 0.0 ? w[k] / total : 0.0;

            data[n] = v.data;
            weight[n] = static_cast<GLfloat>( share );
            z += share * v.z;
            ++n;
        }

        GLdouble coords[3] = { double( ip.x ) / scale + cx, sign * double( ip.y ) / scale + cy,
                               z };
        void* made = nullptr;

        if( n == 0 || !combine( t, coords, data, weight, &made ) )
        {
            needCombine = true;
            return;
        }

        verts.push_back( { coords[0], coords[1], coords[2], made } );
        ip.z = static_cast<int64_t>( verts.size() );
        at.emplace( Key( ip.x, ip.y ), ip.z - 1 );
    };

    Clipper2Lib::Clipper64   clipper;
    Clipper2Lib::PolyTree64 tree;

    clipper.PreserveCollinear( true );
    clipper.SetZCallback( onCrossing );
    clipper.AddSubject( paths );

    const bool ok = clipper.Execute( Clipper2Lib::ClipType::Union, rule, tree );

    if( needCombine )
    {
        callError( t, GLU_TESS_NEED_COMBINE_CALLBACK );
        return;
    }

    if( !ok )
    {
        callError( t, GLU_TESS_COORD_TOO_LARGE );
        return;
    }

    // Every region: an outline (an odd level of the tree) and its holes (its children).
    std::vector<const Clipper2Lib::PolyPath64*> outlines;
    std::vector<const Clipper2Lib::PolyPath64*> pending = { &tree };

    while( !pending.empty() )
    {
        const Clipper2Lib::PolyPath64* node = pending.back();
        pending.pop_back();

        for( const auto& child : *node )
        {
            if( !child->IsHole() )
                outlines.push_back( child.get() );

            pending.push_back( child.get() );
        }
    }

    // A path's vertices, turned counterclockwise in the frame for an outline and clockwise for a
    // hole, as GLU gives its exterior and interior contours.
    auto ringOf = [&]( const Clipper2Lib::Path64& path, bool hole, std::vector<int64_t>& ids )
    {
        ids.clear();

        for( const Clipper2Lib::Point64& p : path )
        {
            int64_t v = p.z;

            if( !known( v ) )
            {
                auto found = at.find( Key( p.x, p.y ) );

                if( found == at.end() )
                    return false;

                v = found->second + 1;
            }

            ids.push_back( v - 1 );
        }

        if( ( Clipper2Lib::Area( path ) < 0.0 ) != hole )
            std::reverse( ids.begin(), ids.end() );

        return true;
    };

    std::vector<std::vector<std::vector<int64_t>>> regions;

    for( const Clipper2Lib::PolyPath64* outline : outlines )
    {
        std::vector<std::vector<int64_t>> region( 1 );

        if( outline->Polygon().size() < 3 || !ringOf( outline->Polygon(), false, region[0] ) )
            continue;

        for( const auto& hole : *outline )
        {
            std::vector<int64_t> ids;

            if( hole->Polygon().size() >= 3 && ringOf( hole->Polygon(), true, ids ) )
                region.push_back( std::move( ids ) );
        }

        regions.push_back( std::move( region ) );
    }

    auto frameOf = [&]( int64_t v ) -> Point
    {
        return { ( verts[v].x - cx ) * scale, sign * ( verts[v].y - cy ) * scale };
    };

    if( t->boundaryOnly )
    {
        for( const std::vector<std::vector<int64_t>>& region : regions )
        {
            for( const std::vector<int64_t>& ring : region )
            {
                callBegin( t, GL_LINE_LOOP );

                for( int64_t v : ring )
                    callVertex( t, verts[v].data );

                callEnd( t );
            }
        }

        return;
    }

    std::vector<OutTri> tris;

    for( const std::vector<std::vector<int64_t>>& region : regions )
    {
        std::vector<std::vector<Point>> rings;
        std::vector<Point>              frame;
        std::vector<void*>              data;

        for( const std::vector<int64_t>& ring : region )
        {
            rings.emplace_back();

            for( int64_t v : ring )
            {
                rings.back().push_back( frameOf( v ) );
                frame.push_back( frameOf( v ) );
                data.push_back( verts[v].data );
            }
        }

        addTriangles( rings, frame, data, tris );
    }

    emitTriangles( t, tris );
}

} // namespace

extern "C" {

GLUtesselator* gluNewTess()
{
    return new GLUtesselator();
}

void gluDeleteTess(GLUtesselator* tess)
{
    delete tess;
}

void gluTessCallback(GLUtesselator* tess, GLenum which, _GLUfuncptr fn)
{
    if (!tess) return;

    switch (which) {
        case GLU_TESS_VERTEX:
            tess->vertexCallback = (void(*)(void*))fn;
            break;
        case GLU_TESS_VERTEX_DATA:
            tess->vertexDataCallback = (void(*)(void*, void*))fn;
            break;
        case GLU_TESS_COMBINE:
            tess->combineCallback = (void(*)(GLdouble[3], void*[4], GLfloat[4], void**))fn;
            break;
        case GLU_TESS_COMBINE_DATA:
            tess->combineDataCallback = (void(*)(GLdouble[3], void*[4], GLfloat[4], void**, void*))fn;
            break;
        case GLU_TESS_EDGE_FLAG:
            tess->edgeFlagCallback = (void(*)(GLboolean))fn;
            break;
        case GLU_TESS_EDGE_FLAG_DATA:
            tess->edgeFlagDataCallback = (void(*)(GLboolean, void*))fn;
            break;
        case GLU_TESS_ERROR:
            tess->errorCallback = (void(*)(GLenum))fn;
            break;
        case GLU_TESS_ERROR_DATA:
            tess->errorDataCallback = (void(*)(GLenum, void*))fn;
            break;
        case GLU_TESS_BEGIN:
            tess->beginCallback = (void(*)(GLenum))fn;
            break;
        case GLU_TESS_BEGIN_DATA:
            tess->beginDataCallback = (void(*)(GLenum, void*))fn;
            break;
        case GLU_TESS_END:
            tess->endCallback = (void(*)())fn;
            break;
        case GLU_TESS_END_DATA:
            tess->endDataCallback = (void(*)(void*))fn;
            break;
    }
}

void gluTessProperty(GLUtesselator* tess, GLenum which, GLdouble value)
{
    if (!tess) return;

    if (which == GLU_TESS_WINDING_RULE)
        tess->windingRule = static_cast<GLenum>(value);
    else if (which == GLU_TESS_BOUNDARY_ONLY)
        tess->boundaryOnly = value != 0.0;
    else if (which == GLU_TESS_TOLERANCE)
        tess->tolerance = value;
}

void gluGetTessProperty(GLUtesselator* tess, GLenum which, GLdouble* value)
{
    if (!tess || !value) return;

    if (which == GLU_TESS_WINDING_RULE)
        *value = static_cast<GLdouble>(tess->windingRule);
    else if (which == GLU_TESS_BOUNDARY_ONLY)
        *value = tess->boundaryOnly ? GL_TRUE : GL_FALSE;
    else if (which == GLU_TESS_TOLERANCE)
        *value = tess->tolerance;
}

void gluTessNormal(GLUtesselator* tess, GLdouble x, GLdouble y, GLdouble z)
{
    // Kept for the frame (frameSign); the polygon is read in the XY plane.
    if (!tess) return;

    tess->normal[0] = x;
    tess->normal[1] = y;
    tess->normal[2] = z;
}

void gluTessBeginPolygon(GLUtesselator* tess, void* userData)
{
    if (!tess) return;

    tess->contours.clear();
    tess->currentContour = nullptr;
    tess->polygonUserData = userData;
}

void gluTessBeginContour(GLUtesselator* tess)
{
    if (!tess) return;

    tess->contours.emplace_back();
    tess->currentContour = &tess->contours.back();
}

void gluTessVertex(GLUtesselator* tess, GLdouble coords[3], void* data)
{
    if (!tess || !tess->currentContour) return;

    TessVertex v;
    v.coords = {coords[0], coords[1], coords[2]};
    v.userData = data;
    tess->currentContour->push_back(v);
}

void gluTessEndContour(GLUtesselator* tess)
{
    if (!tess) return;
    tess->currentContour = nullptr;
}

void gluTessEndPolygon(GLUtesselator* tess)
{
    if (!tess) return;

    tess->currentContour = nullptr;

    if (tess->contours.empty())
        return;

    const double sign = frameSign(tess);

    if (!tess->boundaryOnly && tess->contours.size() == 1)
        tessellateOneContour(tess, sign);
    else
        tessellateResolved(tess, sign);
}

const unsigned char* gluErrorString(GLenum error)
{
    static const unsigned char errStr[] = "GLU tesselator error";
    (void)error;
    return errStr;
}

} // extern "C"
