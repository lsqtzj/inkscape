/**
 * \brief Text Properties dialog
 *
 * Authors:
 *   Bryce W. Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004, 2005 Authors
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information.
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include "text-properties.h"
#include "verbs.h"

namespace Inkscape {
namespace UI {
namespace Dialog {

TextProperties::TextProperties() 
    : Dialog ("dialogs.textandfont", SP_VERB_DIALOG_TEXT),
      _page_font("Font", 1, 1),
      _page_text("Text", 1, 1)
{
    // Top level vbox
    Gtk::VBox *vbox = get_vbox();
    vbox->set_spacing(4);

    // Notebook for individual transformations
    vbox->pack_start(_notebook, true, true);

    // TODO:  Insert widgets
    _notebook.append_page(_page_font, _("Font"));
    _notebook.append_page(_page_text, _("Text"));

    show_all_children();
}

TextProperties::~TextProperties() 
{
}

} // namespace Dialog
} // namespace UI
} // namespace Inkscape

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
