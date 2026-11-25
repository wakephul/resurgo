/*
 * favilla: prototipo del browser di resurgo (tappa 2).
 * gtk 3 + webkitgtk 4.1, blocco dei contenuti con il filtro nativo di webkit.
 *
 * uso:
 *   favilla [url...]                       apre le pagine in schede
 *   favilla --misura 60 url1 url2 ...      apre le pagine, aspetta che finiscano di caricare
 *                                          (al massimo 60 s) e scrive i risultati su stdout
 *   favilla --senza-blocco ...             senza filtro dei contenuti
 *   favilla --con-media ...                con il video dentro la pagina (spento di default)
 *
 * schede addormentate: oltre il limite scritto da mantice in /run/mantice/favilla (schede_attive=N,
 * 0 = nessun limite) la scheda usata meno di recente si chiude, ricordando l'indirizzo, e si
 * riapre quando ci torni. la variabile FAVILLA_SCHEDE scavalca il file (per le prove).
 *
 * per la misura il titolo della pagina finisce nell'output: le pagine di prova ci scrivono
 * i loro risultati (per esempio il tempo del calcolo javascript).
 */
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#include <stdio.h>
#include <string.h>

#define FILTRO_JSON "/usr/share/favilla/blocco.json"
#define FILTRO_PRONTO "/usr/share/favilla/filtri" /* compilato gia' nel pacchetto */
#define FILTRO_ID "resurgo"

typedef struct {
	WebKitWebView *vista;      /* NULL quando la scheda dorme */
	GtkWidget *contenitore;    /* la pagina del quaderno: dentro c'e' la vista o la scritta */
	GtkWidget *etichetta;
	char *titolo;              /* tenuto da parte per quando la scheda dorme */
	gint64 uso;                /* ultima volta in primo piano */
	int addormentata;
	char *url;
	gint64 inizio;
	double secondi;
	int finito;
	const char *fine_processo;
} Scheda;

static GtkWidget *finestra, *quaderno, *indirizzo;
static WebKitUserContentFilter *filtro;
static GPtrArray *schede;
/* il video dentro la pagina e' spento di default: fa cadere webkit su siti come repubblica.it e sulle
 * macchine vecchie costa troppo. i video andranno aperti in mpv con la decodifica hardware. */
static int misura, scadenza, senza_blocco, senza_media = 1;
static int schede_attive; /* 0 = nessun limite */
static gint64 avvio;

static Scheda *scheda_corrente(void) {
	int n = gtk_notebook_get_current_page(GTK_NOTEBOOK(quaderno));
	return n < 0 ? NULL : g_object_get_data(G_OBJECT(gtk_notebook_get_nth_page(GTK_NOTEBOOK(quaderno), n)), "scheda");
}

static WebKitWebView *vista_corrente(void) {
	Scheda *s = scheda_corrente();
	return s ? s->vista : NULL;
}

/* memoria vera (pss, in kB) di favilla e dei processi di webkit, sommata */
static long memoria_kb(int *processi) {
	long totale = 0;
	*processi = 0;
	GDir *d = g_dir_open("/proc", 0, NULL);
	if (!d) return -1;
	const char *nome;
	while ((nome = g_dir_read_name(d))) {
		if (!g_ascii_isdigit(nome[0])) continue;
		char *percorso = g_strdup_printf("/proc/%s/comm", nome), *comm = NULL;
		g_file_get_contents(percorso, &comm, NULL, NULL);
		g_free(percorso);
		if (comm && (g_str_has_prefix(comm, "favilla") || g_str_has_prefix(comm, "WebKit"))) {
			char *rollup = NULL;
			percorso = g_strdup_printf("/proc/%s/smaps_rollup", nome);
			if (g_file_get_contents(percorso, &rollup, NULL, NULL)) {
				char *pss = strstr(rollup, "\nPss:");
				if (pss) { totale += atol(pss + 5); (*processi)++; }
				g_free(rollup);
			}
			g_free(percorso);
		}
		g_free(comm);
	}
	g_dir_close(d);
	return totale;
}

