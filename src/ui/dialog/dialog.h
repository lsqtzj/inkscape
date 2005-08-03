/**
 * \brief Base class for dialogs in Inkscape.  This class provides certain
 *        common behaviors and styles wanted of all dialogs in the application.
 *
 * Author:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004, 2005 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifndef INKSCAPE_DIALOG_H
#define INKSCAPE_DIALOG_H

#include <gtkmm.h>

namespace Inkscape { class Selection; }
class SPDesktop;

namespace Inkscape {
namespace UI {
namespace Dialog {

class Dialog : public Gtk::Dialog {
public:
    Dialog(BaseObjectType *gobj); // fixme: remove this

    Dialog(const char *prefs_path, int verb_num, const char *apply_label = NULL);

    virtual ~Dialog();

    /** Hide and show dialogs */
    virtual void   onHideF12();
    virtual void   onShowF12();

    bool           _hiddenF12;
    bool           _user_hidden; // when it is closed by the user, to prevent repopping on f12

    void           read_geometry();
    void           save_geometry();

    const char           *_prefs_path;

    bool retransientize_suppress; // when true, do not retransientize (prevents races when switching new windows too fast)

protected:

    /**
     * Tooltips object for all descendants to use
     */
    Gtk::Tooltips tooltips;

    virtual void   on_response(int response_id);
    virtual void   _apply();
    virtual void   _close();

    static bool windowKeyPress( GtkWidget *widget, GdkEventKey *event );

    Inkscape::Selection*   _getSelection();

private:
    Dialog(); // no constructor without params

    Dialog(Dialog const &d);            // no copy
    Dialog& operator=(Dialog const &d); // no assign
};

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

#endif //INKSCAPE_DIALOG_H

/*
  Local Variables:
  mode:c++
  c-file-style:"stroustrup"
  c-file-offsets:((innamespace . 0)(inline-open . 0))
  indent-tabs-mode:nil
  fill-column:99
  End:
*/
// vim: filetype=c++:expandtab:shiftwidth=4:tabstop=8:softtabstop=4 :
