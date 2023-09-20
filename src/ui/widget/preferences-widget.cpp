// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * @file
 * Widgets for Inkscape Preferences dialog.
 */
/*
 * Authors:
 *   Marco Scholten
 *   Bruno Dilly <bruno.dilly@gmail.com>
 *
 * Copyright (C) 2004, 2006, 2007 Authors
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#include <iostream>
#include <sstream>
#include <string>
#include <utility>
#include <glibmm/i18n.h>
#include <glibmm/convert.h>
#include <glibmm/regex.h>
#include <gtkmm/box.h>
#include <gtkmm/scale.h>

#include "desktop.h"
#include "inkscape.h"
#include "message-stack.h"
#include "preferences.h"
#include "selcue.h"
#include "selection-chemistry.h"
#include "include/gtkmm_version.h"
#include "io/sys.h"
#include "ui/dialog/filedialog.h"
#include "ui/icon-loader.h"
#include "ui/pack.h"
#include "ui/util.h"
#include "ui/widget/preferences-widget.h"

#ifdef _WIN32
#include <windows.h>
#endif

namespace Inkscape::UI::Widget {

DialogPage::DialogPage()
{
    set_margin(12);

    set_orientation(Gtk::Orientation::VERTICAL);
    set_column_spacing(12);
    set_row_spacing(6);
}

/**
 * Add a widget to the bottom row of the dialog page
 *
 * \param[in] indent         Whether the widget should be indented by one column
 * \param[in] label          The label text for the widget
 * \param[in] widget         The widget to add to the page
 * \param[in] suffix         Text for an optional label at the right of the widget
 * \param[in] tip            Tooltip text for the widget
 * \param[in] expand_widget  Whether to expand the widget horizontally
 * \param[in] other_widget   An optional additional widget to display at the right of the first one
 */
void DialogPage::add_line(bool                 indent,
                          Glib::ustring const &label,
                          Gtk::Widget         &widget,
                          Glib::ustring const &suffix,
                          const Glib::ustring &tip,
                          bool                 expand_widget,
                          Gtk::Widget         *other_widget)
{
    if (!tip.empty())
        widget.set_tooltip_text(tip);
    
    auto const hb = Gtk::make_managed<Gtk::Box>();
    hb->set_spacing(12);
    hb->set_hexpand(true);
    UI::pack_start(*hb, widget, expand_widget, expand_widget);
    hb->set_valign(Gtk::Align::CENTER);
    
    // Add a label in the first column if provided
    if (!label.empty()) {
        auto const label_widget = Gtk::make_managed<Gtk::Label>(label, Gtk::Align::START,
                                                                Gtk::Align::CENTER, true);
        label_widget->set_mnemonic_widget(widget);
        label_widget->set_markup(label_widget->get_text());
        
        if (indent) {
            label_widget->set_margin_start(12);
        }

        label_widget->set_valign(Gtk::Align::CENTER);
        attach_next_to(*label_widget, Gtk::PositionType::BOTTOM);

        attach_next_to(*hb, *label_widget, Gtk::PositionType::RIGHT, 1, 1);
    } else {
        if (indent) {
            hb->set_margin_start(12);
        }

        attach_next_to(*hb, Gtk::PositionType::BOTTOM, 2, 1);
    }

    // Add a label on the right of the widget if desired
    if (!suffix.empty()) {
        auto const suffix_widget = Gtk::make_managed<Gtk::Label>(suffix, Gtk::Align::START, Gtk::Align::CENTER, true);
        suffix_widget->set_markup(suffix_widget->get_text());
        UI::pack_start(*hb, *suffix_widget,false,false);
    }

    // Pack an additional widget into a box with the widget if desired
    if (other_widget)
        UI::pack_start(*hb, *other_widget, expand_widget, expand_widget);
}

void DialogPage::add_group_header(Glib::ustring name, int columns)
{
    if (name.empty()) return;

    auto const label_widget = Gtk::make_managed<Gtk::Label>(Glib::ustring("<b>").append(name).append("</b>"),
                                                            Gtk::Align::START, Gtk::Align::CENTER, true);
    
    label_widget->set_use_markup(true);
    label_widget->set_valign(Gtk::Align::CENTER);
    attach_next_to(*label_widget, Gtk::PositionType::BOTTOM, columns, 1);
}

