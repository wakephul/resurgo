/*
 * resurgo-pannello: il pannello in basso e il menu dei programmi.
 *
 * pannello: la r (apre il menu), rete, batteria se c'e', orologio.
 * menu: ricerca, elenco dei programmi, e in fondo impostazioni, riavvia, spegni.
 * il menu si apre anche col tasto Super (labwc manda SIGUSR1, vedi labwc/rc.xml).
 */
#include <gtk/gtk.h>
#include <gtk-layer-shell.h>
#include <glib-unix.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>

static GtkWidget *menu, *ricerca, *elenco, *orologio, *rete, *batteria;

/* i programmi che non vanno nel menu: le varianti di foot (c'e' gia' "Terminale") */
static const char *nascosti[] = {"foot.desktop", "footclient.desktop", "foot-server.desktop", NULL};

static const char *giorni[] = {"lun", "mar", "mer", "gio", "ven", "sab", "dom"};
static const char *mesi[] = {"gen", "feb", "mar", "apr", "mag", "giu", "lug", "ago", "set", "ott", "nov", "dic"};

static char *leggi(const char *percorso) {
	char *testo = NULL;
	if (!g_file_get_contents(percorso, &testo, NULL, NULL)) return NULL;
	return g_strstrip(testo);
}

static gboolean aggiorna(gpointer dati) {
	(void)dati;
	GDateTime *ora = g_date_time_new_now_local();
	char *t = g_strdup_printf("%s %d %s  %02d:%02d", giorni[g_date_time_get_day_of_week(ora) - 1],
		g_date_time_get_day_of_month(ora), mesi[g_date_time_get_month(ora) - 1],
		g_date_time_get_hour(ora), g_date_time_get_minute(ora));
	gtk_label_set_text(GTK_LABEL(orologio), t);
	g_free(t);
	g_date_time_unref(ora);

	/* rete: la prima interfaccia con un indirizzo ipv4, esclusa lo */
	struct ifaddrs *ifa, *i;
	char indirizzo[INET_ADDRSTRLEN] = "";
	if (getifaddrs(&ifa) == 0) {
		for (i = ifa; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, indirizzo, sizeof indirizzo);
			break;
		}
		freeifaddrs(ifa);
	}
	gtk_label_set_text(GTK_LABEL(rete), *indirizzo ? "Rete collegata" : "Nessuna rete");
	gtk_widget_set_tooltip_text(rete, *indirizzo ? indirizzo : NULL);

	/* batteria: la prima che c'e' */
	GDir *d = g_dir_open("/sys/class/power_supply", 0, NULL);
	const char *nome;
	gboolean trovata = FALSE;
	while (d && (nome = g_dir_read_name(d))) {
		char *p = g_strdup_printf("/sys/class/power_supply/%s/type", nome), *tipo = leggi(p);
		g_free(p);
		if (tipo && !strcmp(tipo, "Battery")) {
			p = g_strdup_printf("/sys/class/power_supply/%s/capacity", nome);
			char *carica = leggi(p);
			g_free(p);
			p = g_strdup_printf("/sys/class/power_supply/%s/status", nome);
			char *stato = leggi(p);
			g_free(p);
			char *testo = g_strdup_printf("Batteria %s%%%s", carica ? carica : "?",
				stato && !strcmp(stato, "Charging") ? ", in carica" : "");
			gtk_label_set_text(GTK_LABEL(batteria), testo);
			g_free(testo); g_free(carica); g_free(stato);
			trovata = TRUE;
		}
		g_free(tipo);
		if (trovata) break;
	}
	if (d) g_dir_close(d);
	gtk_widget_set_visible(batteria, trovata);
	return G_SOURCE_CONTINUE;
}

static void chiudi_menu(void) {
	gtk_widget_hide(menu);
	gtk_entry_set_text(GTK_ENTRY(ricerca), "");
}

static gboolean alterna_menu(gpointer dati) {
	(void)dati;
	if (gtk_widget_get_visible(menu)) {
		chiudi_menu();
	} else {
		gtk_widget_show_all(menu);
		gtk_widget_grab_focus(ricerca);
	}
	return G_SOURCE_CONTINUE;
}

static void lancia(const char *comando) {
	g_spawn_command_line_async(comando, NULL);
	chiudi_menu();
}

