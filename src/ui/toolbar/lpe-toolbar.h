// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef INKSCAPE_UI_TOOLBAR_LPE_TOOLBAR_H
#define INKSCAPE_UI_TOOLBAR_LPE_TOOLBAR_H

/**
 * @file LPE toolbar
 */
/* Authors:
 *   MenTaLguY <mental@rydia.net>
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *   bulia byak <buliabyak@users.sf.net>
 *   Frank Felfe <innerspace@iname.com>
 *   John Cliff <simarilius@yahoo.com>
 *   David Turner <novalis@gnu.org>
 *   Josh Andler <scislac@scislac.com>
 *   Jon A. Cruz <jon@joncruz.org>
 *   Maximilian Albert <maximilian.albert@gmail.com>
 *   Tavmjong Bah <tavmjong@free.fr>
 *   Abhishek Sharma
 *   Kris De Gussem <Kris.DeGussem@gmail.com>
 *   Vaibhav Malik <vaibhavmalik2018@gmail.com>
 *
 * Copyright (C) 2004 David Turner
 * Copyright (C) 2003 MenTaLguY
 * Copyright (C) 1999-2011 authors
 * Copyright (C) 2001-2002 Ximian, Inc.
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include "toolbar.h"
#include "ui/operation-blocker.h"

namespace Gtk {
class Builder;
class ToggleButton;
} // namespace Gtk

class SPLPEItem;

namespace Inkscape {
namespace LivePathEffect { class Effect; }
class Selection;
namespace Tools { class ToolBase; }
namespace UI {
namespace Widget {
class ComboToolItem;
class UnitTracker;
} // namespace Widget
} // namespace UI
} // namespace Inkscape

namespace Inkscape::UI::Toolbar {

class LPEToolbar : public Toolbar
{
public:
    LPEToolbar();
    ~LPEToolbar() override;

    void setDesktop(SPDesktop *desktop) override;
    void setActiveUnit(Util::Unit const *unit) override;

    void setMode(int mode);

private:
    LPEToolbar(Glib::RefPtr<Gtk::Builder> const &builder);

    std::unique_ptr<UI::Widget::UnitTracker> _tracker;

    std::vector<Gtk::ToggleButton *> _mode_buttons;
    Gtk::ToggleButton &_show_bbox_btn;
    Gtk::ToggleButton &_bbox_from_selection_btn;
    Gtk::ToggleButton &_measuring_btn;
    Gtk::ToggleButton &_open_lpe_dialog_btn;
    UI::Widget::ComboToolItem *_line_segment_combo;
    UI::Widget::ComboToolItem *_units_item;

    OperationBlocker _blocker;

    LivePathEffect::Effect *_currentlpe = nullptr;
    SPLPEItem *_currentlpeitem = nullptr;

    sigc::connection c_selection_modified;
    sigc::connection c_selection_changed;

    void mode_changed(int mode);
    void unit_changed(int not_used);
    void sel_modified(Inkscape::Selection *selection, guint flags);
    void sel_changed(Inkscape::Selection *selection);
    void change_line_segment_type(int mode);

    void toggle_show_bbox();
    void toggle_set_bbox();
    void toggle_show_measuring_info();
    void open_lpe_dialog();
};

} // Inkscape::UI::Toolbar

#endif // INKSCAPE_UI_TOOLBAR_LPE_TOOLBAR_H