void DialogPage::add_group_note(Glib::ustring name)
{
    if (name.empty()) return;

    auto const label_widget = Gtk::make_managed<Gtk::Label>(Glib::ustring("<i>").append(name).append("</i>"),
                                                            Gtk::Align::START , Gtk::Align::CENTER, true);
    label_widget->set_use_markup(true);
    label_widget->set_valign(Gtk::Align::CENTER);
    label_widget->set_wrap(true);
    label_widget->set_wrap_mode(Pango::WrapMode::WORD);
    attach_next_to(*label_widget, Gtk::PositionType::BOTTOM, 2, 1);
}

void DialogPage::set_tip(Gtk::Widget& widget, Glib::ustring const &tip)
{
    widget.set_tooltip_text (tip);
}

void PrefCheckButton::init(Glib::ustring const &label, Glib::ustring const &prefs_path,
    bool default_value)
{
    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (!label.empty())
        this->set_label(label);
    this->set_active( prefs->getBool(_prefs_path, default_value) );
}

void PrefCheckButton::on_toggled()
{
    if (this->get_visible()) //only take action if the user toggled it
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setBool(_prefs_path, this->get_active());
    }
    this->changed_signal.emit(this->get_active());
}

void PrefRadioButton::init(Glib::ustring const &label, Glib::ustring const &prefs_path,
    Glib::ustring const &string_value, bool default_value, PrefRadioButton* group_member)
{
    _prefs_path = prefs_path;
    _value_type = VAL_STRING;
    _string_value = string_value;
    (void)default_value;
    this->set_label(label);
    if (group_member)
    {
        Gtk::RadioButtonGroup rbg = group_member->get_group();
        this->set_group(rbg);
    }
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring val = prefs->getString(_prefs_path);
    if ( !val.empty() )
        this->set_active(val == _string_value);
    else
        this->set_active( false );
}

void PrefRadioButton::init(Glib::ustring const &label, Glib::ustring const &prefs_path,
    int int_value, bool default_value, PrefRadioButton* group_member)
{
    _prefs_path = prefs_path;
    _value_type = VAL_INT;
    _int_value = int_value;
    this->set_label(label);
    if (group_member)
    {
        Gtk::RadioButtonGroup rbg = group_member->get_group();
        this->set_group(rbg);
    }
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (default_value)
        this->set_active( prefs->getInt(_prefs_path, int_value) == _int_value );
    else
        this->set_active( prefs->getInt(_prefs_path, int_value + 1) == _int_value );
}

void PrefRadioButton::on_toggled()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();

    if (this->get_visible() && this->get_active() ) //only take action if toggled by user (to active)
    {
        if ( _value_type == VAL_STRING )
            prefs->setString(_prefs_path, _string_value);
        else if ( _value_type == VAL_INT )
            prefs->setInt(_prefs_path, _int_value);
    }
    this->changed_signal.emit(this->get_active());
}

PrefRadioButtons::PrefRadioButtons(const std::vector<PrefItem>& buttons, const Glib::ustring& prefs_path) {
    set_spacing(2);

    PrefRadioButton* group = nullptr;
    for (auto&& item : buttons) {
        auto* btn = Gtk::make_managed<PrefRadioButton>();
        btn->init(item.label, prefs_path, item.int_value, item.is_default, group);
        btn->set_tooltip_text(item.tooltip);
        add(*btn);
        if (!group) group = btn;
    }
}

void PrefSpinButton::init(Glib::ustring const &prefs_path,
              double lower, double upper, double step_increment, double /*page_increment*/,
              double default_value, bool is_int, bool is_percent)
{
    _prefs_path = prefs_path;
    _is_int = is_int;
    _is_percent = is_percent;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double value;
    if (is_int) {
        if (is_percent) {
            value = 100 * prefs->getDoubleLimited(prefs_path, default_value, lower/100.0, upper/100.0);
        } else {
            value = (double) prefs->getIntLimited(prefs_path, (int) default_value, (int) lower, (int) upper);
        }
    } else {
        value = prefs->getDoubleLimited(prefs_path, default_value, lower, upper);
    }

    this->set_range (lower, upper);
    this->set_increments (step_increment, 0);
    this->set_value (value);
    this->set_width_chars(6);
    if (is_int)
        this->set_digits(0);
    else if (step_increment < 0.1)
        this->set_digits(4);
    else
        this->set_digits(2);

}

