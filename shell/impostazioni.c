/*
 * resurgo-impostazioni: prima versione, solo da leggere.
 * pagine: questo computer (versione e rapporto di mantice), rete, privacy, aggiornamenti, schermo, informazioni.
 * le cose da cambiare arriveranno pagina per pagina, ognuna con il suo controllo dei permessi.
 */
#include <gtk/gtk.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>

static char *leggi(const char *percorso) {
	char *testo = NULL;
	if (!g_file_get_contents(percorso, &testo, NULL, NULL)) return NULL;
	return g_strstrip(testo);
}

static char *esegui(const char *comando) {
	char *uscita = NULL;
	if (!g_spawn_command_line_sync(comando, &uscita, NULL, NULL, NULL)) return NULL;
	return uscita ? g_strstrip(uscita) : NULL;
}

/* una pagina: titolo, poi righe "voce: valore" in due colonne, poi testo libero */
static GtkWidget *nuova_pagina(const char *titolo, GtkWidget **griglia) {
	GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
	gtk_container_set_border_width(GTK_CONTAINER(scatola), 20);
	GtkWidget *t = gtk_label_new(titolo);
	gtk_style_context_add_class(gtk_widget_get_style_context(t), "titolo");
	gtk_label_set_xalign(GTK_LABEL(t), 0);
	gtk_box_pack_start(GTK_BOX(scatola), t, FALSE, FALSE, 0);
	*griglia = gtk_grid_new();
	gtk_grid_set_row_spacing(GTK_GRID(*griglia), 8);
	gtk_grid_set_column_spacing(GTK_GRID(*griglia), 16);
	gtk_box_pack_start(GTK_BOX(scatola), *griglia, FALSE, FALSE, 0);
	GtkWidget *scorri = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scorri), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scorri), scatola);
	g_object_set_data(G_OBJECT(scorri), "scatola", scatola);
	return scorri;
}

static void riga(GtkWidget *griglia, const char *voce, const char *valore) {
	int n = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(griglia), "righe"));
	GtkWidget *v = gtk_label_new(voce);
	gtk_style_context_add_class(gtk_widget_get_style_context(v), "nota");
	gtk_label_set_xalign(GTK_LABEL(v), 0);
	gtk_label_set_yalign(GTK_LABEL(v), 0);
	GtkWidget *d = gtk_label_new(valore && *valore ? valore : "non disponibile");
	gtk_label_set_xalign(GTK_LABEL(d), 0);
	gtk_label_set_line_wrap(GTK_LABEL(d), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(d), 52);
	gtk_label_set_selectable(GTK_LABEL(d), TRUE);
	gtk_grid_attach(GTK_GRID(griglia), v, 0, n, 1, 1);
	gtk_grid_attach(GTK_GRID(griglia), d, 1, n, 1, 1);
	g_object_set_data(G_OBJECT(griglia), "righe", GINT_TO_POINTER(n + 1));
}

static void testo(GtkWidget *pagina, const char *contenuto) {
	GtkWidget *l = gtk_label_new(contenuto);
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_line_wrap(GTK_LABEL(l), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(l), 70);
	gtk_label_set_selectable(GTK_LABEL(l), TRUE);
	gtk_box_pack_start(GTK_BOX(g_object_get_data(G_OBJECT(pagina), "scatola")), l, FALSE, FALSE, 0);
}

static GtkWidget *computer(void) {
	GtkWidget *g, *p = nuova_pagina("Questo computer", &g);
	char *versione = leggi("/etc/resurgo-versione");
	char *v = g_strdup_printf("Resurgo %s", versione ? versione : "?");
	riga(g, "Sistema", v);
	g_free(v); g_free(versione);
	char *rapporto = esegui("mantice mostra");
	testo(p, rapporto && *rapporto ? rapporto : "Mantice non ha ancora scritto il suo rapporto.");
	g_free(rapporto);
	return p;
}

static GtkWidget *rete(void) {
	GtkWidget *g, *p = nuova_pagina("Rete", &g);
	struct ifaddrs *ifa, *i;
	int trovate = 0;
	if (getifaddrs(&ifa) == 0) {
		for (i = ifa; i; i = i->ifa_next) {
			if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK)) continue;
			char indirizzo[INET_ADDRSTRLEN];
			inet_ntop(AF_INET, &((struct sockaddr_in *)i->ifa_addr)->sin_addr, indirizzo, sizeof indirizzo);
			riga(g, i->ifa_name, indirizzo);
			trovate++;
		}
		freeifaddrs(ifa);
	}
	if (!trovate) riga(g, "Collegamento", "nessuna rete collegata");
	return p;
}

