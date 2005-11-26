/**
 * \brief Notebook page widget
 *
 * Author:
 *   Bryce Harrington <bryce@bryceharrington.org>
 *
 * Copyright (C) 2004 Bryce Harrington
 *
 * Released under GNU GPL.  Read the file 'COPYING' for more information
 */

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <gtkmm/frame.h>
#include <gtkmm/label.h>
#include <gtkmm/table.h>
#include "notebook-page.h"

namespace Inkscape {
namespace UI {
namespace Widget {

/**
 *    Construct a NotebookPage
 *
 *    \param label Label.
 */
  
NotebookPage::NotebookPage(int n_rows, int n_columns)
    :_table(n_rows, n_columns)
{
    set_border_width(2);
    _table.set_spacings(2);
    pack_start(_table, false, false, 0);
}

} // namespace Widget
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