void PrefSpinButton::on_value_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (this->get_visible()) //only take action if user changed value
    {
        if (_is_int) {
            if (_is_percent) {
                prefs->setDouble(_prefs_path, this->get_value()/100.0);
            } else {
                prefs->setInt(_prefs_path, (int) this->get_value());
            }
        } else {
            prefs->setDouble(_prefs_path, this->get_value());
        }
    }
    this->changed_signal.emit(this->get_value());
}

void PrefSpinUnit::init(Glib::ustring const &prefs_path,
              double lower, double upper, double step_increment,
              double default_value, UnitType unit_type, Glib::ustring const &default_unit)
{
    _prefs_path = prefs_path;
    _is_percent = (unit_type == UNIT_TYPE_DIMENSIONLESS);

    resetUnitType(unit_type);
    setUnit(default_unit);
    setRange (lower, upper); /// @fixme  this disregards changes of units
    setIncrements (step_increment, 0);
    if (step_increment < 0.1) {
        setDigits(4);
    } else {
        setDigits(2);
    }

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double value = prefs->getDoubleLimited(prefs_path, default_value, lower, upper);
    Glib::ustring unitstr = prefs->getUnit(prefs_path);
    if (unitstr.length() == 0) {
        unitstr = default_unit;
        // write the assumed unit to preferences:
        prefs->setDoubleUnit(_prefs_path, value, unitstr);
    }
    setValue(value, unitstr);

    signal_value_changed().connect_notify(sigc::mem_fun(*this, &PrefSpinUnit::on_my_value_changed));
}

void PrefSpinUnit::on_my_value_changed()
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    if (getWidget()->get_visible()) //only take action if user changed value
    {
        prefs->setDoubleUnit(_prefs_path, getValue(getUnit()->abbr), getUnit()->abbr);
    }
}

const double ZoomCorrRuler::textsize = 7;
const double ZoomCorrRuler::textpadding = 5;

ZoomCorrRuler::ZoomCorrRuler(int width, int height) :
    _unitconv(1.0),
    _border(5)
{
    set_size(width, height);
}

void ZoomCorrRuler::set_size(int x, int y)
{
    _min_width = x;
    _height = y;
    set_size_request(x + _border*2, y + _border*2);
}

// The following two functions are borrowed from 2geom's toy-framework-2; if they are useful in
// other locations, we should perhaps make them (or adapted versions of them) publicly available
static void
draw_text(cairo_t *cr, Geom::Point loc, const char* txt, bool bottom = false,
          double fontsize = ZoomCorrRuler::textsize, std::string fontdesc = "Sans") {
    PangoLayout* layout = pango_cairo_create_layout (cr);
    pango_layout_set_text(layout, txt, -1);

    // set font and size
    std::ostringstream sizestr;
    sizestr << fontsize;
    fontdesc = fontdesc + " " + sizestr.str();
    PangoFontDescription *font_desc = pango_font_description_from_string(fontdesc.c_str());
    pango_layout_set_font_description(layout, font_desc);
    pango_font_description_free (font_desc);

    PangoRectangle logical_extent;
    pango_layout_get_pixel_extents(layout, nullptr, &logical_extent);
    cairo_move_to(cr, loc[Geom::X], loc[Geom::Y] - (bottom ? logical_extent.height : 0));
    pango_cairo_show_layout(cr, layout);
}

static void
draw_number(cairo_t *cr, Geom::Point pos, double num) {
    std::ostringstream number;
    number << num;
    draw_text(cr, pos, number.str().c_str(), true);
}

/*
 * \arg dist The distance between consecutive minor marks
 * \arg major_interval Number of marks after which to draw a major mark
 */
