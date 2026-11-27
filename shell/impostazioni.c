/*
 * resurgo-impostazioni: prima versione, solo da leggere.
 * pagine: questo computer (versione e rapporto di mantice), rete, privacy, aggiornamenti, schermo, informazioni.
 * le cose da cambiare arriveranno pagina per pagina, ognuna con il suo controllo dei permessi.
 * la prima e' il wi-fi, nella pagina rete: parla con iwd sul bus di sistema (l'utente e' nel gruppo netdev).
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

/* gli indirizzi delle interfacce collegate: si rifanno a ogni cambio del wi-fi */
static void indirizzi(GtkWidget *g) {
	GList *figli = gtk_container_get_children(GTK_CONTAINER(g));
	for (GList *f = figli; f; f = f->next) gtk_widget_destroy(f->data);
	g_list_free(figli);
	g_object_set_data(G_OBJECT(g), "righe", GINT_TO_POINTER(0));
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
	gtk_widget_show_all(g);
}

/* ---- wi-fi ----
 * iwd tiene le schede (Device, con Station quando e' acceso), le reti viste (Network) e quelle salvate.
 * per collegarsi si chiama Network.Connect; se serve la password iwd la chiede all'agente registrato qui
 * (RequestPassphrase), che apre la finestra. cosi' funziona anche quando una password salvata non va piu'. */
#define IWD "net.connman.iwd"
#define AGENTE "/resurgo/agente"
static GtkWidget *finestra, *griglia_rete, *wifi_stato, *wifi_avviso, *wifi_elenco, *wifi_cerca, *wifi_scollega, *wifi_accendi;
static GDBusConnection *bus;
static char *iwd_proprietario, *stazione, *da_accendere, *interfaccia_da_accendere;
static GtkWidget *finestra_password;
/* la rete cliccata: iwd puo' chiedere la password solo per questa, non per una rete qualsiasi */
static char *rete_scelta;
static guint rinfresco;
/* aperte da "resurgo-impostazioni rete" (il pannello): la tastiera parte dalla prima rete, una volta sola */
static gboolean fuoco_alle_reti;

static void avviso(const char *t) {
	gtk_label_set_text(GTK_LABEL(wifi_avviso), t ? t : "");
	gtk_widget_set_visible(wifi_avviso, t != NULL);
}

static GVariant *chiama(const char *percorso, const char *interfaccia, const char *metodo, GVariant *argomenti, const char *tipo) {
	return g_dbus_connection_call_sync(bus, IWD, percorso, interfaccia, metodo, argomenti,
		tipo ? G_VARIANT_TYPE(tipo) : NULL, G_DBUS_CALL_FLAGS_NONE, 5000, NULL, NULL);
}

/* il nome di una rete lo sceglie chi la trasmette: utf-8 valido, senza a capo, caratteri di controllo
 * o di direzione del testo (che girerebbero la frase della password), al massimo 32 caratteri */
static char *pulito(const char *nome) {
	char *valido = g_utf8_make_valid(nome, -1);
	GString *s = g_string_new(NULL);
	for (const char *c = valido; *c && g_utf8_strlen(s->str, -1) < 32; c = g_utf8_next_char(c)) {
		gunichar u = g_utf8_get_char(c);
		if (g_unichar_iscntrl(u) || g_unichar_type(u) == G_UNICODE_FORMAT) continue;
		g_string_append_unichar(s, u);
	}
	g_free(valido);
	return g_string_free(s, FALSE);
}

static char *proprieta_testo(GVariant *proprieta, const char *nome) {
	const char *v = NULL;
	return proprieta && g_variant_lookup(proprieta, nome, "&s", &v) ? g_strdup(v) : NULL;
}

static gboolean proprieta_vero(GVariant *proprieta, const char *nome) {
	gboolean v = FALSE;
	return proprieta && g_variant_lookup(proprieta, nome, "b", &v) && v;
}