static void scrivi_risultati(void) {
	int processi;
	long kb = memoria_kb(&processi);
	printf("{\"avvio_s\": %.2f, \"memoria_mb\": %.1f, \"processi\": %d, \"schede\": [\n",
	       (g_get_monotonic_time() - avvio) / 1e6, kb / 1024.0, processi);
	for (guint i = 0; i < schede->len; i++) {
		Scheda *s = g_ptr_array_index(schede, i);
		const char *titolo = s->vista ? webkit_web_view_get_title(s->vista) : s->titolo;
		char *t = g_strescape(titolo ? titolo : "", NULL);
		char *u = g_strescape(s->url ? s->url : "", NULL);
		printf("  {\"url\": \"%s\", \"caricata\": %s, \"secondi\": %.2f, \"titolo\": \"%s\", \"processo\": \"%s\", \"addormentata\": %s}%s\n",
		       u, s->finito ? "true" : "false", s->secondi, t, s->fine_processo ? s->fine_processo : "vivo",
		       s->addormentata ? "true" : "false", i + 1 < schede->len ? "," : "");
		g_free(t);
		g_free(u);
	}
	printf("]}\n");
	fflush(stdout);
}

static gboolean fine_misura(gpointer forzata) {
	static int fatto;
	if (fatto) return G_SOURCE_REMOVE;
	/* le pagine di prova scrivono il risultato nel titolo: finche' calcolano, aspetto */
	for (guint i = 0; !forzata && i < schede->len; i++) {
		Scheda *s = g_ptr_array_index(schede, i);
		const char *t = s->vista ? webkit_web_view_get_title(s->vista) : NULL;
		if (t && g_str_has_suffix(t, "in corso")) return G_SOURCE_CONTINUE;
	}
	fatto = 1;
	scrivi_risultati();
	gtk_main_quit();
	return G_SOURCE_REMOVE;
}

/* in misura: quando tutte le schede hanno finito, aspetto 5 s che i processi si assestino */
static void controlla_fine(void) {
	if (!misura) return;
	for (guint i = 0; i < schede->len; i++)
		if (!((Scheda *)g_ptr_array_index(schede, i))->finito) return;
	g_timeout_add_seconds(5, fine_misura, NULL); /* poi ricontrolla ogni 5 s */
}

static void controlla_sonno(void);

static void caricamento(WebKitWebView *v, WebKitLoadEvent evento, gpointer dati) {
	Scheda *s = dati;
	if (evento == WEBKIT_LOAD_STARTED) s->inizio = g_get_monotonic_time();
	if (evento == WEBKIT_LOAD_FINISHED && !s->finito) {
		s->finito = 1;
		s->secondi = (g_get_monotonic_time() - s->inizio) / 1e6;
		controlla_fine();
	}
	if (v == vista_corrente()) gtk_entry_set_text(GTK_ENTRY(indirizzo), webkit_web_view_get_uri(v) ? webkit_web_view_get_uri(v) : "");
	if (evento == WEBKIT_LOAD_FINISHED) controlla_sonno();
}

/* il processo che disegna la pagina e' morto: segno perche' e conto la scheda come finita */
static void processo_finito(WebKitWebView *v, WebKitWebProcessTerminationReason motivo, gpointer dati) {
	(void)v;
	Scheda *s = dati;
	s->fine_processo = motivo == WEBKIT_WEB_PROCESS_CRASHED ? "crash"
		: motivo == WEBKIT_WEB_PROCESS_EXCEEDED_MEMORY_LIMIT ? "troppa memoria" : "chiuso";
	if (!s->finito) { s->finito = 1; controlla_fine(); }
}

static void titolo_cambiato(WebKitWebView *v, GParamSpec *p, Scheda *s) {
	(void)p;
	const char *t = webkit_web_view_get_title(v);
	g_free(s->titolo);
	s->titolo = g_strdup(t);
	gtk_label_set_text(GTK_LABEL(s->etichetta), t && *t ? t : "Nuova scheda");
}