void
ZoomCorrRuler::draw_marks(Cairo::RefPtr<Cairo::Context> cr, double dist, int major_interval) {
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    const double zoomcorr = prefs->getDouble("/options/zoomcorrection/value", 1.0);
    double mark = 0;
    int i = 0;
    double step = dist * zoomcorr / _unitconv;
    bool draw_minor = true;
    if (step <= 0) {
        return;
    }
    else if (step < 2) {
        // marks too dense
        draw_minor = false;
    }
    int last_pos = -1;
    while (mark <= _drawing_width) {
        cr->move_to(mark, _height);
        if ((i % major_interval) == 0) {
            // don't overcrowd the marks
            if (static_cast<int>(mark) > last_pos) {
                // major mark
                cr->line_to(mark, 0);
                Geom::Point textpos(mark + 3, ZoomCorrRuler::textsize + ZoomCorrRuler::textpadding);
                draw_number(cr->cobj(), textpos, dist * i);

                last_pos = static_cast<int>(mark) + 1;
            }
        } else if (draw_minor) {
            // minor mark
            cr->line_to(mark, ZoomCorrRuler::textsize + 2 * ZoomCorrRuler::textpadding);
        }
        mark += step;
        ++i;
    }
}

bool
ZoomCorrRuler::on_draw(const Cairo::RefPtr<Cairo::Context>& cr) {
    auto const w = get_width();
    _drawing_width = w - _border * 2;

    auto const fg = get_color();
    cr->set_line_width(1);
    cr->set_source_rgb(fg.get_red(), fg.get_green(), fg.get_blue());

    cr->translate(_border, _border); // so that we have a small white border around the ruler
    cr->move_to (0, _height);
    cr->line_to (_drawing_width, _height);

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring abbr = prefs->getString("/options/zoomcorrection/unit");
    if (abbr == "cm") {
        draw_marks(cr, 0.1, 10);
    } else if (abbr == "in") {
        draw_marks(cr, 0.25, 4);
    } else if (abbr == "mm") {
        draw_marks(cr, 10, 10);
    } else if (abbr == "pc") {
        draw_marks(cr, 1, 10);
    } else if (abbr == "pt") {
        draw_marks(cr, 10, 10);
    } else if (abbr == "px") {
        draw_marks(cr, 10, 10);
    } else {
        draw_marks(cr, 1, 1);
    }
    cr->stroke();

    return true;
}

void
ZoomCorrRulerSlider::on_slider_value_changed()
{
    if (this->get_visible() || freeze) //only take action if user changed value
    {
        freeze = true;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setDouble("/options/zoomcorrection/value", _slider->get_value() / 100.0);
        _sb->set_value(_slider->get_value());
        _ruler.queue_draw();
        freeze = false;
    }
}

void
ZoomCorrRulerSlider::on_spinbutton_value_changed()
{
    if (this->get_visible() || freeze) //only take action if user changed value
    {
        freeze = true;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setDouble("/options/zoomcorrection/value", _sb->get_value() / 100.0);
        _slider->set_value(_sb->get_value());
        _ruler.queue_draw();
        freeze = false;
    }
}

void
ZoomCorrRulerSlider::on_unit_changed() {
    if (!_unit.get_sensitive()) {
        // when the unit menu is initialized, the unit is set to the default but
        // it needs to be reset later so we don't perform the change in this case
        return;
    }
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    prefs->setString("/options/zoomcorrection/unit", _unit.getUnitAbbr());
    double conv = _unit.getConversion(_unit.getUnitAbbr(), "px");
    _ruler.set_unit_conversion(conv);
    if (_ruler.get_visible()) {
        _ruler.queue_draw();
    }
}

bool ZoomCorrRulerSlider::on_mnemonic_activate ( bool group_cycling )
{
    return _sb->mnemonic_activate ( group_cycling );
}