static char *nome_rete(const char *percorso) {
	GVariant *r = chiama(percorso, "org.freedesktop.DBus.Properties", "Get", g_variant_new("(ss)", IWD ".Network", "Name"), "(v)");
	if (!r) return g_strdup("senza nome");
	GVariant *v;
	g_variant_get(r, "(v)", &v);
	char *nome = g_variant_is_of_type(v, G_VARIANT_TYPE_STRING) ? pulito(g_variant_get_string(v, NULL)) : g_strdup("senza nome");
	g_variant_unref(v);
	g_variant_unref(r);
	return nome;
}

/* -6000 e' -60 dBm */
static const char *segnale(gint16 s) {
	return s > -6000 ? "segnale forte" : s > -7500 ? "segnale buono" : "segnale debole";
}

static void wifi_aggiorna(void);

static gboolean rinfresca(gpointer dati) {
	(void)dati;
	rinfresco = 0;
	wifi_aggiorna();
	return G_SOURCE_REMOVE;
}

static void piu_tardi(void) {
	if (!rinfresco) rinfresco = g_timeout_add(300, rinfresca, NULL);
}

static void fatto(GObject *o, GAsyncResult *risultato, gpointer messaggio) {
	GError *errore = NULL;
	GVariant *r = g_dbus_connection_call_finish(G_DBUS_CONNECTION(o), risultato, &errore);
	if (r) g_variant_unref(r);
	if (errore) {
		char *nome = g_dbus_error_get_remote_error(errore);
		/* annullato da chi chiudeva la finestra della password, o una ricerca gia' in corso: niente da dire */
		gboolean muto = nome && (g_str_has_suffix(nome, ".Aborted") || g_str_has_suffix(nome, ".Canceled")
			|| g_str_has_suffix(nome, ".Busy") || g_str_has_suffix(nome, ".InProgress"));
		if (!muto && messaggio) avviso(messaggio);
		g_free(nome);
		g_error_free(errore);
	}
	piu_tardi();
}

static void manda(const char *percorso, const char *interfaccia, const char *metodo, GVariant *argomenti, const char *messaggio) {
	/* connect aspetta anche la password e l'associazione: niente scadenza corta */
	g_dbus_connection_call(bus, IWD, percorso, interfaccia, metodo, argomenti, NULL, G_DBUS_CALL_FLAGS_NONE,
		G_MAXINT, NULL, fatto, (gpointer)messaggio);
}

static void cerca(void) {
	if (stazione) manda(stazione, IWD ".Station", "Scan", NULL, NULL);
}

static void scollega(void) {
	avviso(NULL);
	if (stazione) manda(stazione, IWD ".Station", "Disconnect", NULL, NULL);
}

static void accendi(void) {
	avviso(NULL);
	if (da_accendere) manda(da_accendere, "org.freedesktop.DBus.Properties", "Set",
		g_variant_new("(ssv)", interfaccia_da_accendere, "Powered", g_variant_new_boolean(TRUE)),
		"Non si accende: forse il Wi-Fi è spento con l'interruttore del computer o con un tasto Fn.");
}

static void scelta(GtkListBox *l, GtkListBoxRow *r, gpointer dati) {
	(void)l; (void)dati;
	const char *percorso = g_object_get_data(G_OBJECT(r), "rete"), *tipo = g_object_get_data(G_OBJECT(r), "tipo");
	if (!percorso || g_object_get_data(G_OBJECT(r), "collegata")) return;
	if (tipo && !strcmp(tipo, "8021x")) {
		avviso("Le reti aziendali (802.1X) per ora si configurano solo dal terminale, con iwctl.");
		return;
	}
	avviso(NULL);
	g_free(rete_scelta);
	rete_scelta = g_strdup(percorso);
	gtk_label_set_text(GTK_LABEL(wifi_stato), "Mi sto collegando");
	manda(percorso, IWD ".Network", "Connect", NULL,
		"Il collegamento non è riuscito. Se la rete ha la password, controlla di averla scritta giusta.");
}