static void avvia_programma(GtkListBox *l, GtkListBoxRow *riga, gpointer dati) {
	(void)l; (void)dati;
	GAppInfo *app = g_object_get_data(G_OBJECT(riga), "app");
	if (app) g_app_info_launch(app, NULL, NULL, NULL);
	chiudi_menu();
}

static gboolean filtra(GtkListBoxRow *riga, gpointer dati) {
	(void)dati;
	const char *cerca = gtk_entry_get_text(GTK_ENTRY(ricerca));
	if (!*cerca) return TRUE;
	GAppInfo *app = g_object_get_data(G_OBJECT(riga), "app");
	char *nome = g_utf8_casefold(g_app_info_get_display_name(app), -1);
	char *chiave = g_utf8_casefold(cerca, -1);
	gboolean ok = strstr(nome, chiave) != NULL;
	g_free(nome); g_free(chiave);
	return ok;
}

static void ricerca_cambiata(GtkSearchEntry *e, gpointer dati) {
	(void)e; (void)dati;
	gtk_list_box_invalidate_filter(GTK_LIST_BOX(elenco));
}

/* invio nella ricerca: parte il primo programma visibile */
static void ricerca_invio(GtkEntry *e, gpointer dati) {
	(void)e; (void)dati;
	for (int n = 0;; n++) {
		GtkListBoxRow *riga = gtk_list_box_get_row_at_index(GTK_LIST_BOX(elenco), n);
		if (!riga) break;
		if (gtk_widget_get_child_visible(GTK_WIDGET(riga))) { avvia_programma(NULL, riga, NULL); return; }
	}
}

static gboolean tasto_menu(GtkWidget *w, GdkEventKey *ev, gpointer dati) {
	(void)w; (void)dati;
	if (ev->keyval == GDK_KEY_Escape) { chiudi_menu(); return TRUE; }
	return FALSE;
}

static gint per_nome(gconstpointer a, gconstpointer b) {
	return g_utf8_collate(g_app_info_get_display_name((GAppInfo *)a), g_app_info_get_display_name((GAppInfo *)b));
}

static void riempi_elenco(void) {
	GList *apps = g_list_sort(g_app_info_get_all(), per_nome);
	for (GList *a = apps; a; a = a->next) {
		GAppInfo *app = a->data;
		const char *id = g_app_info_get_id(app);
		gboolean nascosto = !g_app_info_should_show(app);
		for (int k = 0; nascosti[k] && id; k++) if (!strcmp(id, nascosti[k])) nascosto = TRUE;
		if (nascosto) continue;
		GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
		GIcon *icona = g_app_info_get_icon(app);
		GtkWidget *img = icona ? gtk_image_new_from_gicon(icona, GTK_ICON_SIZE_LARGE_TOOLBAR) : gtk_image_new();
		gtk_image_set_pixel_size(GTK_IMAGE(img), 24);
		GtkWidget *nome = gtk_label_new(g_app_info_get_display_name(app));
		gtk_label_set_xalign(GTK_LABEL(nome), 0);
		gtk_label_set_ellipsize(GTK_LABEL(nome), PANGO_ELLIPSIZE_END);
		gtk_box_pack_start(GTK_BOX(riga), img, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(riga), nome, TRUE, TRUE, 0);
		GtkWidget *r = gtk_list_box_row_new();
		gtk_container_add(GTK_CONTAINER(r), riga);
		g_object_set_data_full(G_OBJECT(r), "app", g_object_ref(app), g_object_unref);
		gtk_list_box_insert(GTK_LIST_BOX(elenco), r, -1);
	}
	g_list_free_full(apps, g_object_unref);
}

static GtkWidget *pulsante(const char *testo, const char *comando) {
	GtkWidget *b = gtk_button_new_with_label(testo);
	g_signal_connect_swapped(b, "clicked", G_CALLBACK(lancia), (gpointer)comando);
	return b;
}