void
ZoomCorrRulerSlider::init(int ruler_width, int ruler_height, double lower, double upper,
                      double step_increment, double page_increment, double default_value)
{
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double value = prefs->getDoubleLimited("/options/zoomcorrection/value", default_value, lower, upper) * 100.0;

    freeze = false;

    _ruler.set_size(ruler_width, ruler_height);

    _slider = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);

    _slider->set_size_request(_ruler.width(), -1);
    _slider->set_range (lower, upper);
    _slider->set_increments (step_increment, page_increment);
    _slider->set_value (value);
    _slider->set_digits(2);

    _slider->signal_value_changed().connect(sigc::mem_fun(*this, &ZoomCorrRulerSlider::on_slider_value_changed));
    _sb = Gtk::make_managed<Inkscape::UI::Widget::SpinButton>();
    _sb->signal_value_changed().connect(sigc::mem_fun(*this, &ZoomCorrRulerSlider::on_spinbutton_value_changed));
    _unit.signal_changed().connect(sigc::mem_fun(*this, &ZoomCorrRulerSlider::on_unit_changed));

    _sb->set_range (lower, upper);
    _sb->set_increments (step_increment, 0);
    _sb->set_value (value);
    _sb->set_digits(2);
    _sb->set_halign(Gtk::Align::CENTER);
    _sb->set_valign(Gtk::Align::END);

    _unit.set_sensitive(false);
    _unit.setUnitType(UNIT_TYPE_LINEAR);
    _unit.set_sensitive(true);
    _unit.setUnit(prefs->getString("/options/zoomcorrection/unit"));
    _unit.set_halign(Gtk::Align::CENTER);
    _unit.set_valign(Gtk::Align::END);

    _slider->set_hexpand(true);
    _ruler.set_hexpand(true);
    auto const table = Gtk::make_managed<Gtk::Grid>();
    table->attach(*_slider, 0, 0, 1, 1);
    table->attach(*_sb,      1, 0, 1, 1);
    table->attach(_ruler,   0, 1, 1, 1);
    table->attach(_unit,    1, 1, 1, 1);

    UI::pack_start(*this, *table, UI::PackOptions::shrink);
}

void
PrefSlider::on_slider_value_changed()
{
    if (this->get_visible() || freeze) //only take action if user changed value
    {
        freeze = true;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setDouble(_prefs_path, _slider->get_value());
        if (_sb) _sb->set_value(_slider->get_value());
        freeze = false;
    }
}

void
PrefSlider::on_spinbutton_value_changed()
{
    if (this->get_visible() || freeze) //only take action if user changed value
    {
        freeze = true;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if (_sb) {
            prefs->setDouble(_prefs_path, _sb->get_value());
            _slider->set_value(_sb->get_value());
        }
        freeze = false;
    }
}

bool PrefSlider::on_mnemonic_activate ( bool group_cycling )
{
    return _sb ? _sb->mnemonic_activate ( group_cycling ) : false;
}

void
PrefSlider::init(Glib::ustring const &prefs_path,
                 double lower, double upper, double step_increment, double page_increment, double default_value, int digits)
{
    _prefs_path = prefs_path;

    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    double value = prefs->getDoubleLimited(prefs_path, default_value, lower, upper);

    freeze = false;

    _slider = Gtk::make_managed<Gtk::Scale>(Gtk::Orientation::HORIZONTAL);

    _slider->set_range (lower, upper);
    _slider->set_increments (step_increment, page_increment);
    _slider->set_value (value);
    _slider->set_digits(digits);
    _slider->signal_value_changed().connect(sigc::mem_fun(*this, &PrefSlider::on_slider_value_changed));
    if (_spin) {
        _sb = Gtk::make_managed<Inkscape::UI::Widget::SpinButton>();
        _sb->signal_value_changed().connect(sigc::mem_fun(*this, &PrefSlider::on_spinbutton_value_changed));
        _sb->set_range (lower, upper);
        _sb->set_increments (step_increment, 0);
        _sb->set_value (value);
        _sb->set_digits(digits);
        _sb->set_halign(Gtk::Align::CENTER);
        _sb->set_valign(Gtk::Align::END);
    }

    auto const table = Gtk::make_managed<Gtk::Grid>();
    _slider->set_hexpand();
    table->attach(*_slider, 0, 0, 1, 1);
    if (_sb) table->attach(*_sb, 1, 0, 1, 1);

    UI::pack_start(*this, *table, UI::PackOptions::expand_widget);
}