static void wifi_aggiorna(void) {
	indirizzi(griglia_rete);
	/* l'elenco si rifa' a ogni segnale di iwd: la rete che aveva la tastiera la riprende dopo */
	GtkWidget *a_fuoco = gtk_container_get_focus_child(GTK_CONTAINER(wifi_elenco));
	char *rete_a_fuoco = a_fuoco && gtk_widget_is_focus(a_fuoco) ? g_strdup(g_object_get_data(G_OBJECT(a_fuoco), "rete")) : NULL;
	GList *figli = gtk_container_get_children(GTK_CONTAINER(wifi_elenco));
	for (GList *f = figli; f; f = f->next) gtk_widget_destroy(f->data);
	g_list_free(figli);
	g_clear_pointer(&stazione, g_free);
	g_clear_pointer(&da_accendere, g_free);
	g_clear_pointer(&interfaccia_da_accendere, g_free);
	/* i pulsanti cambiano solo se serve: nascondere quello con la tastiera la perderebbe */
	gboolean mostra_cerca = FALSE, mostra_scollega = FALSE;

	GVariant *oggetti = iwd_proprietario ? chiama("/", "org.freedesktop.DBus.ObjectManager", "GetManagedObjects", NULL, "(a{oa{sa{sv}}})") : NULL;
	if (!oggetti) {
		gtk_label_set_text(GTK_LABEL(wifi_stato), "Il servizio del Wi-Fi (iwd) non risponde.");
		g_free(rete_a_fuoco);
		for (GtkWidget **w = (GtkWidget *[]){wifi_cerca, wifi_scollega, wifi_accendi, wifi_elenco, NULL}; *w; w++)
			gtk_widget_set_visible(*w, FALSE);
		return;
	}
	/* la prima scheda in modo stazione (le altre possono fare da punto di accesso) */
	GHashTable *reti = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, (GDestroyNotify)g_variant_unref);
	GVariant *prop_stazione = NULL;
	char *adattatore_spento = NULL, *dispositivo_spento = NULL;
	gboolean schede = FALSE;
	GVariantIter *it;
	const char *percorso;
	GVariant *interfacce;
	g_variant_get(oggetti, "(a{oa{sa{sv}}})", &it);
	while (g_variant_iter_next(it, "{&o@a{sa{sv}}}", &percorso, &interfacce)) {
		GVariant *v;
		if ((v = g_variant_lookup_value(interfacce, IWD ".Station", NULL))) {
			if (!stazione) { stazione = g_strdup(percorso); prop_stazione = g_variant_ref(v); }
			g_variant_unref(v);
		}
		if ((v = g_variant_lookup_value(interfacce, IWD ".Device", NULL))) {
			char *modo = proprieta_testo(v, "Mode");
			if (!modo || !strcmp(modo, "station")) {
				schede = TRUE;
				if (!proprieta_vero(v, "Powered") && !dispositivo_spento) dispositivo_spento = g_strdup(percorso);
			}
			g_free(modo);
			g_variant_unref(v);
		}
		if ((v = g_variant_lookup_value(interfacce, IWD ".Adapter", NULL))) {
			if (!proprieta_vero(v, "Powered") && !adattatore_spento) adattatore_spento = g_strdup(percorso);
			g_variant_unref(v);
		}
		if ((v = g_variant_lookup_value(interfacce, IWD ".Network", NULL))) g_hash_table_insert(reti, g_strdup(percorso), v);
		g_variant_unref(interfacce);
	}
	g_variant_iter_free(it);
	g_variant_unref(oggetti);

	if (!stazione) {
		/* spenta: si riaccende la scheda, o prima l'adattatore se e' spento lui */
		if (adattatore_spento) { da_accendere = g_strdup(adattatore_spento); interfaccia_da_accendere = g_strdup(IWD ".Adapter"); }
		else if (dispositivo_spento) { da_accendere = g_strdup(dispositivo_spento); interfaccia_da_accendere = g_strdup(IWD ".Device"); }
		gtk_label_set_text(GTK_LABEL(wifi_stato), da_accendere ? "Il Wi-Fi è spento." :
			schede ? "La scheda Wi-Fi non è pronta." : "Questo computer non ha una scheda Wi-Fi che Resurgo riconosce.");
		goto fine;
	}

	char *stato = proprieta_testo(prop_stazione, "State");
	const char *collegata = NULL;
	g_variant_lookup(prop_stazione, "ConnectedNetwork", "&o", &collegata);
	GVariant *rc = collegata ? g_hash_table_lookup(reti, collegata) : NULL;
	char *grezzo = rc ? proprieta_testo(rc, "Name") : NULL, *nome = grezzo ? pulito(grezzo) : NULL, *t;
	g_free(grezzo);
	if (stato && !strcmp(stato, "connected")) t = g_strdup_printf("Collegato a %s.", nome ? nome : "una rete senza nome");
	else if (stato && (!strcmp(stato, "connecting") || !strcmp(stato, "roaming"))) t = g_strdup_printf("Mi sto collegando a %s.", nome ? nome : "una rete");
	else t = g_strdup("Non collegato.");
	if (proprieta_vero(prop_stazione, "Scanning")) { char *u = g_strconcat(t, " Sto cercando le reti.", NULL); g_free(t); t = u; }
	gtk_label_set_text(GTK_LABEL(wifi_stato), t);
	mostra_scollega = stato && strcmp(stato, "disconnected");
	mostra_cerca = TRUE;
	g_free(t); g_free(nome); g_free(stato);

	/* le reti in ordine di iwd (prima le migliori), col segnale in centesimi di dBm */
	GVariant *ordinate = chiama(stazione, IWD ".Station", "GetOrderedNetworks", NULL, "(a(on))");
	if (ordinate) {
		GVariantIter *ir;
		gint16 forza;
		g_variant_get(ordinate, "(a(on))", &ir);
		while (g_variant_iter_next(ir, "(&on)", &percorso, &forza)) {
			GVariant *p = g_hash_table_lookup(reti, percorso);
			if (!p) continue;
			char *grezzo = proprieta_testo(p, "Name"), *n = grezzo ? pulito(grezzo) : NULL, *tipo = proprieta_testo(p, "Type");
			const char *salvata = NULL;
			gboolean questa = proprieta_vero(p, "Connected");
			g_variant_lookup(p, "KnownNetwork", "&o", &salvata);
			const char *come = questa ? "collegata" : salvata ? "salvata" : !tipo ? "" : !strcmp(tipo, "open") ? "aperta, senza password"
				: !strcmp(tipo, "8021x") ? "aziendale" : "con password";
			char *nota = g_strdup_printf("%s%s%s", come, *come ? ", " : "", segnale(forza));
			GtkWidget *scatola = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
			gtk_container_set_border_width(GTK_CONTAINER(scatola), 6);
			GtkWidget *ln = gtk_label_new(n ? n : "senza nome"), *lc = gtk_label_new(nota);
			gtk_label_set_xalign(GTK_LABEL(ln), 0);
			gtk_label_set_ellipsize(GTK_LABEL(ln), PANGO_ELLIPSIZE_END);
			gtk_style_context_add_class(gtk_widget_get_style_context(lc), "nota");
			gtk_box_pack_start(GTK_BOX(scatola), ln, TRUE, TRUE, 0);
			gtk_box_pack_end(GTK_BOX(scatola), lc, FALSE, FALSE, 0);
			GtkWidget *r = gtk_list_box_row_new();
			gtk_container_add(GTK_CONTAINER(r), scatola);
			g_object_set_data_full(G_OBJECT(r), "rete", g_strdup(percorso), g_free);
			g_object_set_data_full(G_OBJECT(r), "tipo", tipo, g_free);
			if (questa) g_object_set_data(G_OBJECT(r), "collegata", GINT_TO_POINTER(1));
			gtk_list_box_insert(GTK_LIST_BOX(wifi_elenco), r, -1);
			gtk_widget_show_all(r);
			gtk_widget_show(wifi_elenco);
			if (rete_a_fuoco && !strcmp(rete_a_fuoco, percorso)) gtk_widget_grab_focus(r);
			g_free(n); g_free(grezzo); g_free(nota);
		}
		g_variant_iter_free(ir);
		g_variant_unref(ordinate);
		GtkListBoxRow *prima = gtk_list_box_get_row_at_index(GTK_LIST_BOX(wifi_elenco), 0);
		/* solo se si sta ancora guardando la pagina della rete */
		if (fuoco_alle_reti && prima && gtk_widget_get_mapped(wifi_stato)) { gtk_widget_grab_focus(GTK_WIDGET(prima)); fuoco_alle_reti = FALSE; }
	}
