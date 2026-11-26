/*
 * resurgo-benvenuto: la finestra del primo avvio.
 * mostra cosa ha regolato mantice, area per area, con un interruttore per spegnere ogni regolazione
 * (vale dal prossimo avvio: finisce in /var/lib/mantice/disattivati).
 * esce subito se l'utente l'ha gia' chiusa una volta (~/.config/resurgo/benvenuto-visto);
 * con --sempre si apre comunque (per le prove e dalle impostazioni).
 */
#include <gtk/gtk.h>
#include <string.h>

#define FATTO "/var/lib/mantice/fatto"
#define DISATTIVATI "/var/lib/mantice/disattivati"

static const struct { const char *area, *nome; } aree[] = {
	{"memoria", "Memoria"},
	{"processore", "Processore"},
	{"disco", "Disco"},
	{"grafica", "Grafica"},
};

static char *visto(void) {
	return g_build_filename(g_get_user_config_dir(), "resurgo", "benvenuto-visto", NULL);
}

static gboolean disattivata(const char *area) {
	char *elenco = NULL;
	gboolean si = FALSE;
	if (g_file_get_contents(DISATTIVATI, &elenco, NULL, NULL)) {
		char **righe = g_strsplit(elenco, "\n", -1);
		for (int k = 0; righe[k]; k++) if (!strcmp(g_strstrip(righe[k]), area)) si = TRUE;
		g_strfreev(righe);
		g_free(elenco);
	}
	return si;
}

/* riscrive disattivati con l'area tolta o aggiunta */
static gboolean cambia(GtkSwitch *interruttore, gboolean acceso, gpointer area) {
	GString *nuovo = g_string_new("");
	for (guint k = 0; k < G_N_ELEMENTS(aree); k++) {
		gboolean spenta = !strcmp(aree[k].area, area) ? !acceso : disattivata(aree[k].area);
		if (spenta) g_string_append_printf(nuovo, "%s\n", aree[k].area);
	}
	GError *err = NULL;
	if (!g_file_set_contents(DISATTIVATI, nuovo->str, -1, &err)) {
		g_printerr("resurgo-benvenuto: %s\n", err->message);
		g_error_free(err);
		g_string_free(nuovo, TRUE);
		return TRUE; /* l'interruttore resta com'era */
	}
	g_string_free(nuovo, TRUE);
	gtk_switch_set_state(interruttore, acceso);
	return TRUE;
}

static void inizia(GtkButton *b, GtkWidget *finestra) {
	(void)b;
	char *f = visto(), *d = g_path_get_dirname(f);
	g_mkdir_with_parents(d, 0700);
	g_file_set_contents(f, "", 0, NULL);
	g_free(f); g_free(d);
	gtk_widget_destroy(finestra);
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	char *f = visto();
	gboolean sempre = argc > 1 && !strcmp(argv[1], "--sempre");
	if (!sempre && g_file_test(f, G_FILE_TEST_EXISTS)) return 0;
	g_free(f);

	char *fatto = NULL;
	g_file_get_contents(FATTO, &fatto, NULL, NULL);
	char **righe = g_strsplit(fatto ? fatto : "", "\n", -1);

	GtkWidget *finestra = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(finestra), "Benvenuto");
	gtk_window_set_default_size(GTK_WINDOW(finestra), 640, 460);
	g_signal_connect(finestra, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(scatola), 24);
	GtkWidget *titolo = gtk_label_new("Benvenuto in Resurgo");
	gtk_style_context_add_class(gtk_widget_get_style_context(titolo), "titolo");
	gtk_label_set_xalign(GTK_LABEL(titolo), 0);
	GtkWidget *intro = gtk_label_new("Ho guardato questo computer e l'ho regolato. Se una regolazione non ti va, "
		"spegnila qui: vale dal prossimo avvio.");
	gtk_label_set_xalign(GTK_LABEL(intro), 0);
	gtk_label_set_line_wrap(GTK_LABEL(intro), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(intro), 64);
	gtk_box_pack_start(GTK_BOX(scatola), titolo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(scatola), intro, FALSE, FALSE, 0);

	GtkWidget *elenco = gtk_list_box_new();
	gtk_list_box_set_selection_mode(GTK_LIST_BOX(elenco), GTK_SELECTION_NONE);
	for (guint k = 0; k < G_N_ELEMENTS(aree); k++) {
		/* le righe del rapporto di questa area */
		GString *dettagli = g_string_new("");
		for (int j = 0; righe[j]; j++) {
			char **parti = g_strsplit(righe[j], "\t", 2);
			if (parti[0] && parti[1] && !strcmp(parti[0], aree[k].area))
				g_string_append_printf(dettagli, "%s%s", dettagli->len ? "\n" : "", parti[1]);
			g_strfreev(parti);
		}
		GtkWidget *riga = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 16);
		gtk_container_set_border_width(GTK_CONTAINER(riga), 6);
		GtkWidget *testi = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
		GtkWidget *nome = gtk_label_new(aree[k].nome);
		gtk_style_context_add_class(gtk_widget_get_style_context(nome), "sottotitolo");
		gtk_label_set_xalign(GTK_LABEL(nome), 0);
		GtkWidget *cosa = gtk_label_new(dettagli->len ? dettagli->str : "niente da regolare su questo computer");
		gtk_style_context_add_class(gtk_widget_get_style_context(cosa), "nota");
		gtk_label_set_xalign(GTK_LABEL(cosa), 0);
		gtk_label_set_line_wrap(GTK_LABEL(cosa), TRUE);
		gtk_label_set_max_width_chars(GTK_LABEL(cosa), 52);
		gtk_box_pack_start(GTK_BOX(testi), nome, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(testi), cosa, FALSE, FALSE, 0);
		GtkWidget *interruttore = gtk_switch_new();
		gtk_switch_set_active(GTK_SWITCH(interruttore), !disattivata(aree[k].area));
		gtk_widget_set_valign(interruttore, GTK_ALIGN_CENTER);
		g_signal_connect(interruttore, "state-set", G_CALLBACK(cambia), (gpointer)aree[k].area);
		gtk_box_pack_start(GTK_BOX(riga), testi, TRUE, TRUE, 0);
		gtk_box_pack_end(GTK_BOX(riga), interruttore, FALSE, FALSE, 0);
		gtk_list_box_insert(GTK_LIST_BOX(elenco), riga, -1);
		g_string_free(dettagli, TRUE);
	}
	GtkWidget *scorri = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scorri), elenco);
	gtk_box_pack_start(GTK_BOX(scatola), scorri, TRUE, TRUE, 0);

	GtkWidget *via = gtk_button_new_with_label("Inizia");
	gtk_style_context_add_class(gtk_widget_get_style_context(via), "suggested-action");
	gtk_widget_set_halign(via, GTK_ALIGN_END);
	g_signal_connect(via, "clicked", G_CALLBACK(inizia), finestra);
	gtk_box_pack_start(GTK_BOX(scatola), via, FALSE, FALSE, 0);

	gtk_container_add(GTK_CONTAINER(finestra), scatola);
	gtk_widget_show_all(finestra);
	g_strfreev(righe);
	g_free(fatto);
	gtk_main();
	return 0;
}