void PrefCombo::init(Glib::ustring const &prefs_path,
                     std::span<Glib::ustring const> labels,
                     std::span<int const> values,
                     int const default_value)
{
    int const labels_size = labels.size();
    int const values_size = values.size();
    if (values_size != labels_size) {
        std::cerr << "PrefCombo::"
                  << "Different number of values/labels in " << prefs_path.raw() << std::endl;
        return;
    }

    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int row = 0;
    int value = prefs->getInt(_prefs_path, default_value);

    for (int i = 0; i < labels_size; ++i) {
        this->append(labels[i]);
        _values.push_back(values[i]);
        if (value == values[i])
            row = i;
    }
    this->set_active(row);
}

void PrefCombo::init(Glib::ustring const &prefs_path,
                     std::span<Glib::ustring const> labels,
                     std::span<Glib::ustring const> values,
                     Glib::ustring const &default_value)
{
    int const labels_size = labels.size();
    int const values_size = values.size();
    if (values_size != labels_size) {
        std::cerr << "PrefCombo::"
                  << "Different number of values/labels in " << prefs_path.raw() << std::endl;
        return;
    }

    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    int row = 0;
    Glib::ustring value = prefs->getString(_prefs_path);
    if (value.empty()) {
        value = default_value;
    }

    for (int i = 0; i < labels_size; ++i) {
        this->append(labels[i]);
        _ustr_values.push_back(values[i]);
        if (value == values[i])
            row = i;
    }
    this->set_active(row);
}

void PrefCombo::on_changed()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        if(!_values.empty())
        {
            prefs->setInt(_prefs_path, _values[this->get_active_row_number()]);
        }
        else
        {
            prefs->setString(_prefs_path, _ustr_values[this->get_active_row_number()]);
        }
    }
}

void PrefEntryButtonHBox::init(Glib::ustring const &prefs_path,
            bool visibility, Glib::ustring const &default_string)
{
    _prefs_path = prefs_path;
    _default_string = default_string;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    relatedEntry = Gtk::make_managed<Gtk::Entry>();
    relatedButton = Gtk::make_managed<Gtk::Button>(_("Reset"));
    relatedEntry->set_invisible_char('*');
    relatedEntry->set_visibility(visibility);
    relatedEntry->set_text(prefs->getString(_prefs_path));
    UI::pack_start(*this, *relatedEntry);
    UI::pack_start(*this, *relatedButton);
    relatedButton->signal_clicked().connect(
            sigc::mem_fun(*this, &PrefEntryButtonHBox::onRelatedButtonClickedCallback));
    relatedEntry->signal_changed().connect(
            sigc::mem_fun(*this, &PrefEntryButtonHBox::onRelatedEntryChangedCallback));
}

void PrefEntryButtonHBox::onRelatedEntryChangedCallback()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, relatedEntry->get_text());
    }
}

void PrefEntryButtonHBox::onRelatedButtonClickedCallback()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, _default_string);
        relatedEntry->set_text(_default_string);
    }
}

bool PrefEntryButtonHBox::on_mnemonic_activate ( bool group_cycling )
{
    return relatedEntry->mnemonic_activate ( group_cycling );
}

void PrefEntryFileButtonHBox::init(Glib::ustring const &prefs_path,
            bool visibility)
{
    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    
    relatedEntry = Gtk::make_managed<Gtk::Entry>();
    relatedEntry->set_invisible_char('*');
    relatedEntry->set_visibility(visibility);
    relatedEntry->set_text(prefs->getString(_prefs_path));
    
    relatedButton = Gtk::make_managed<Gtk::Button>();
    auto const pixlabel = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
    auto const im = sp_get_icon_image("applications-graphics", Gtk::IconSize::NORMAL);
    UI::pack_start(*pixlabel, *im);
    auto const l = Gtk::make_managed<Gtk::Label>();
    l->set_markup_with_mnemonic(_("_Browse..."));
    UI::pack_start(*pixlabel, *l);
    relatedButton->add(*pixlabel); 

    UI::pack_end(*this, *relatedButton, false, false, 4);
    UI::pack_start(*this, *relatedEntry, true, true);

    relatedButton->signal_clicked().connect(
            sigc::mem_fun(*this, &PrefEntryFileButtonHBox::onRelatedButtonClickedCallback));
    relatedEntry->signal_changed().connect(
            sigc::mem_fun(*this, &PrefEntryFileButtonHBox::onRelatedEntryChangedCallback));
}

