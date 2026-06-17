/***********************************************************************************
    pRefStick  -  bake a frame-invariant position into vertex Cf

    Created by Marten Blumen

    Companion to PRefToMotion. Produces the PRef pass for UV-less geo with no
    rest/Pref primvar, including point-baked (animated-vertex) motion.

    input 0 : LIVE geo (ReadGeo).         ModifyGeo bakes Cf onto these points.
    input 1 : REFERENCE geo, optional. Wire ReadGeo -> FrameHold @ source frame.
              Its point positions are transferred onto input 0's points by index,
              giving an invariant value (frozen source-frame position) on the live
              screen position -- exactly what PRefToMotion needs.

    use reference OFF (or input 1 unconnected): bakes the LIVE local points. That
    is invariant only for RIGID geo whose track is in the object matrix. For
    point-baked motion, connect input 1 and leave use reference ON.

    Reference points are read with the const point_list() accessor; writable_points
    on a borrowed (other-op) GeometryList copies and faults.

    Render UNLIT (Cf must reach pixels unscaled). Background stays 0 (holdout).
    A position pass reads near-black in the Viewer but the float data is intact;
    judge by hue-invariance across frames.
***********************************************************************************/

#include "DDImage/ModifyGeo.h"
#include "DDImage/Scene.h"
#include "DDImage/GeometryList.h"
#include "DDImage/GeoInfo.h"
#include "DDImage/Attribute.h"
#include "DDImage/Knobs.h"
#include "DDImage/Vector3.h"
#include "DDImage/Vector4.h"

#include <vector>

using namespace DD::Image;

static const char* const CLASS = "pRefStick";

// Help-tab body. Nuke renders HTML in knob labels, so <br>/<b> format cleanly and
// it stays read-only (no editable field). Single-quoted attributes avoid escaping.
static const char* const HELP_TEXT =
    "<b>pRefStick</b><br><br>"
    "Bakes a frame-invariant position into vertex Cf for an UNLIT PRef render that "
    "drives PRefToMotion on UV-less geometry.<br><br>"
    "<b>Inputs</b><br>"
    "&nbsp;&nbsp;<b>0</b>&nbsp;&nbsp; live geo (ReadGeo)<br>"
    "&nbsp;&nbsp;<b>1</b>&nbsp;&nbsp; reference geo: ReadGeo into a FrameHold at the "
    "source frame (optional)<br><br>"
    "<b>use reference ON</b> (input 1 wired) - transfers the frozen reference positions "
    "onto the live geo by index. For point-baked / deforming geometry of constant "
    "topology.<br><br>"
    "<b>use reference OFF</b> - bakes the live local points. For rigid, matrix-driven "
    "geometry.<br><br>"
    "Render the output UNLIT through the classic ScanlineRender, then feed PRefToMotion. "
    "The three frame numbers must agree: the FrameHold frame, the PRefToMotion source "
    "frame, and the texture alignment frame.";

class pRefStick : public ModifyGeo
{
    bool        _useReference;
    const char* _attrib;
    float       _offset;

    std::vector<std::vector<Vector3> > _refPts;

public:
    explicit pRefStick(Node* node)
        : ModifyGeo(node), _useReference(true), _attrib("Cf"), _offset(0.0f) {}

    const char* Class() const override { return CLASS; }

    const char* node_help() const override
    {
        return "Bakes a frame-invariant position into the geometry colour (Cf) for an "
               "UNLIT PRef render driving PRefToMotion on UV-less geo.\n"
               "  use reference ON  + input 1 (ReadGeo -> FrameHold @ source frame): "
               "transfers the frozen reference positions onto the live geo by index "
               "(point-baked / animated-vertex motion).\n"
               "  use reference OFF : bakes live local points (rigid, matrix-driven).\n\n"
               "Created by Marten Blumen";
    }

    int minimum_inputs() const override { return 1; }
    int maximum_inputs() const override { return 2; }

    bool test_input(int input, Op* op) const override
    {
        if (input == 1)
            return op == nullptr || dynamic_cast<GeoOp*>(op) != nullptr;
        return ModifyGeo::test_input(input, op);
    }

    const char* input_label(int input, char*) const override
    {
        return input == 0 ? "" : "ref";
    }

    void knobs(Knob_Callback f) override
    {
        Bool_knob(f, &_useReference, "use_reference", "use reference (input 1)");
        Tooltip(f, "On: bake input 1's frozen positions onto the live geo by index "
                   "(point-baked motion). Off: bake live local points (rigid geo).");

        String_knob(f, &_attrib, "attribute", "colour attribute");
        Tooltip(f, "Vertex-colour attribute to write. Cf is what ScanlineRender renders.");

        Float_knob(f, &_offset, "offset");
        Tooltip(f, "Added to every baked position. Nudge off 0 if a surface point sits "
                   "at local origin (PRefToMotion drops (0,0,0) as background). A uniform "
                   "offset does not affect the nearest-neighbour solve.");

        // subtle credit: small grey italic, no divider rule
        Text_knob(f, "credit", "<i><font color='#888888'>Created by Marten Blumen</font></i>");
        SetFlags(f, Knob::STARTLINE);

        // help on its own tab, read-only formatted text
        Tab_knob(f, "Help");
        Text_knob(f, "help", HELP_TEXT);
        SetFlags(f, Knob::STARTLINE);
    }

    void get_geometry_hash() override
    {
        ModifyGeo::get_geometry_hash();
        geo_hash[Group_Points].append(_useReference);
        geo_hash[Group_Points].append(_attrib ? _attrib : "");
        geo_hash[Group_Points].append(_offset);
        if (_useReference && Op::input(1)) {
            Hash h;
            Op::input(1)->append(h);
            geo_hash[Group_Points].append(h.value());
        }
    }

    void geometry_engine(Scene& scene, GeometryList& out) override
    {
        _refPts.clear();

        if (_useReference) {
            if (GeoOp* refOp = dynamic_cast<GeoOp*>(Op::input(1))) {
                Scene refScene;
                GeometryList refList;
                refOp->get_geometry(refScene, refList);     // safe (framework input)
                const unsigned no = refList.objects();
                _refPts.resize(no);
                for (unsigned o = 0; o < no; ++o) {
                    const PointList* pl = refList[o].point_list();   // const = no copy
                    if (!pl) continue;
                    const unsigned n = (unsigned)pl->size();
                    _refPts[o].resize(n);
                    for (unsigned i = 0; i < n; ++i)
                        _refPts[o][i] = (*pl)[i];
                }
            }
        }

        ModifyGeo::geometry_engine(scene, out);
    }

    void modify_geometry(int obj, Scene& scene, GeometryList& out) override
    {
        PointList* pts = out.writable_points(obj);
        if (!pts) return;
        const unsigned np = (unsigned)pts->size();

        Attribute* cf = out.writable_attribute(obj, Group_Points, _attrib, VECTOR4_ATTRIB);
        if (!cf) return;

        const bool useRef = (_useReference &&
                             obj < (int)_refPts.size() &&
                             _refPts[obj].size() == np);

        for (unsigned i = 0; i < np; ++i) {
            const Vector3& p = useRef ? _refPts[obj][i] : (*pts)[i];
            cf->vector4(i) = Vector4(p.x + _offset,
                                     p.y + _offset,
                                     p.z + _offset,
                                     1.0f);
        }
    }

    static const Description d;
};

static Op* build(Node* node) { return new pRefStick(node); }

const Op::Description pRefStick::d(CLASS, "Geometry/pRefStick", build);