static void crea_vista(Scheda *s) {
	WebKitWebView *v = WEBKIT_WEB_VIEW(webkit_web_view_new());
	WebKitSettings *imp = webkit_web_view_get_settings(v);
	/* disegno col processore: le schede grafiche vecchie non reggono l'accelerazione */
	webkit_settings_set_hardware_acceleration_policy(imp, WEBKIT_HARDWARE_ACCELERATION_POLICY_NEVER);
	webkit_settings_set_enable_dns_prefetching(imp, FALSE);
	webkit_settings_set_enable_page_cache(imp, FALSE);
	if (senza_media) {
		webkit_settings_set_enable_media(imp, FALSE);
		webkit_settings_set_enable_webaudio(imp, FALSE);
		webkit_settings_set_enable_mediasource(imp, FALSE);
		webkit_settings_set_enable_media_stream(imp, FALSE);
	}
	if (filtro) webkit_user_content_manager_add_filter(webkit_web_view_get_user_content_manager(v), filtro);

	s->vista = v;
	s->addormentata = 0;
	g_signal_connect(v, "load-changed", G_CALLBACK(caricamento), s);
	g_signal_connect(v, "notify::title", G_CALLBACK(titolo_cambiato), s);
	g_signal_connect(v, "web-process-terminated", G_CALLBACK(processo_finito), s);
	GList *figli = gtk_container_get_children(GTK_CONTAINER(s->contenitore));
	for (GList *f = figli; f; f = f->next) gtk_widget_destroy(f->data);
	g_list_free(figli);
	gtk_box_pack_start(GTK_BOX(s->contenitore), GTK_WIDGET(v), TRUE, TRUE, 0);
	gtk_widget_show_all(s->contenitore);
	if (s->url) webkit_web_view_load_uri(v, s->url);
}

/* chiude la vista (e il suo processo, con la memoria) e lascia una scritta al suo posto */
static void addormenta(Scheda *s) {
	const char *uri = webkit_web_view_get_uri(s->vista);
	if (uri) { g_free(s->url); s->url = g_strdup(uri); }
	gtk_widget_destroy(GTK_WIDGET(s->vista));
	s->vista = NULL;
	s->addormentata = 1;
	GtkWidget *scritta = gtk_label_new("Questa scheda dorme per lasciare memoria alle altre. Torna qui e si riapre.");
	gtk_box_pack_start(GTK_BOX(s->contenitore), scritta, TRUE, TRUE, 0);
	gtk_widget_show(scritta);
}

/* oltre il limite: dorme la scheda sveglia usata meno di recente, mai quella in primo piano.
 * gira solo da g_idle_add: chiudere una vista mentre emette un suo segnale vorrebbe dire usarla dopo averla liberata */
static gboolean sonno_in_attesa;

static gboolean esegui_sonno(gpointer dati) {
	(void)dati;
	sonno_in_attesa = FALSE;
	if (schede_attive <= 0) return G_SOURCE_REMOVE;
	Scheda *corrente = scheda_corrente();
	for (;;) {
		guint sveglie = 0;
		Scheda *vecchia = NULL;
		for (guint i = 0; i < schede->len; i++) {
			Scheda *s = g_ptr_array_index(schede, i);
			if (!s->vista) continue;
			sveglie++;
			if (s != corrente && s->finito && (!vecchia || s->uso < vecchia->uso)) vecchia = s;
		}
		if (sveglie <= (guint)schede_attive || !vecchia) return G_SOURCE_REMOVE;
		addormenta(vecchia);
	}
}

static void controlla_sonno(void) {
	if (schede_attive <= 0 || sonno_in_attesa) return;
	sonno_in_attesa = TRUE;
	g_idle_add(esegui_sonno, NULL);
}

static void scheda_scelta(GtkNotebook *q, GtkWidget *pagina, guint n, gpointer dati) {
	(void)q; (void)n; (void)dati;
	Scheda *s = g_object_get_data(G_OBJECT(pagina), "scheda");
	if (!s) return;
	s->uso = g_get_monotonic_time();
	if (!s->vista) crea_vista(s);
	gtk_entry_set_text(GTK_ENTRY(indirizzo), s->url ? s->url : "");
	controlla_sonno();
}