void PrefEntryFileButtonHBox::onRelatedEntryChangedCallback()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, relatedEntry->get_text());
    }
}

static Inkscape::UI::Dialog::FileOpenDialog * selectPrefsFileInstance = nullptr;

void PrefEntryFileButtonHBox::onRelatedButtonClickedCallback()
{
    if (this->get_visible()) //only take action if user changed value
    {
        //# Get the current directory for finding files
        static Glib::ustring open_path;
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();

        Glib::ustring attr = prefs->getString(_prefs_path);
        if (!attr.empty()) open_path = attr;
        
        //# Test if the open_path directory exists
        if (!Inkscape::IO::file_test(open_path.c_str(),
                  (GFileTest)(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_DIR)))
            open_path = "";

#ifdef _WIN32
        //# If no open path, default to our win32 documents folder
        if (open_path.empty())
        {
            // The path to the My Documents folder is read from the
            // value "HKEY_CURRENT_USER\Software\Windows\CurrentVersion\Explorer\Shell Folders\Personal"
            HKEY key = NULL;
            if(RegOpenKeyExA(HKEY_CURRENT_USER,
                "Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Shell Folders",
                0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS)
            {
                WCHAR utf16path[_MAX_PATH];
                DWORD value_type;
                DWORD data_size = sizeof(utf16path);
                if(RegQueryValueExW(key, L"Personal", NULL, &value_type,
                    (BYTE*)utf16path, &data_size) == ERROR_SUCCESS)
                {
                    g_assert(value_type == REG_SZ);
                    char * const utf8path = g_utf16_to_utf8(
                        (const gunichar2*)utf16path, -1, NULL, NULL, NULL);
                    if(utf8path)
                    {
                        open_path = Glib::ustring(utf8path);
                        g_free(utf8path);
                    }
                }
            }
        }
#endif

        //# If no open path, default to our home directory
        if (open_path.empty())
        {
            open_path = g_get_home_dir();
            open_path.append(G_DIR_SEPARATOR_S);
        }

        //# Create a dialog
        SPDesktop *desktop = SP_ACTIVE_DESKTOP;
        if (!selectPrefsFileInstance) {
        selectPrefsFileInstance =
              Inkscape::UI::Dialog::FileOpenDialog::create(
                 *desktop->getToplevel(),
                 open_path,
                 Inkscape::UI::Dialog::EXE_TYPES,
                 _("Select a bitmap editor"));
        }
        
        //# Show the dialog
        bool const success = selectPrefsFileInstance->show();
        
        if (!success) {
            return;
        }
        
        //# User selected something.  Get name and type
        Glib::ustring fileName = selectPrefsFileInstance->getFilename();

        if (!fileName.empty())
        {
            Glib::ustring newFileName = Glib::filename_to_utf8(fileName);

            if ( newFileName.size() > 0)
                open_path = newFileName;
            else
                g_warning( "ERROR CONVERTING OPEN FILENAME TO UTF-8" );

            prefs->setString(_prefs_path, open_path);
        }
        
        relatedEntry->set_text(fileName);
    }
}

bool PrefEntryFileButtonHBox::on_mnemonic_activate ( bool group_cycling )
{
    return relatedEntry->mnemonic_activate ( group_cycling );
}

void PrefOpenFolder::init(Glib::ustring const &entry_string, Glib::ustring const &tooltip)
{
    relatedEntry = Gtk::make_managed<Gtk::Entry>();
    relatedButton = Gtk::make_managed<Gtk::Button>();
    auto const pixlabel = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 3);
    auto const im = sp_get_icon_image("document-open", Gtk::IconSize::NORMAL);
    UI::pack_start(*pixlabel, *im);
    auto const l = Gtk::make_managed<Gtk::Label>();
    l->set_markup_with_mnemonic(_("Open"));
    UI::pack_start(*pixlabel, *l);
    relatedButton->add(*pixlabel);
    relatedButton->set_tooltip_text(tooltip);
    relatedEntry->set_text(entry_string);
    relatedEntry->set_sensitive(false);
    UI::pack_end(*this, *relatedButton, false, false, 4);
    UI::pack_start(*this, *relatedEntry, true, true);
    relatedButton->signal_clicked().connect(sigc::mem_fun(*this, &PrefOpenFolder::onRelatedButtonClickedCallback));
}

