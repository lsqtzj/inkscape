/*
 * Released under GNU GPL, read the file 'COPYING' for more information
 */
#include "live_effects/lpe-dash-stroke.h"
#include "2geom/pathvector.h"
#include "2geom/path.h"
#include "helper/geom.h"

// TODO due to internal breakage in glibmm headers, this must be last:
#include <glibmm/i18n.h>

namespace Inkscape {
namespace LivePathEffect {

LPEDashStroke::LPEDashStroke(LivePathEffectObject *lpeobject)
    : Effect(lpeobject),
    numberdashes(_("Number of dashes"), _("Number of dashes"), "numberdashes", &wr, this, 3),
    holefactor(_("Hole factor"), _("Hole factor"), "holefactor", &wr, this, 0.0),
    splitsegments(_("Use segments"), _("Use segments"), "splitsegments", &wr, this, true),
    halfextreme(_("Half start/end"), _("Start and end of each segment has half size"), "halfextreme", &wr, this, true),
    message(_("Info Box"), _("Important messages"), "message", &wr, this, _("Add <b>\"Fill Between Many LPE\"</b> to add fill."))
{
    registerParameter(&numberdashes);
    registerParameter(&holefactor);
    registerParameter(&splitsegments);
    registerParameter(&halfextreme);
    registerParameter(&message);
    numberdashes.param_set_range(0, 5000);
    numberdashes.param_set_increments(1, 1);
    numberdashes.param_set_digits(0);
    holefactor.param_set_range(-0.99999, 0.99999);
    holefactor.param_set_increments(0.01, 0.01);
    holefactor.param_set_digits(5);
    message.param_set_min_height(30);
}

LPEDashStroke::~LPEDashStroke() {}

void
LPEDashStroke::doBeforeEffect (SPLPEItem const* lpeitem){
}

///Calculate the time in curve_in with a real time of A
//TODO: find a better place to it
double 
LPEDashStroke::timeAtLength(double const A, Geom::Path const &segment)
{
    if ( A == 0 || segment[0].isDegenerate()) {
        return 0;
    }
    double t = 1;
    t = timeAtLength(A, segment.toPwSb());
    return t;
}

///Calculate the time in curve_in with a real time of A
//TODO: find a better place to it
double 
LPEDashStroke::timeAtLength(double const A, Geom::Piecewise<Geom::D2<Geom::SBasis> > pwd2)
{
    if ( A == 0 || pwd2.size() == 0) {
        return 0;
    }

    double t = pwd2.size();
    std::vector<double> t_roots = roots(Geom::arcLengthSb(pwd2) - A);
    if (!t_roots.empty()) {
        t = t_roots[0];
    }
    return t;
}

Geom::PathVector
LPEDashStroke::doEffect_path(Geom::PathVector const & path_in){
    Geom::PathVector const pv = pathv_to_linear_and_cubic_beziers(path_in);
    Geom::PathVector result;
    for (Geom::PathVector::const_iterator path_it = pv.begin(); path_it != pv.end(); ++path_it) {
        if (path_it->empty()) {
            continue;
        }
        Geom::Path::const_iterator curve_it1 = path_it->begin();
        Geom::Path::const_iterator curve_it2 = ++(path_it->begin());
        Geom::Path::const_iterator curve_endit = path_it->end_default();
        if (path_it->closed()) {
          const Geom::Curve &closingline = path_it->back_closed(); 
          // the closing line segment is always of type 
          // Geom::LineSegment.
          if (are_near(closingline.initialPoint(), closingline.finalPoint())) {
            // closingline.isDegenerate() did not work, because it only checks for
            // *exact* zero length, which goes wrong for relative coordinates and
            // rounding errors...
            // the closing line segment has zero-length. So stop before that one!
            curve_endit = path_it->end_open();
          }
        }
        if(splitsegments) {
            //double item_length = Geom::length(paths_to_pw(path_it));
            //item_length = Inkscape::Util::Quantity::convert(item_length * scale, unit->abbr, unit_name);
        }
        size_t numberholes = numberdashes - 1;
        size_t ammount = numberdashes + numberholes;
        if (halfextreme) {
            ammount--;
        }
        double base = 1/(double)ammount;
        double globaldash =  base * numberdashes * (1 + holefactor);
        if (halfextreme) { 
            globaldash =  base * (numberdashes - 1) * (1 + holefactor);
        }
        double globalhole =  1-globaldash;
        double dashpercent = globaldash/numberdashes;
        if (halfextreme) { 
           dashpercent = globaldash/(numberdashes -1);
        }
        double holepercent = globalhole/numberholes;

        size_t p_index = 0;
        size_t start_index = 0;
        if(splitsegments) {
            while (curve_it1 != curve_endit) {
                Geom::Path segment = (*path_it).portion(p_index, p_index + 1);
                double dashsize = (*curve_it1).length() * dashpercent;
                double holesize = (*curve_it1).length() * holepercent;
                if ((*curve_it1).isLineSegment()) {
                    if (result.size() && Geom::are_near(segment.initialPoint(),result[result.size()-1].finalPoint())) {
                        result[result.size()-1].setFinal(segment.initialPoint());
                        if (halfextreme) {
                            result[result.size()-1].append(segment.portion(0.0, dashpercent/2.0));
                        } else {
                            result[result.size()-1].append(segment.portion(0.0, dashpercent));
                        }
                    } else {
                        if (halfextreme) {
                            result.push_back(segment.portion(0.0, dashpercent/2.0));
                        } else {
                            result.push_back(segment.portion(0.0, dashpercent));
                        }
                        start_index = result.size()-1;
                    }
                    
                    double start = dashpercent + holepercent;
                    if (halfextreme) {
                        start = (dashpercent/2.0) + holepercent;
                    }
                    while (start  < 1) {
                        if (start + dashpercent > 1) {
                            result.push_back(segment.portion(start, 1));
                        } else {
                            result.push_back(segment.portion(start, start + dashpercent));
                        }
                        start += dashpercent + holepercent;
                    }
                } else if (!(*curve_it1).isLineSegment()) {
                    double start = 0.0;
                    double end = 0.0;
                    if (halfextreme) {
                        end = timeAtLength(dashsize/2.0,segment);
                    } else {
                        end = timeAtLength(dashsize,segment);
                    }
                    if (result.size() && Geom::are_near(segment.initialPoint(),result[result.size()-1].finalPoint())) {
                        result[result.size()-1].setFinal(segment.initialPoint());
                        result[result.size()-1].append(segment.portion(start, end));
                    } else {
                        result.push_back(segment.portion(start, end));
                        start_index = result.size()-1;
                    }
                    double startsize = dashsize + holesize;
                    if (halfextreme) {
                        startsize = (dashsize/2.0) + holesize;
                    }
                    double endsize = startsize + dashsize;
                    start = timeAtLength(startsize,segment);
                    end   = timeAtLength(endsize,segment);
                    while (start  < 1 && start  > 0) {
                        result.push_back(segment.portion(start, end));
                        startsize = endsize + holesize;
                        endsize = startsize + dashsize;
                        start = timeAtLength(startsize,segment);
                        end   = timeAtLength(endsize,segment);
                    }
                }
                p_index ++;
                ++curve_it1;
                ++curve_it2;
            } 
        } else {
            double start = 0.0;
            double end = 0.0;
            Geom::Piecewise<Geom::D2<Geom::SBasis> > pwd2 = (*path_it).toPwSb();
            double lenght_pwd2 = length (pwd2);
            double dashsize = lenght_pwd2 * dashpercent;
            double holesize = lenght_pwd2 * holepercent;
            if (halfextreme) {
                end = timeAtLength(dashsize/2.0,pwd2);
            } else {
                end = timeAtLength(dashsize,pwd2);
            }
            result.push_back((*path_it).portion(start, end));
            start_index = result.size()-1;
            double startsize = dashsize + holesize;
            if (halfextreme) {
                startsize = (dashsize/2.0) + holesize;
            }
            double endsize = startsize + dashsize;
            start = timeAtLength(startsize,pwd2);
            end   = timeAtLength(endsize,pwd2);
            while (start  < (*path_it).size() && start  > 0) {
                result.push_back((*path_it).portion(start, end));
                startsize = endsize + holesize;
                endsize = startsize + dashsize;
                start = timeAtLength(startsize,pwd2);
                end   = timeAtLength(endsize,pwd2);
            }
        }
        if (curve_it2 == curve_endit) {
            if (path_it->closed()) {
                Geom::Path end = result[result.size()-1];
                end.setFinal(result[start_index].initialPoint());
                end.append(result[start_index]);
                result[start_index] = end;
            }
        }
    }
    return result;
}

}; //namespace LivePathEffect
}; /* namespace Inkscape */

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
