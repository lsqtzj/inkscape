// SPDX-License-Identifier: GPL-2.0-or-later
/** @file
 * @brief A dialog for CSS selectors
 */
/* Authors:
 *   Kamalpreet Kaur Grewal
 *   Tavmjong Bah
 *
 * Copyright (C) Kamalpreet Kaur Grewal 2016 <grewalkamal005@gmail.com>
 * Copyright (C) Tavmjong Bah 2017 <tavmjong@free.fr>
 *
 * Released under GNU GPL v2+, read the file 'COPYING' for more information.
 */

#ifndef STYLEDIALOG_H
#define STYLEDIALOG_H

#include <ui/widget/panel.h>
#include <gtkmm/treeview.h>
#include <gtkmm/treestore.h>
#include <gtkmm/treemodelfilter.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/dialog.h>
#include <gtkmm/treeselection.h>
#include <gtkmm/paned.h>
#include <glibmm/regex.h>

#include "ui/dialog/desktop-tracker.h"

#include "xml/helper-observer.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

/**
 * @brief The StyleDialog class
 * A list of CSS selectors will show up in this dialog. This dialog allows one to
 * add and delete selectors. Elements can be added to and removed from the selectors
 * in the dialog. Selection of any selector row selects the matching  objects in
 * the drawing and vice-versa. (Only simple selectors supported for now.)
 *
 * This class must keep two things in sync:
 *   1. The text node of the style element.
 *   2. The Gtk::TreeModel.
 */
class StyleDialog : public Widget::Panel {

public:
    ~StyleDialog() override;
    // No default constructor, noncopyable, nonassignable
    StyleDialog();
    StyleDialog(StyleDialog const &d) = delete;
    StyleDialog operator=(StyleDialog const &d) = delete;

    static StyleDialog &getInstance() { return *new StyleDialog(); }
  private:
    // Monitor <style> element for changes.
    class NodeObserver;
    // Monitor all objects for addition/removal/attribute change
    class NodeWatcher;
    Glib::RefPtr<Glib::Regex> r_props = Glib::Regex::create("\\s*;\\s*");
    Glib::RefPtr<Glib::Regex> r_pair = Glib::Regex::create("\\s*:\\s*");
    std::vector<StyleDialog::NodeWatcher*> _nodeWatchers;
    void _nodeAdded(   Inkscape::XML::Node &repr );
    void _nodeRemoved( Inkscape::XML::Node &repr );
    void _nodeChanged( Inkscape::XML::Node &repr );
    // Data structure
    class ModelColumns : public Gtk::TreeModel::ColumnRecord {
    public:
        ModelColumns() {
            add(_colActive);
            add(_colLabel);
            add(_colSelector);
            add(_colValue);
            add(_colStrike);
        }
        Gtk::TreeModelColumn<Glib::ustring > _colSelector; // Style or matching object id.
        Gtk::TreeModelColumn<bool> _colActive;             // Active or inative property
        Gtk::TreeModelColumn<Glib::ustring > _colLabel;    // Style or matching object id.
        Gtk::TreeModelColumn<Glib::ustring > _colValue;    // List of properties.
        Gtk::TreeModelColumn<bool> _colStrike;             // Propery not used, overloaded
    };
    ModelColumns _mColumns;

    // Widgets
    Gtk::ScrolledWindow _scrolledWindow;
    Gtk::Box   _mainBox;
    Gtk::Box   _styleBox;

    // Reading and writing the style element.
    Inkscape::XML::Node *_getStyleTextNode();
    void _readStyleElement();
    void _writeStyleElement();

    // Update watchers
    void _addWatcherRecursive(Inkscape::XML::Node *node);
    void _updateWatchers();    

    // Manipulate Tree
    std::vector<SPObject *> _getObjVec(Glib::ustring selector);
    std::map<Glib::ustring, Glib::ustring> parseStyle(Glib::ustring style_string);

    // Variables
    Inkscape::XML::Node *_textNode; // Track so we know when to add a NodeObserver.
    bool _updating;  // Prevent cyclic actions: read <-> write, select via dialog <-> via desktop

    // Signals and handlers - External
    sigc::connection _document_replaced_connection;
    sigc::connection _desktop_changed_connection;
    sigc::connection _selection_changed_connection;

    void _handleDocumentReplaced(SPDesktop* desktop, SPDocument *document);
    void _handleDesktopChanged(SPDesktop* desktop);
    void _handleSelectionChanged();
    void _closeDialog(Gtk::Dialog *textDialogPtr);
    DesktopTracker _desktopTracker;
};

} // namespace Dialogc
} // namespace UI
} // namespace Inkscape

#endif // STYLEDIALOG_H

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
