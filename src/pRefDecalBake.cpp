/***********************************************************************************
    pRefDecalBake  -  bake a camera-projected decal into vertex Cf

    Created by Marten Blumen

    The "proper Pref" way to stick a decal (blood splat, bullet hole, scorch) to
    UV-less, deforming geometry: at a chosen placement frame, project the decal
    onto the surface through the render camera, cull back-faces, and bake the
    sampled colour onto the vertices. The colour is frozen and transferred onto
    the live (deforming) geometry by index - exactly pRefStick's mechanism - so
    rendering the live geo UNLIT welds the decal to the surface. Deformation,
    rotation, occlusion and reveal-on-rotate are then handled by ScanlineRender.

    input 0 : LIVE geo (ReadGeo).            ModifyGeo bakes Cf onto these points.
    input 1 : REFERENCE geo: ReadGeo -> FrameHold @ placement frame. Its
              placement-frame world positions/normals drive the projection.
    input 2 : CAMERA: the render camera -> FrameHold @ placement frame.
    input 3 : DECAL: RGBA element, positioned in screen space on the plate at the
              placement frame.

    Topology must match between inputs 0 and 1 (transfer is by index), as in
    pRefStick. Resolution of the stuck decal is the mesh density.

    VERIFY (cannot be compile-checked here):
      - mode = uv_debug bakes the projected 0..1 screen coord as R,G. Render at
        the placement frame; it should read as a screen-locked gradient on the geo
        and should STICK as the surface deforms. If it is mirrored or offset, flip
        the sign / aspect in project() (one block, clearly marked).
      - lens read uses cam->knob("focal"/"haperture"); the N attribute read and
        the other two spots to confirm against your NDK headers.
***********************************************************************************/

#include "DDImage/ModifyGeo.h"
#include "DDImage/Scene.h"
#include "DDImage/GeometryList.h"
#include "DDImage/GeoInfo.h"
#include "DDImage/Attribute.h"
#include "DDImage/Knobs.h"
#include "DDImage/Vector3.h"
#include "DDImage/Vector4.h"
#include "DDImage/Matrix4.h"
#include "DDImage/CameraOp.h"
#include "DDImage/Iop.h"
#include "DDImage/Tile.h"

#include <vector>
#include <algorithm>

using namespace DD::Image;

static const char* const CLASS = "pRefDecalBake";

static const char* const mode_names[] = { "decal", "uv_debug", "facing_debug", nullptr };

class pRefDecalBake : public ModifyGeo
{
    int         _mode;
    bool        _useFacing;
    float       _facing;
    const char* _attrib;
    float       _opacity;

    // per-object baked colour + validity, captured at the placement frame
    std::vector<std::vector<Vector4> > _baked;

public:
    explicit pRefDecalBake(Node* node)
        : ModifyGeo(node), _mode(0), _useFacing(true), _facing(0.0f),
          _attrib("Cf"), _opacity(1.0f) {}

    const char* Class() const override { return CLASS; }

    const char* node_help() const override
    {
        return "Projects a decal onto geometry through the camera at a placement "
               "frame and bakes it into vertex Cf, frozen and transferred to the "
               "live deforming geo by index. Render UNLIT to weld the decal to the "
               "surface.\n\nCreated by Marten Blumen";
    }

    int minimum_inputs() const override { return 4; }
    int maximum_inputs() const override { return 4; }

    bool test_input(int input, Op* op) const override
    {
        switch (input) {
            case 0: return ModifyGeo::test_input(0, op);
            case 1: return op == nullptr || dynamic_cast<GeoOp*>(op)   != nullptr;
            case 2: return op == nullptr || dynamic_cast<CameraOp*>(op) != nullptr;
            case 3: return op == nullptr || dynamic_cast<Iop*>(op)      != nullptr;
        }
        return false;
    }

    const char* input_label(int input, char*) const override
    {
        switch (input) {
            case 1: return "ref";
            case 2: return "cam";
            case 3: return "decal";
        }
        return "";
    }

    void knobs(Knob_Callback f) override
    {
        Enumeration_knob(f, &_mode, mode_names, "mode");
        Tooltip(f, "decal: bake the projected decal. uv_debug: bake the projected "
                   "0..1 screen coord (verify the projection). facing_debug: bake "
                   "the camera-facing dot.");

        Bool_knob(f, &_useFacing, "use_facing", "cull back-faces");
        Tooltip(f, "Only bake onto vertices facing the camera at the placement frame "
                   "(needs an N attribute on the reference geo).");

        Float_knob(f, &_facing, "facing", "facing threshold");
        Tooltip(f, "Min dot(normal, toCamera) to receive the decal. 0 = front hemisphere.");

        String_knob(f, &_attrib, "attribute", "colour attribute");
        Tooltip(f, "Vertex-colour attribute to write. Cf is what ScanlineRender renders.");

        Float_knob(f, &_opacity, "opacity");
        Tooltip(f, "Scales the baked decal alpha and colour (premultiplied).");

        Divider(f, "Created by Marten Blumen");
    }

    void get_geometry_hash() override
    {
        ModifyGeo::get_geometry_hash();
        geo_hash[Group_Points].append(_mode);
        geo_hash[Group_Points].append(_useFacing);
        geo_hash[Group_Points].append(_facing);
        geo_hash[Group_Points].append(_attrib ? _attrib : "");
        geo_hash[Group_Points].append(_opacity);
        for (int i = 1; i <= 3; ++i)
            if (Op::input(i)) { Hash h; Op::input(i)->append(h); geo_hash[Group_Points].append(h.value()); }
    }

