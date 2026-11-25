/* prova-filtro: compila il blocco.json con webkit senza aprire finestre, e dice tempo, errori e dimensione.
 * uso: prova-filtro blocco.json cartella-cache */
#include <webkit2/webkit2.h>
#include <stdio.h>

static GMainLoop *giro;
static gint64 inizio;
static int esito = 1;

static void fatto(GObject *store, GAsyncResult *ris, gpointer dati) {
	(void)dati;
	GError *err = NULL;
	WebKitUserContentFilter *f = webkit_user_content_filter_store_save_finish(WEBKIT_USER_CONTENT_FILTER_STORE(store), ris, &err);
	printf("%s in %.1f s%s%s\n", f ? "compilato" : "fallito", (g_get_monotonic_time() - inizio) / 1e6,
	       err ? ": " : "", err ? err->message : "");
	esito = f ? 0 : 1;
	g_main_loop_quit(giro);
}

int main(int argc, char **argv) {
	if (argc < 3) return 2;
	giro = g_main_loop_new(NULL, FALSE);
	GBytes *json = g_file_load_bytes(g_file_new_for_path(argv[1]), NULL, NULL, NULL);
	g_mkdir_with_parents(argv[2], 0700);
	WebKitUserContentFilterStore *store = webkit_user_content_filter_store_new(argv[2]);
	inizio = g_get_monotonic_time();
	webkit_user_content_filter_store_save(store, "resurgo", json, NULL, fatto, NULL);
	g_main_loop_run(giro);
	return esito;
}