fine:
	gtk_widget_set_visible(wifi_cerca, mostra_cerca);
	gtk_widget_set_visible(wifi_scollega, mostra_scollega);
	gtk_widget_set_visible(wifi_accendi, da_accendere != NULL);
	gtk_widget_set_visible(wifi_elenco, gtk_list_box_get_row_at_index(GTK_LIST_BOX(wifi_elenco), 0) != NULL);
	g_free(rete_a_fuoco);
	if (prop_stazione) g_variant_unref(prop_stazione);
	g_free(adattatore_spento);
	g_free(dispositivo_spento);
	g_hash_table_unref(reti);
}

/* la finestra della password. NULL se annullata (anche da iwd, con Cancel) */
static char *chiedi_password(const char *nome) {
	GtkWidget *d = gtk_dialog_new_with_buttons("Password del Wi-Fi", GTK_WINDOW(finestra),
		GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT, "Annulla", GTK_RESPONSE_CANCEL, "Collegati", GTK_RESPONSE_OK, NULL);
	gtk_dialog_set_default_response(GTK_DIALOG(d), GTK_RESPONSE_OK);
	gtk_window_set_default_size(GTK_WINDOW(d), 380, -1);
	GtkWidget *area = gtk_dialog_get_content_area(GTK_DIALOG(d));
	gtk_container_set_border_width(GTK_CONTAINER(area), 16);
	gtk_box_set_spacing(GTK_BOX(area), 10);
	GtkWidget *l = gtk_label_new("Questa rete chiede la password:"), *n = gtk_label_new(nome), *e = gtk_entry_new(), *mostra = gtk_check_button_new_with_label("Mostra la password");
	GtkWidget *corta = gtk_label_new("La password del Wi-Fi è lunga da 8 a 63 caratteri, senza lettere accentate.");
	gtk_label_set_xalign(GTK_LABEL(l), 0);
	gtk_label_set_xalign(GTK_LABEL(n), 0);
	gtk_label_set_ellipsize(GTK_LABEL(n), PANGO_ELLIPSIZE_END);
	gtk_style_context_add_class(gtk_widget_get_style_context(n), "sottotitolo");
	gtk_label_set_xalign(GTK_LABEL(corta), 0);
	gtk_entry_set_visibility(GTK_ENTRY(e), FALSE);
	gtk_entry_set_input_purpose(GTK_ENTRY(e), GTK_INPUT_PURPOSE_PASSWORD);
	gtk_entry_set_activates_default(GTK_ENTRY(e), TRUE);
	g_object_bind_property(mostra, "active", e, "visibility", G_BINDING_DEFAULT);
	gtk_box_pack_start(GTK_BOX(area), l, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(area), n, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(area), e, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(area), mostra, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(area), corta, FALSE, FALSE, 0);
	gtk_widget_show_all(area);
	gtk_widget_hide(corta);
	gtk_widget_grab_focus(e);
	finestra_password = d;
	char *password = NULL;
	while (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK) {
		const char *p = gtk_entry_get_text(GTK_ENTRY(e));
		size_t n = strlen(p);
		/* come la vuole iwd: da 8 a 63 caratteri ascii stampabili */
		gboolean buona = n >= 8 && n <= 63;
		for (size_t k = 0; buona && k < n; k++) buona = p[k] >= 0x20 && p[k] <= 0x7e;
		if (buona) { password = g_strdup(p); break; }
		gtk_widget_show(corta);
	}
	finestra_password = NULL;
	gtk_widget_destroy(d);
	return password;
}