    // ---- nearest-sample the decal (clamped) ----
    static Vector4 sampleDecal(Tile& t, Channel rC, Channel gC, Channel bC, Channel aC, float px, float py)
    {
        int x = (int)std::floor(px + 0.5f);
        int y = (int)std::floor(py + 0.5f);
        x = std::max(t.x(), std::min(x, t.r() - 1));
        y = std::max(t.y(), std::min(y, t.t() - 1));
        return Vector4(t[rC][y][x], t[gC][y][x], t[bC][y][x], t[aC][y][x]);
    }

    void geometry_engine(Scene& scene, GeometryList& out) override
    {
        _baked.clear();

        GeoOp*    refOp = dynamic_cast<GeoOp*>(Op::input(1));
        CameraOp* cam   = dynamic_cast<CameraOp*>(Op::input(2));
        Iop*      dec   = dynamic_cast<Iop*>(Op::input(3));

        if (refOp && cam && dec) {
            cam->validate(true);
            dec->validate(true);

            // --- camera + format ---
            const Matrix4 camWorld = cam->matrix();                // returns DD::Image::Matrix4
            Matrix4 view   = camWorld.inverse();                   // world -> camera
            Vector3 camPos = camWorld.transform(Vector3(0.0f, 0.0f, 0.0f));
            // read lens via knobs (get_value, since accessor names/types churn in 16)
            Knob* fk = cam->knob("focal");
            Knob* hk = cam->knob("haperture");
            const float focal = fk ? (float)fk->get_value() : 50.0f;
            const float ha    = hk ? (float)hk->get_value() : 24.576f;
            const Format& fmt = dec->format();
            const float W  = (float)fmt.width();
            const float H  = (float)fmt.height();
            const float pa = (float)fmt.pixel_aspect();
            const float va = ha * (H * pa) / W;           // vertical aperture from format

            // --- decal tile ---
            Box db = dec->info().box();
            dec->request(db.x(), db.y(), db.r(), db.t(), Mask_RGBA, 1);
            Tile tile(*dec, db.x(), db.y(), db.r(), db.t(), Mask_RGBA);
            const Channel rC = Chan_Red, gC = Chan_Green, bC = Chan_Blue, aC = Chan_Alpha;

            // --- reference geometry ---
            Scene refScene; GeometryList refList;
            refOp->get_geometry(refScene, refList);
            const unsigned no = refList.objects();
            _baked.resize(no);

            for (unsigned o = 0; o < no; ++o) {
                const GeoInfo& gi = refList[o];
                const PointList* pl = gi.point_list();
                if (!pl) continue;
                const unsigned np = (unsigned)pl->size();
                _baked[o].assign(np, Vector4(0.0f, 0.0f, 0.0f, 0.0f));

                const Matrix4 objM = gi.matrix;                 // object -> world

                // optional normals for facing
                const Attribute* nAttr =
                    _useFacing ? gi.get_typed_attribute("N", NORMAL_ATTRIB) : nullptr;
                const bool haveN = (nAttr && nAttr->size() >= np);

                for (unsigned i = 0; i < np; ++i) {
                    const Vector3 wp = objM.transform((*pl)[i]);     // world position

                    // ---- PROJECT (verify signs/aspect with mode=uv_debug) ----
                    const Vector3 cs = view.transform(wp);           // camera space
                    if (cs.z >= -1e-4f) continue;                    // behind camera (-Z fwd)
                    const float ndcx = (cs.x / -cs.z) * (2.0f * focal / ha);
                    const float ndcy = (cs.y / -cs.z) * (2.0f * focal / va);
                    const float u = ndcx * 0.5f + 0.5f;              // 0..1 screen
                    const float v = ndcy * 0.5f + 0.5f;
                    if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f) continue;

                    // ---- facing cull ----
                    float facingDot = 1.0f;
                    if (haveN) {
                        Vector3 wn = objM.vtransform(nAttr->normal(i));
                        wn.normalize();
                        Vector3 toCam = camPos - wp; toCam.normalize();
                        facingDot = wn.dot(toCam);
                        if (_useFacing && facingDot <= _facing) continue;
                    }

                    if (_mode == 1) {                                 // uv_debug
                        _baked[o][i] = Vector4(u, v, 0.0f, 1.0f);
                    } else if (_mode == 2) {                          // facing_debug
                        float fd = std::max(0.0f, facingDot);
                        _baked[o][i] = Vector4(fd, fd, fd, 1.0f);
                    } else {                                          // decal
                        const float px = db.x() + u * (float)(db.r() - db.x());
                        const float py = db.y() + v * (float)(db.t() - db.y());
                        Vector4 c = sampleDecal(tile, rC, gC, bC, aC, px, py);
                        _baked[o][i] = c * _opacity;                  // premultiplied
                    }
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

        const bool have = (obj < (int)_baked.size() && _baked[obj].size() == np);

        for (unsigned i = 0; i < np; ++i)
            cf->vector4(i) = have ? _baked[obj][i] : Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    static const Description d;
};

static Op* build(Node* node) { return new pRefDecalBake(node); }

const Op::Description pRefDecalBake::d(CLASS, "Geometry/pRefDecalBake", build);