static GtkWidget *privacy(void) {
	GtkWidget *g, *p = nuova_pagina("Privacy", &g);
	char *conf = leggi("/etc/unbound/unbound.conf");
	gboolean cifrato = conf && strstr(conf, "forward-tls-upstream: yes");
	riga(g, "DNS", cifrato ? "cifrato, verso Quad9 (DNS su TLS)"
		: "ricorsivo: Resurgo chiede ai server radice, in chiaro. Serve sulle reti che bloccano il DNS cifrato.");
	g_free(conf);
	char *elenco = leggi("/usr/share/favilla/dns-blocco.conf");
	if (elenco) {
		int n = 0;
		for (char *c = elenco; *c; c++) if (*c == '\n') n++;
		char *t = g_strdup_printf("%d domini di pubblicità e tracciamento bloccati per tutto il sistema", n + 1);
		riga(g, "Blocco", t);
		g_free(t); g_free(elenco);
	} else {
		riga(g, "Blocco", "nessun elenco installato");
	}
	riga(g, "Telemetria", "nessuna: Resurgo non manda niente a nessuno");
	return p;
}

static GtkWidget *aggiornamenti(void) {
	GtkWidget *g, *p = nuova_pagina("Aggiornamenti", &g);
	char *versione = leggi("/etc/resurgo-versione");
	riga(g, "Versione", versione);
	g_free(versione);
	char *cmd = leggi("/proc/cmdline");
	char *slot = cmd ? strstr(cmd, "resurgo.slot=") : NULL;
	char *s = g_strdup_printf("sistema %c (l'altro tiene la versione precedente o la prossima)", slot ? slot[13] : '?');
	riga(g, "In uso", s);
	g_free(s); g_free(cmd);
	char *avviso = leggi("/var/lib/resurgo/avviso");
	if (avviso) { riga(g, "Ultimo aggiornamento", avviso); g_free(avviso); }
	testo(p, "Gli aggiornamenti arrivano firmati e si installano nel sistema che non è in uso. "
		"Se la versione nuova non parte, dopo tre tentativi torna da sola quella di prima.");
	return p;
}

static GtkWidget *schermo(void) {
	GtkWidget *g, *p = nuova_pagina("Schermo", &g);
	char *uscite = esegui("wlr-randr");
	char **righe = uscite ? g_strsplit(uscite, "\n", -1) : NULL;
	for (int k = 0; righe && righe[k]; k++) {
		/* il nome dell'uscita e la modalita' in uso */
		if (righe[k][0] && righe[k][0] != ' ') riga(g, "Uscita", righe[k]);
		else if (strstr(righe[k], "current")) riga(g, "Risoluzione", g_strstrip(righe[k]));
	}
	g_strfreev(righe);
	g_free(uscite);
	return p;
}

static GtkWidget *informazioni(void) {
	GtkWidget *g, *p = nuova_pagina("Informazioni", &g);
	testo(p, "Resurgo è un sistema operativo libero per far tornare utilizzabili i computer vecchi. "
		"Il codice è GPL-3.0, logo e documentazione CC BY-SA 4.0.");
	riga(g, "Progetto", "github.com/wakephul/resurgo");
	return p;
}

int main(int argc, char **argv) {
	gtk_init(&argc, &argv);
	GtkWidget *finestra = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(finestra), "Impostazioni");
	/* entra in uno schermo da 1024x600 con il pannello */
	gtk_window_set_default_size(GTK_WINDOW(finestra), 820, 500);
	g_signal_connect(finestra, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *pile = gtk_stack_new();
	gtk_stack_set_transition_type(GTK_STACK(pile), GTK_STACK_TRANSITION_TYPE_NONE);
	gtk_stack_add_titled(GTK_STACK(pile), computer(), "computer", "Questo computer");
	gtk_stack_add_titled(GTK_STACK(pile), rete(), "rete", "Rete");
	gtk_stack_add_titled(GTK_STACK(pile), privacy(), "privacy", "Privacy");
	gtk_stack_add_titled(GTK_STACK(pile), aggiornamenti(), "aggiornamenti", "Aggiornamenti");
	gtk_stack_add_titled(GTK_STACK(pile), schermo(), "schermo", "Schermo");
	gtk_stack_add_titled(GTK_STACK(pile), informazioni(), "informazioni", "Informazioni");
	GtkWidget *lato = gtk_stack_sidebar_new();
	gtk_stack_sidebar_set_stack(GTK_STACK_SIDEBAR(lato), GTK_STACK(pile));
	gtk_widget_set_size_request(lato, 180, -1);

	GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_box_pack_start(GTK_BOX(scatola), lato, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(scatola), pile, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(finestra), scatola);
	gtk_widget_show_all(finestra);
	/* la pagina da aprire (resurgo-impostazioni rete): solo dopo show_all, le pagine nascoste non si scelgono */
	if (argc > 1) gtk_stack_set_visible_child_name(GTK_STACK(pile), argv[1]);
	gtk_main();
	return 0;
}
