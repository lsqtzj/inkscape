#ifndef INKSCAPE_LPE_COPY_ROTATE_H
#define INKSCAPE_LPE_COPY_ROTATE_H

/** \file
 * LPE <copy_rotate> implementation, see lpe-copy_rotate.cpp.
 */

/*
 * Authors:
 *   Johan Engelen
 *
 * Copyright (C) Johan Engelen 2007 <j.b.c.engelen@utwente.nl>
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include "live_effects/effect.h"
#include "live_effects/parameter/point.h"
#include "live_effects/lpegroupbbox.h"

namespace Inkscape {
namespace LivePathEffect {

namespace CR {
  // we need a separate namespace to avoid clashes with LPEPerpBisector
  class KnotHolderEntityStartingAngle;
  class KnotHolderEntityRotationAngle;
}

class LPECopyRotate : public Effect, GroupBBoxEffect {
public:
    LPECopyRotate(LivePathEffectObject *lpeobject);
    virtual ~LPECopyRotate();

    virtual void doOnApply (SPLPEItem const* lpeitem);

    virtual Geom::Piecewise<Geom::D2<Geom::SBasis> > doEffect_pwd2 (Geom::Piecewise<Geom::D2<Geom::SBasis> > const & pwd2_in);

    virtual void kaleidoscope(std::vector<Geom::Path> &path_in);

    virtual void split(std::vector<Geom::Path> &path_in,Geom:Path divider,bool start);

    /* the knotholder entity classes must be declared friends */
    friend class CR::KnotHolderEntityStartingAngle;
    void addKnotHolderEntities(KnotHolder *knotholder, SPDesktop *desktop, SPItem *item);

protected:
    virtual void addCanvasIndicators(SPLPEItem const *lpeitem, std::vector<Geom::PathVector> &hp_vec);

private:
    ScalarParam starting_angle;
    ScalarParam rotation_angle;
    ScalarParam num_copies;
    BoolParam copiesTo360;
    BoolParam setKaleidoscope;

    PointParam origin;

    Geom::Point A;
    Geom::Point B;
    Geom::Point dir;

    Geom::Point start_pos;
    Geom::Point rot_pos;
    double dist_angle_handle;

    LPECopyRotate(const LPECopyRotate&);
    LPECopyRotate& operator=(const LPECopyRotate&);
};

} //namespace LivePathEffect
} //namespace Inkscape

#endif

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