static WebKitWebView *nuova_scheda(const char *url) {
	Scheda *s = g_new0(Scheda, 1);
	s->url = g_strdup(url);
	s->uso = g_get_monotonic_time();
	s->contenitore = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	g_object_set_data(G_OBJECT(s->contenitore), "scheda", s);
	g_ptr_array_add(schede, s);

	s->etichetta = gtk_label_new("Nuova scheda");
	gtk_label_set_max_width_chars(GTK_LABEL(s->etichetta), 24);
	gtk_label_set_ellipsize(GTK_LABEL(s->etichetta), PANGO_ELLIPSIZE_END);
	/* aggiungere la prima scheda la sceglie subito, e scheda_scelta crea gia' la vista: niente doppioni */
	gtk_notebook_append_page(GTK_NOTEBOOK(quaderno), s->contenitore, s->etichetta);
	if (!s->vista) crea_vista(s);
	return s->vista;
}

static void vai(GtkEntry *e, gpointer dati) {
	(void)dati;
	char *testo = g_strstrip(g_strdup(gtk_entry_get_text(e)));
	char *schema = g_uri_parse_scheme(testo);
	int noto = schema && (!g_ascii_strcasecmp(schema, "http") || !g_ascii_strcasecmp(schema, "https")
		|| !g_ascii_strcasecmp(schema, "file") || !g_ascii_strcasecmp(schema, "about"));
	char *url = noto ? g_strdup(testo) : g_strconcat("https://", testo, NULL);
	g_free(schema);
	g_free(testo);
	WebKitWebView *v = vista_corrente();
	if (v) webkit_web_view_load_uri(v, url); else nuova_scheda(url);
	g_free(url);
}

static void apri_schede(char **urls) {
	if (!urls || !*urls) { nuova_scheda(NULL); return; }
	for (char **u = urls; *u; u++) nuova_scheda(*u);
	if (misura) g_timeout_add_seconds(scadenza, fine_misura, GINT_TO_POINTER(1));
}

static void filtro_pronto(GObject *store, GAsyncResult *ris, gpointer urls) {
	GError *err = NULL;
	filtro = webkit_user_content_filter_store_save_finish(WEBKIT_USER_CONTENT_FILTER_STORE(store), ris, &err);
	if (!filtro) {
		fprintf(stderr, "favilla: filtro non caricato: %s\n", err->message);
		g_error_free(err);
		gtk_window_set_title(GTK_WINDOW(finestra), "Favilla (blocco della pubblicita' non attivo)");
	} else {
		fprintf(stderr, "favilla: filtro compilato in %.2f s\n", (g_get_monotonic_time() - avvio) / 1e6);
	}
	apri_schede(urls);
}

static void filtro_dal_pacchetto(GObject *store, GAsyncResult *ris, gpointer urls);

static void filtro_dalla_cache(GObject *store, GAsyncResult *ris, gpointer urls) {
	filtro = webkit_user_content_filter_store_load_finish(WEBKIT_USER_CONTENT_FILTER_STORE(store), ris, NULL);
	if (filtro) {
		fprintf(stderr, "favilla: filtro dalla cache in %.2f s\n", (g_get_monotonic_time() - avvio) / 1e6);
		apri_schede(urls);
		return;
	}
	/* prima volta: compilo le regole json, webkit le salva gia' compilate per i prossimi avvii */
	GFile *f = g_file_new_for_path(FILTRO_JSON);
	GBytes *json = g_file_load_bytes(f, NULL, NULL, NULL);
	g_object_unref(f);
	if (!json) {
		fprintf(stderr, "favilla: manca %s\n", FILTRO_JSON);
		gtk_window_set_title(GTK_WINDOW(finestra), "Favilla (blocco della pubblicita' non attivo)");
		apri_schede(urls);
		return;
	}
	webkit_user_content_filter_store_save(WEBKIT_USER_CONTENT_FILTER_STORE(store), FILTRO_ID, json, NULL, filtro_pronto, urls);
	g_bytes_unref(json);
}