static void agente(GDBusConnection *c, const char *mittente, const char *percorso, const char *interfaccia,
		const char *metodo, GVariant *parametri, GDBusMethodInvocation *chiamata, gpointer dati) {
	(void)c; (void)percorso; (void)interfaccia; (void)dati;
	/* risponde solo a iwd */
	if (!iwd_proprietario || g_strcmp0(mittente, iwd_proprietario)) {
		g_dbus_method_invocation_return_dbus_error(chiamata, "net.connman.iwd.Agent.Error.Canceled", "solo iwd");
		return;
	}
	if (!strcmp(metodo, "RequestPassphrase")) {
		const char *rete;
		g_variant_get(parametri, "(&o)", &rete);
		/* 3. una finestra alla volta, e solo per la rete cliccata */
		if (finestra_password || g_strcmp0(rete, rete_scelta)) {
			g_dbus_method_invocation_return_dbus_error(chiamata, "net.connman.iwd.Agent.Error.Canceled", "non richiesta");
			return;
		}
		g_clear_pointer(&rete_scelta, g_free);
		char *nome = nome_rete(rete), *password = chiedi_password(nome);
		g_free(nome);
		if (password) {
			g_dbus_method_invocation_return_value(chiamata, g_variant_new("(s)", password));
			memset(password, 0, strlen(password));
			g_free(password);
		} else {
			g_dbus_method_invocation_return_dbus_error(chiamata, "net.connman.iwd.Agent.Error.Canceled", "annullata");
		}
		return;
	}
	if (!strcmp(metodo, "Cancel") && finestra_password) gtk_dialog_response(GTK_DIALOG(finestra_password), GTK_RESPONSE_CANCEL);
	g_dbus_method_invocation_return_value(chiamata, NULL);
}

