/*
 * This file is part of the Nice GLib ICE library.
 *
 * (C) 2010, 2013 Collabora Ltd.
 *  Contact: Youness Alaoui
 *
 * The contents of this file are subject to the Mozilla Public License Version
 * 1.1 (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 * http://www.mozilla.org/MPL/
 *
 * Software distributed under the License is distributed on an "AS IS" basis,
 * WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
 * for the specific language governing rights and limitations under the
 * License.
 *
 * The Original Code is the Nice GLib ICE library.
 *
 * The Initial Developers of the Original Code are Collabora Ltd and Nokia
 * Corporation. All Rights Reserved.
 *
 * Contributors:
 *   Youness Alaoui, Collabora Ltd.
 *
 * Alternatively, the contents of this file may be used under the terms of the
 * the GNU Lesser General Public License Version 2.1 (the "LGPL"), in which
 * case the provisions of LGPL are applicable instead of those above. If you
 * wish to allow use of your version of this file only under the terms of the
 * LGPL and not to allow others to use your version of this file under the
 * MPL, indicate your decision by deleting the provisions above and replace
 * them with the notice and other provisions required by the LGPL. If you do
 * not delete the provisions above, a recipient may use your version of this
 * file under either the MPL or the LGPL.
 */

#ifndef __NICE_OUTPUT_STREAM_H__
#define __NICE_OUTPUT_STREAM_H__

#include <glib-object.h>
#include <gio/gio.h>
#include "agent.h"


/* TYPE MACROS */
#define NICE_TYPE_OUTPUT_STREAM  (nice_output_stream_get_type ())
#define NICE_OUTPUT_STREAM(obj)   (G_TYPE_CHECK_INSTANCE_CAST((obj), NICE_TYPE_OUTPUT_STREAM,  NiceOutputStream))
#define NICE_OUTPUT_STREAM_CLASS(klass)  (G_TYPE_CHECK_CLASS_CAST((klass), NICE_TYPE_OUTPUT_STREAM, NiceOutputStreamClass))
#define NICE_IS_OUTPUT_STREAM(obj)   (G_TYPE_CHECK_INSTANCE_TYPE((obj), NICE_TYPE_OUTPUT_STREAM))
#define NICE_IS_OUTPUT_STREAM_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), NICE_TYPE_OUTPUT_STREAM))
#define NICE_OUTPUT_STREAM_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), NICE_TYPE_OUTPUT_STREAM, NiceOutputStreamClass))

typedef struct _NiceOutputStreamPrivate    NiceOutputStreamPrivate;
typedef struct _NiceOutputStreamClass  NiceOutputStreamClass;
typedef struct _NiceOutputStream NiceOutputStream;


GType nice_output_stream_get_type (void);

struct _NiceOutputStreamClass
{
  GOutputStreamClass parent_class;
};

struct _NiceOutputStream
{
  GOutputStream parent_instance;
  NiceOutputStreamPrivate *priv;
};


NiceOutputStream *nice_output_stream_new (NiceAgent *agent, uint32_t stream_id, uint32_t component_id);

#endif /* __NICE_OUTPUT_STREAM_H__ */