static void crea_menu(void) {
	menu = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_layer_init_for_window(GTK_WINDOW(menu));
	gtk_layer_set_layer(GTK_WINDOW(menu), GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_anchor(GTK_WINDOW(menu), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(menu), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_keyboard_mode(GTK_WINDOW(menu), GTK_LAYER_SHELL_KEYBOARD_MODE_EXCLUSIVE);
	gtk_style_context_add_class(gtk_widget_get_style_context(menu), "resurgo-menu");
	/* sta comodo anche a 1024x600: 360 x 440, sopra un pannello di 34 */
	gtk_window_set_default_size(GTK_WINDOW(menu), 360, 440);
	g_signal_connect(menu, "key-press-event", G_CALLBACK(tasto_menu), NULL);

	GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
	gtk_container_set_border_width(GTK_CONTAINER(scatola), 12);
	ricerca = gtk_search_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(ricerca), "Cerca un programma");
	g_signal_connect(ricerca, "search-changed", G_CALLBACK(ricerca_cambiata), NULL);
	g_signal_connect(ricerca, "activate", G_CALLBACK(ricerca_invio), NULL);

	elenco = gtk_list_box_new();
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(elenco), TRUE);
	gtk_list_box_set_filter_func(GTK_LIST_BOX(elenco), filtra, NULL, NULL);
	g_signal_connect(elenco, "row-activated", G_CALLBACK(avvia_programma), NULL);
	riempi_elenco();
	GtkWidget *scorri = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	/* le finestre del pannello prendono la misura naturale: l'elenco deve chiedere la sua altezza */
	gtk_scrolled_window_set_min_content_height(GTK_SCROLLED_WINDOW(scorri), 320);
	gtk_scrolled_window_set_min_content_width(GTK_SCROLLED_WINDOW(scorri), 320);
	gtk_container_add(GTK_CONTAINER(scorri), elenco);

	GtkWidget *fondo = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(fondo), pulsante("Impostazioni", "resurgo-impostazioni"), FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(fondo), pulsante("Spegni", "doas -n /sbin/poweroff"), FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(fondo), pulsante("Riavvia", "doas -n /sbin/reboot"), FALSE, FALSE, 0);

	gtk_box_pack_start(GTK_BOX(scatola), ricerca, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(scatola), scorri, TRUE, TRUE, 0);
	gtk_box_pack_start(GTK_BOX(scatola), fondo, FALSE, FALSE, 0);
	gtk_container_add(GTK_CONTAINER(menu), scatola);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);

	GtkWidget *barra = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_layer_init_for_window(GTK_WINDOW(barra));
	gtk_layer_set_layer(GTK_WINDOW(barra), GTK_LAYER_SHELL_LAYER_TOP);
	gtk_layer_set_anchor(GTK_WINDOW(barra), GTK_LAYER_SHELL_EDGE_BOTTOM, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(barra), GTK_LAYER_SHELL_EDGE_LEFT, TRUE);
	gtk_layer_set_anchor(GTK_WINDOW(barra), GTK_LAYER_SHELL_EDGE_RIGHT, TRUE);
	gtk_layer_auto_exclusive_zone_enable(GTK_WINDOW(barra));
	gtk_style_context_add_class(gtk_widget_get_style_context(barra), "resurgo-pannello");
	gtk_widget_set_size_request(barra, -1, 34);

	GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
	gtk_container_set_border_width(GTK_CONTAINER(scatola), 2);
	GtkWidget *r = gtk_button_new();
	GdkPixbuf *logo = gdk_pixbuf_new_from_file_at_size("/usr/share/resurgo/r.svg", 20, 20, NULL);
	gtk_button_set_image(GTK_BUTTON(r), logo ? gtk_image_new_from_pixbuf(logo) : gtk_label_new("r"));
	gtk_widget_set_tooltip_text(r, "Programmi (tasto Super)");
	g_signal_connect_swapped(r, "clicked", G_CALLBACK(alterna_menu), NULL);
	orologio = gtk_label_new("");
	rete = gtk_label_new("");
	batteria = gtk_label_new("");
	gtk_box_pack_start(GTK_BOX(scatola), r, FALSE, FALSE, 0);
	gtk_box_pack_end(GTK_BOX(scatola), orologio, FALSE, FALSE, 10);
	gtk_box_pack_end(GTK_BOX(scatola), batteria, FALSE, FALSE, 10);
	gtk_box_pack_end(GTK_BOX(scatola), rete, FALSE, FALSE, 10);
	gtk_container_add(GTK_CONTAINER(barra), scatola);

	crea_menu();
	aggiorna(NULL);
	g_timeout_add_seconds(10, aggiorna, NULL);
	g_unix_signal_add(SIGUSR1, alterna_menu, NULL);

	gtk_widget_show_all(barra);
	gtk_widget_set_visible(batteria, strlen(gtk_label_get_text(GTK_LABEL(batteria))) > 0);
	gtk_main();
	return 0;
}
