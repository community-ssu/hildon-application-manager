#ifndef _HILDON_FANCY_BUTTON_H_
#define _HILDON_FANCY_BUTTON_H_

#include <gtk/gtk.h>

G_BEGIN_DECLS

typedef struct _HildonFancyButton      HildonFancyButton;
typedef struct _HildonFancyButtonClass HildonFancyButtonClass;

GType hildon_fancy_button_get_type (void);

#define HILDON_TYPE_STRING_FANCY_BUTTON "HildonFancyButton"
#define HILDON_TYPE_FANCY_BUTTON (hildon_fancy_button_get_type ())
#define HILDON_FANCY_BUTTON(object)           (G_TYPE_CHECK_INSTANCE_CAST ((object), HILDON_TYPE_FANCY_BUTTON, HildonFancyButton))
#define HILDON_IS_FANCY_BUTTON(object)        (G_TYPE_CHECK_INSTANCE_TYPE ((object), HILDON_TYPE_FANCY_BUTTON))
#define HILDON_FANCY_BUTTON_GET_CLASS(object) (G_TYPE_INSTANCE_GET_CLASS ((object),  HILDON_TYPE_FANCY_BUTTON, HildonFancyButtonClass))
#define HILDON_FANCY_BUTTON_CLASS(klass)      (G_TYPE_CHECK_CLASS_CAST ((klass),     HILDON_TYPE_FANCY_BUTTON, HildonFancyButtonClass))
#define HILDON_IS_FANCY_BUTTON_CLASS(klass)   (G_TYPE_CHECK_CLASS_TYPE ((klass),     HILDON_TYPE_FANCY_BUTTON))

G_END_DECLS

#endif /* !_HILDON_FANCY_BUTTON_H_ */