/* il filtro compilato nel pacchetto vale solo con la stessa versione di webkit: se non va, cache dell'utente */
static void filtro_dal_pacchetto(GObject *store, GAsyncResult *ris, gpointer urls) {
	filtro = webkit_user_content_filter_store_load_finish(WEBKIT_USER_CONTENT_FILTER_STORE(store), ris, NULL);
	if (filtro) {
		fprintf(stderr, "favilla: filtro pronto in %.2f s\n", (g_get_monotonic_time() - avvio) / 1e6);
		apri_schede(urls);
		return;
	}
	char *dir = g_build_filename(g_get_user_cache_dir(), "favilla", "filtri", NULL);
	g_mkdir_with_parents(dir, 0700); /* webkit non crea la cartella da solo */
	WebKitUserContentFilterStore *cache = webkit_user_content_filter_store_new(dir);
	g_free(dir);
	webkit_user_content_filter_store_load(cache, FILTRO_ID, NULL, filtro_dalla_cache, urls);
}

int main(int argc, char **argv) {
	avvio = g_get_monotonic_time();
	gtk_init(&argc, &argv);

	int i = 1;
	for (; i < argc; i++) {
		if (!strcmp(argv[i], "--misura") && i + 1 < argc) { misura = 1; scadenza = atoi(argv[++i]); }
		else if (!strcmp(argv[i], "--senza-blocco")) senza_blocco = 1;
		else if (!strcmp(argv[i], "--con-media")) senza_media = 0;
		else break;
	}
	char **urls = argv + i;
	schede = g_ptr_array_new();

	/* quante schede tenere sveglie: lo decide mantice in base alla memoria */
	char *conf = NULL;
	if (g_file_get_contents("/run/mantice/favilla", &conf, NULL, NULL)) {
		char *v = strstr(conf, "schede_attive=");
		if (v) schede_attive = atoi(v + strlen("schede_attive="));
		g_free(conf);
	}
	if (g_getenv("FAVILLA_SCHEDE")) schede_attive = atoi(g_getenv("FAVILLA_SCHEDE"));

	finestra = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	gtk_window_set_title(GTK_WINDOW(finestra), "Favilla");
	gtk_window_set_default_size(GTK_WINDOW(finestra), 1024, 700);
	g_signal_connect(finestra, "destroy", G_CALLBACK(gtk_main_quit), NULL);

	GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	indirizzo = gtk_entry_new();
	gtk_entry_set_placeholder_text(GTK_ENTRY(indirizzo), "Indirizzo o ricerca");
	g_signal_connect(indirizzo, "activate", G_CALLBACK(vai), NULL);
	quaderno = gtk_notebook_new();
	gtk_notebook_set_scrollable(GTK_NOTEBOOK(quaderno), TRUE);
	g_signal_connect(quaderno, "switch-page", G_CALLBACK(scheda_scelta), NULL);
	gtk_box_pack_start(GTK_BOX(scatola), indirizzo, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(scatola), quaderno, TRUE, TRUE, 0);
	gtk_container_add(GTK_CONTAINER(finestra), scatola);
	gtk_widget_show_all(finestra);

	if (senza_blocco) {
		apri_schede(urls);
	} else {
		WebKitUserContentFilterStore *pronto = webkit_user_content_filter_store_new(FILTRO_PRONTO);
		webkit_user_content_filter_store_load(pronto, FILTRO_ID, NULL, filtro_dal_pacchetto, urls);
	}

	gtk_main();
	/* i processi di webkit non devono sopravvivere a favilla: senza questo restano in memoria per un po' */
	for (guint k = 0; k < schede->len; k++) {
		Scheda *s = g_ptr_array_index(schede, k);
		if (s->vista) webkit_web_view_terminate_web_process(s->vista);
	}
	return 0;
}