static void segnale_iwd(GDBusConnection *c, const char *mittente, const char *percorso, const char *interfaccia,
		const char *nome, GVariant *parametri, gpointer dati) {
	(void)c; (void)mittente; (void)percorso; (void)interfaccia; (void)nome; (void)parametri; (void)dati;
	piu_tardi();
}

static void iwd_comparso(GDBusConnection *c, const char *nome, const char *proprietario, gpointer dati) {
	(void)c; (void)nome; (void)dati;
	g_free(iwd_proprietario);
	iwd_proprietario = g_strdup(proprietario);
	/* se c'e' gia' un altro agente (iwctl aperto) va bene lo stesso */
	GVariant *r = chiama("/net/connman/iwd", IWD ".AgentManager", "RegisterAgent", g_variant_new("(o)", AGENTE), NULL);
	if (r) g_variant_unref(r);
	wifi_aggiorna();
	cerca();
}

static void iwd_sparito(GDBusConnection *c, const char *nome, gpointer dati) {
	(void)c; (void)nome; (void)dati;
	g_clear_pointer(&iwd_proprietario, g_free);
	if (finestra_password) gtk_dialog_response(GTK_DIALOG(finestra_password), GTK_RESPONSE_CANCEL);
	wifi_aggiorna();
}

static GtkWidget *rete(void) {
	GtkWidget *p = nuova_pagina("Rete", &griglia_rete);
	GtkWidget *scatola = g_object_get_data(G_OBJECT(p), "scatola");
	indirizzi(griglia_rete);

	GtkWidget *titolo = gtk_label_new("Wi-Fi");
	gtk_style_context_add_class(gtk_widget_get_style_context(titolo), "sottotitolo");
	gtk_label_set_xalign(GTK_LABEL(titolo), 0);
	gtk_widget_set_margin_top(titolo, 12);
	wifi_stato = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(wifi_stato), 0);
	gtk_label_set_line_wrap(GTK_LABEL(wifi_stato), TRUE);
	wifi_avviso = gtk_label_new("");
	gtk_label_set_xalign(GTK_LABEL(wifi_avviso), 0);
	gtk_label_set_line_wrap(GTK_LABEL(wifi_avviso), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(wifi_avviso), 70);
	gtk_widget_set_no_show_all(wifi_avviso, TRUE);
	wifi_cerca = gtk_button_new_with_label("Cerca le reti");
	wifi_scollega = gtk_button_new_with_label("Scollega");
	wifi_accendi = gtk_button_new_with_label("Accendi il Wi-Fi");
	g_signal_connect(wifi_cerca, "clicked", G_CALLBACK(cerca), NULL);
	g_signal_connect(wifi_scollega, "clicked", G_CALLBACK(scollega), NULL);
	g_signal_connect(wifi_accendi, "clicked", G_CALLBACK(accendi), NULL);
	GtkWidget *pulsanti = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
	gtk_box_pack_start(GTK_BOX(pulsanti), wifi_cerca, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(pulsanti), wifi_scollega, FALSE, FALSE, 0);
	gtk_box_pack_start(GTK_BOX(pulsanti), wifi_accendi, FALSE, FALSE, 0);
	wifi_elenco = gtk_list_box_new();
	gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(wifi_elenco), TRUE);
	g_signal_connect(wifi_elenco, "row-activated", G_CALLBACK(scelta), NULL);
	for (GtkWidget **w = (GtkWidget *[]){titolo, wifi_stato, wifi_avviso, pulsanti, wifi_elenco, NULL}; *w; w++)
		gtk_box_pack_start(GTK_BOX(scatola), *w, FALSE, FALSE, 0);
	/* i pulsanti e l'elenco li mostra wifi_aggiorna quando servono */
	for (GtkWidget **w = (GtkWidget *[]){wifi_cerca, wifi_scollega, wifi_accendi, wifi_elenco, NULL}; *w; w++)
		gtk_widget_set_no_show_all(*w, TRUE);

	bus = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, NULL);
	if (!bus) {
		gtk_label_set_text(GTK_LABEL(wifi_stato), "Il servizio del Wi-Fi (iwd) non risponde.");
		return p;
	}
	static const char xml[] = "<node><interface name='net.connman.iwd.Agent'>"
		"<method name='Release'/>"
		"<method name='RequestPassphrase'><arg type='o' direction='in'/><arg type='s' direction='out'/></method>"
		"<method name='Cancel'><arg type='s' direction='in'/></method>"
		"</interface></node>";
	static const GDBusInterfaceVTable vt = {agente, NULL, NULL, {0}};
	GDBusNodeInfo *info = g_dbus_node_info_new_for_xml(xml, NULL);
	g_dbus_connection_register_object(bus, AGENTE, info->interfaces[0], &vt, NULL, NULL, NULL);
	g_dbus_node_info_unref(info);
	g_dbus_connection_signal_subscribe(bus, IWD, NULL, NULL, NULL, NULL, G_DBUS_SIGNAL_FLAGS_NONE, segnale_iwd, NULL, NULL);
	g_bus_watch_name_on_connection(bus, IWD, G_BUS_NAME_WATCHER_FLAGS_NONE, iwd_comparso, iwd_sparito, NULL, NULL);
	gtk_label_set_text(GTK_LABEL(wifi_stato), "Cerco il servizio del Wi-Fi.");
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
	finestra = gtk_window_new(GTK_WINDOW_TOPLEVEL);
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
	fuoco_alle_reti = argc > 1 && !strcmp(argv[1], "rete");
	gtk_main();
	return 0;
}
