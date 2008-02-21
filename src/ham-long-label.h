#ifndef HAMLONGLABEL_H_
#define HAMLONGLABEL_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define HAM_TYPE_LONG_LABEL             (ham_long_label_get_type ())
#define HAM_LONG_LABEL(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), HAM_TYPE_LONG_LABEL, HAMLongLabel))
#define HAM_LONG_LABEL_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), HAM_TYPE_LONG_LABEL, HAMLongLabelClass))
#define HAM_IS_LONG_LABEL(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), HAM_TYPE_LONG_LABEL))
#define HAM_IS_LONG_LABEL_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), HAM_TYPE_LONG_LABEL))
#define HAM_LONG_LABEL_GET_CLASS(inst)  (G_TYPE_INSTANCE_GET_CLASS ((inst), HAM_TYPE_LONG_LABEL, HAMLongLabelClass))

typedef struct _HamLongLabel HamLongLabel;
typedef struct _HamLongLabelClass HamLongLabelClass;

struct _HamLongLabel
{
    GtkHBox parent;

};

struct _HamLongLabelClass
{
    GtkHBoxClass parent_class;

};

GType ham_long_label_get_type (void);

GtkWidget *ham_long_label_new (const gchar *text);

GtkWidget *ham_long_label_new_with_markup (const gchar *markup);

void ham_long_label_set_text (HamLongLabel *hll, const gchar *text);

void ham_long_label_set_markup (HamLongLabel *hll, const gchar *markup);

G_END_DECLS

#endif /*HAMLONGLABEL_H_*/