void PrefOpenFolder::onRelatedButtonClickedCallback()
{
    g_mkdir_with_parents(relatedEntry->get_text().c_str(), 0700);
    // https://stackoverflow.com/questions/42442189/how-to-open-spawn-a-file-with-glib-gtkmm-in-windows
#ifdef _WIN32
    ShellExecute(NULL, "open", relatedEntry->get_text().c_str(), NULL, NULL, SW_SHOWDEFAULT);
#elif defined(__APPLE__)
    std::vector<std::string> argv = { "open", relatedEntry->get_text().raw() };
    Glib::spawn_async("", argv, Glib::SpawnFlags::SEARCH_PATH);
#else
    char * const path = g_filename_to_uri(relatedEntry->get_text().c_str(), NULL, NULL);
    std::vector<std::string> argv = { "xdg-open", path };
    Glib::spawn_async("", argv, Glib::SpawnFlags::SEARCH_PATH);
    g_free(path);
#endif
}

void PrefEntry::init(Glib::ustring const &prefs_path, bool visibility)
{
    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    this->set_invisible_char('*');
    this->set_visibility(visibility);
    this->set_text(prefs->getString(_prefs_path));
}

void PrefEntry::on_changed()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, this->get_text());
    }
}

void PrefEntryFile::on_changed()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, Glib::filename_to_utf8(this->get_text()));
    }
}

void PrefMultiEntry::init(Glib::ustring const &prefs_path, int height)
{
    // TODO: Figure out if there's a way to specify height in lines instead of px
    //       and how to obtain a reasonable default width if 'expand_widget' is not used
    set_size_request(100, height);
    set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    set_has_frame(true);

    add(_text);

    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    Glib::ustring value = prefs->getString(_prefs_path);
    value = Glib::Regex::create("\\|")->replace_literal(value, 0, "\n", (Glib::Regex::MatchFlags)0);
    _text.get_buffer()->set_text(value);
    _text.get_buffer()->signal_changed().connect(sigc::mem_fun(*this, &PrefMultiEntry::on_changed));
}

void PrefMultiEntry::on_changed()
{
    if (get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        Glib::ustring value = _text.get_buffer()->get_text();
        value = Glib::Regex::create("\\n")->replace_literal(value, 0, "|", (Glib::Regex::MatchFlags)0);
        prefs->setString(_prefs_path, value);
    } 
}

void PrefColorPicker::init(Glib::ustring const &label, Glib::ustring const &prefs_path,
                           std::uint32_t const default_rgba)
{
    _prefs_path = prefs_path;
    _title = label;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    this->setRgba32( prefs->getInt(_prefs_path, (int)default_rgba) );
}

void PrefColorPicker::on_changed(std::uint32_t const rgba)
{
    if (this->get_visible()) //only take action if the user toggled it
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setInt(_prefs_path, static_cast<int>(rgba));
    }
}

void PrefUnit::init(Glib::ustring const &prefs_path)
{
    _prefs_path = prefs_path;
    Inkscape::Preferences *prefs = Inkscape::Preferences::get();
    setUnitType(UNIT_TYPE_LINEAR);
    setUnit(prefs->getString(_prefs_path));
}

void PrefUnit::on_changed()
{
    if (this->get_visible()) //only take action if user changed value
    {
        Inkscape::Preferences *prefs = Inkscape::Preferences::get();
        prefs->setString(_prefs_path, getUnitAbbr());
    }
}

} // namespace Inkscape::UI::Widget

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0)(case-label . +))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=cpp:expandtab:shiftwidth=4:tabstop=8:softtabstop=4:fileencoding=utf-8:textwidth=99 :
