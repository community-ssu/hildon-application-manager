localedir = $(datadir)/locale

LINGUAS = en_GB es_MX nl_NL pt_BR no_NO fi_FI it_IT fr_CA ru_RU de_DE	\
          da_DK en_US pt_PT sv_SE fr_FR es_ES

textdomain = hildon-application-manager-categories

install-data-local:
	for l in $(LINGUAS); do \
	  mkdir -p $(DESTDIR)$(localedir)/$$l/LC_MESSAGES/; \
	  msgfmt $$l.po \
          -o $(DESTDIR)$(localedir)/$$l/LC_MESSAGES/$(textdomain).mo; \
	done

# Some time saving hacks to get started.

origindomain = hildon-application-manager

get:
	for l in $(LINGUAS); do \
	  msgunfmt $(localedir)/$$l/LC_MESSAGES/$(origindomain).mo | \
          grep -v ai_category_other | \
          grep -v ai_category_all | \
	  grep -A 1 ai_category | \
          sed -e 's/--//' -e 's/ai_category_//' >$$l.po; \
	done
