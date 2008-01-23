/* This file is part of the hildon-application-manager.
 * 
 * Parts of this file are derived from Modest.
 * 
 * Modest's legal notice:
 * Copyright (c) 2007, Nokia Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * * Neither the name of the Nokia Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived from
 *   this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
 * IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 * PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER
 * OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef MODEST_HBOX_CELL_RENDERER_H
#define MODEST_HBOX_CELL_RENDERER_H
#include <glib-object.h>
#include <gtk/gtkcellrenderer.h>

G_BEGIN_DECLS

#define MODEST_TYPE_HBOX_CELL_RENDERER             (modest_hbox_cell_renderer_get_type ())
#define MODEST_HBOX_CELL_RENDERER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), MODEST_TYPE_HBOX_CELL_RENDERER, ModestHBoxCellRenderer))
#define MODEST_HBOX_CELL_RENDERER_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), MODEST_TYPE_HBOX_CELL_RENDERER, ModestHBoxCellRendererClass))
#define MODEST_IS_HBOX_CELL_RENDERER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), MODEST_TYPE_HBOX_CELL_RENDERER))
#define MODEST_IS_HBOX_CELL_RENDERER_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), MODEST_TYPE_HBOX_CELL_RENDERER))
#define MODEST_HBOX_CELL_RENDERER_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_CLASS ((inst), MODEST_TYPE_HBOX_CELL_RENDERER, ModestHBoxCellRendererClass))

typedef struct _ModestHBoxCellRenderer ModestHBoxCellRenderer;
typedef struct _ModestHBoxCellRendererClass ModestHBoxCellRendererClass;

struct _ModestHBoxCellRenderer
{
	GtkCellRenderer parent;

};

struct _ModestHBoxCellRendererClass
{
	GtkCellRendererClass parent_class;

};

GType modest_hbox_cell_renderer_get_type (void);

GtkCellRenderer* modest_hbox_cell_renderer_new (void);

void modest_hbox_cell_renderer_append (ModestHBoxCellRenderer *hbox_renderer, GtkCellRenderer *cell, gboolean expand);

G_END_DECLS

#endif
