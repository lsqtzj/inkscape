#define __SP_URI_REFERENCES_C__

/*
 * Helper methods for resolving URI References
 *
 * Authors:
 *   Lauris Kaplinski <lauris@kaplinski.com>
 *
 * Copyright (C) 2001-2002 Lauris Kaplinski
 * Copyright (C) 2001 Ximian, Inc.
 *
 * Released under GNU GPL, read the file 'COPYING' for more information
 */

#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include "document.h"
#include "sp-object.h"
#include "uri-references.h"

static gchar *uri_to_id(SPDocument *document, const gchar *uri);

namespace Inkscape {

URIReference::URIReference(SPDocument *rel_document, const gchar *uri) {
	gchar *id = uri_to_id(rel_document, uri);
	if (!id) {
		throw UnsupportedURIException();
	}

	_obj = sp_document_lookup_id(rel_document, id);
	if (_obj) {
		sp_object_href(_obj, NULL);
	}

	_connection = sp_document_id_changed_connect(rel_document, id, SigC::slot(*this, &URIReference::_setObject));

	g_free(id);
}

URIReference::~URIReference() {
	/* SigC::Object's destructor will disconnect the connection for us
	 * automagically, but we store it and disconnect it here to avoid a
	 * potential race condition from the hunref triggering document
	 * changes.
	 */
	_connection.disconnect();
	if (_obj) {
		sp_object_hunref(_obj, NULL);
		_obj = NULL;
	}
}

void URIReference::_setObject(SPObject *obj) {

	if (obj == _obj) return;

	SPObject *old_obj=_obj;
	_obj = obj;

	if (_obj) {
		sp_object_href(_obj, NULL);
	}
	_changed_signal.emit(_obj);
	if (old_obj) {
		/* unref the old object _after_ the signal emission */
		sp_object_hunref(old_obj, NULL);
	}
}

}; /* namespace Inkscape */

static gchar *
uri_to_id(SPDocument *document, const gchar *uri)
{
	const gchar *e;
	gchar *id;
	gint len;

	g_return_val_if_fail (document != NULL, NULL);
	g_return_val_if_fail (SP_IS_DOCUMENT (document), NULL);

	if (!uri) return NULL;
	/* fixme: xpointer, everything */
	if (strncmp (uri, "url(#", 5)) return NULL;

	e = uri + 5;
	while (*e) {
		if (*e == ')') break;
		if (!isalnum (*e) && (*e != '_') && (*e != '-')) return NULL;
		e += 1;
		if (!*e) return NULL;
	}

	len = e - uri - 5;
	if (len < 1) return NULL;

	id = (gchar*)g_new(gchar, len + 1);
	memcpy (id, uri + 5, len);
	id[len] = '\0';

	return id;
}

SPObject *
sp_uri_reference_resolve (SPDocument *document, const gchar *uri)
{
	gchar *id;

	id = uri_to_id(document, uri);
	if (!id) return NULL;

	SPObject *ref;
	ref = sp_document_lookup_id (document, id);
	g_free(id);
	return ref;
}

