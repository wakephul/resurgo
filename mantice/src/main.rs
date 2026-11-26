//! mantice: legge l'hardware all'avvio e adatta resurgo al computer su cui gira.
//!
//! gira come root dal servizio openrc, prima che parta il desktop.
//!   mantice applica   legge l'hardware, applica le regolazioni, scrive il rapporto
//!   mantice mostra    stampa l'ultimo rapporto
//!   mantice sorveglia ventole e frequenza secondo la temperatura, ogni 5 secondi (vedi sorveglia.rs)
//!   mantice ripristina rimette ventole e frequenza come le lascia il bios
//!
//! cosa regola si puo' spegnere riga per riga in /var/lib/mantice/disattivati (memoria, processore, disco,
//! grafica, batteria, ventole). il file e' di root: il benvenuto della shell passa da
//! "doas mantice disattiva AREA" / "riattiva AREA", con una regola doas per ogni comando esatto.
//! in /var/lib/mantice/fatto ogni regolazione ha la sua area (area<TAB>testo), per il benvenuto.
//! solo libreria standard: un eseguibile piccolo.

mod sorveglia;

use std::fs;
use std::path::Path;
use std::process::Command;

const RUN: &str = "/run/mantice";
const RAPPORTO: &str = "/var/lib/mantice/rapporto";
const DISATTIVATI: &str = "/var/lib/mantice/disattivati";
const FATTO: &str = "/var/lib/mantice/fatto";

struct Hardware {
    ram_mb: u64,
    processore: String,
    nuclei: usize,
    aes: bool,
    dischi: Vec<(String, bool)>, // nome, meccanico
    grafica: Vec<String>,        // driver del kernel
    cpufreq: bool,
    temperature: Vec<(String, f64)>,
    ventole: Vec<String>,
    batterie: Vec<String>,
}

/// regolazioni per fascia di memoria
struct Profilo {
    zram_percento: u64,
    swappiness: u32,
    schede_attive: u32, // 0 = nessun limite
}

/// per le prove: con MANTICE_RADICE=/tmp/finto, /sys e /proc si leggono e scrivono li' dentro
fn rad(percorso: impl AsRef<Path>) -> String {
    let p = percorso.as_ref().to_string_lossy().into_owned();
    match std::env::var("MANTICE_RADICE") {
        Ok(r) if !r.is_empty() && (p.starts_with("/sys") || p.starts_with("/proc")) => format!("{}{}", r, p),
        _ => p,
    }
}

fn leggi(percorso: impl AsRef<Path>) -> Option<String> {
    fs::read_to_string(rad(percorso)).ok().map(|s| s.trim().to_string())
}

fn scrivi(percorso: impl AsRef<Path>, valore: &str) -> Result<(), String> {
    let p = rad(percorso);
    fs::write(&p, valore).map_err(|e| format!("{}: {}", p, e))
}

fn esiste(percorso: impl AsRef<Path>) -> bool {
    Path::new(&rad(percorso)).exists()
}

fn voci(cartella: &str) -> Vec<String> {
    let mut v: Vec<String> = fs::read_dir(rad(cartella))
        .map(|d| d.filter_map(|e| e.ok()).map(|e| e.file_name().to_string_lossy().into_owned()).collect())
        .unwrap_or_default();
    v.sort();
    v
}

fn comando(programma: &str, argomenti: &[&str]) -> Result<(), String> {
    match Command::new(programma).args(argomenti).output() {
        Ok(o) if o.status.success() => Ok(()),
        Ok(o) => Err(format!("{} {}: {}", programma, argomenti.join(" "), String::from_utf8_lossy(&o.stderr).trim())),
        Err(e) => Err(format!("{}: {}", programma, e)),
    }
}

fn rileva() -> Hardware {
    let meminfo = leggi("/proc/meminfo").unwrap_or_default();
    let ram_kb: u64 = meminfo
        .lines()
        .find(|l| l.starts_with("MemTotal:"))
        .and_then(|l| l.split_whitespace().nth(1))
        .and_then(|n| n.parse().ok())
        .unwrap_or(0);

    let cpuinfo = leggi("/proc/cpuinfo").unwrap_or_default();
    let campo = |nome: &str| {
        cpuinfo
            .lines()
            .find(|l| l.starts_with(nome))
            .and_then(|l| l.split_once(':'))
            .map(|(_, v)| v.trim().to_string())
    };
    let processore = campo("model name").or_else(|| campo("Hardware")).or_else(|| campo("CPU part")).unwrap_or_else(|| "sconosciuto".into());
    let nuclei = cpuinfo.lines().filter(|l| l.starts_with("processor")).count().max(1);
    let flag = campo("flags").or_else(|| campo("Features")).unwrap_or_default();
    let aes = flag.split_whitespace().any(|f| f == "aes");

    let dischi = voci("/sys/block")
        .into_iter()
        .filter(|n| !["loop", "ram", "zram", "dm-", "sr", "fd"].iter().any(|p| n.starts_with(p)))
        .map(|n| {
            let meccanico = leggi(format!("/sys/block/{}/queue/rotational", n)).as_deref() == Some("1");
            (n, meccanico)
        })
        .collect();

    let grafica = voci("/sys/class/drm")
        .into_iter()
        .filter(|n| n.starts_with("card") && !n.contains('-'))
        .filter_map(|n| fs::read_link(rad(format!("/sys/class/drm/{}/device/driver", n))).ok())
        .filter_map(|p| p.file_name().map(|f| f.to_string_lossy().into_owned()))
        .collect();

    let temperature = voci("/sys/class/thermal")
        .into_iter()
        .filter(|n| n.starts_with("thermal_zone"))
        .filter_map(|n| {
            let t: f64 = leggi(format!("/sys/class/thermal/{}/temp", n))?.parse().ok()?;
            Some((leggi(format!("/sys/class/thermal/{}/type", n)).unwrap_or(n), t / 1000.0))
        })
        .collect();

    let ventole = voci("/sys/class/hwmon")
        .into_iter()
        .filter(|n| esiste(format!("/sys/class/hwmon/{}/pwm1", n)))
        .map(|n| leggi(format!("/sys/class/hwmon/{}/name", n)).unwrap_or(n))
        .chain(esiste("/proc/acpi/ibm/fan").then(|| "thinkpad".to_string()))
        .collect();

    let batterie = voci("/sys/class/power_supply")
        .into_iter()
        .filter(|n| leggi(format!("/sys/class/power_supply/{}/type", n)).as_deref() == Some("Battery"))
        .collect();

    Hardware {
        ram_mb: ram_kb / 1024,
        processore,
        nuclei,
        aes,
        dischi,
        grafica,
        cpufreq: esiste("/sys/devices/system/cpu/cpu0/cpufreq"),
        temperature,
        ventole,
        batterie,
    }
}

fn profilo(ram_mb: u64) -> Profilo {
    // le soglie stanno un po' sopra 1 e 2 GB: il kernel si tiene una parte della memoria
    match ram_mb {
        0..=1200 => Profilo { zram_percento: 100, swappiness: 150, schede_attive: 2 },
        1201..=2500 => Profilo { zram_percento: 50, swappiness: 120, schede_attive: 6 },
        _ => Profilo { zram_percento: 25, swappiness: 100, schede_attive: 0 },
    }
}

/// memoria compressa in ram (zram) come swap. earlyoom e' sempre acceso dal suo servizio: se lo accendesse mantice,
/// openrc lo spegnerebbe passando dalla fase di avvio a quella normale
fn memoria(hw: &Hardware, p: &Profilo, fatto: &mut Vec<String>) -> Result<(), String> {
    if !esiste("/sys/block/zram0") {
        comando("modprobe", &["zram", "num_devices=1"])?;
    }
    let attiva = leggi("/proc/swaps").unwrap_or_default().contains("zram0");
    if !attiva {
        // lz4 e' il piu' leggero per i processori vecchi; se manca resta quello del kernel
        let algoritmo = if scrivi("/sys/block/zram0/comp_algorithm", "lz4").is_ok() { "lz4" } else { "lzo-rle" };
        let byte = hw.ram_mb * 1024 * 1024 * p.zram_percento / 100;
        scrivi("/sys/block/zram0/disksize", &byte.to_string())?;
        comando("mkswap", &["/dev/zram0"])?;
        comando("swapon", &["-p", "100", "/dev/zram0"])?;
        fatto.push(format!("memoria compressa (zram, {}) da {} MB", algoritmo, byte / 1024 / 1024));
    }
    scrivi("/proc/sys/vm/swappiness", &p.swappiness.to_string())?;
    // con zram leggere una pagina alla volta e' piu' veloce che leggerne otto
    scrivi("/proc/sys/vm/page-cluster", "0")?;
    fatto.push(format!("swappiness {}", p.swappiness));
    Ok(())
}

/// regolazione della frequenza orientata al risparmio: scalda meno, ventole piu' piano
fn processore(hw: &Hardware, fatto: &mut Vec<String>) -> Result<(), String> {
    if !hw.cpufreq {
        fatto.push("frequenza del processore non regolabile, lasciata com'è".into());
        return Ok(());
    }
    let disponibili = leggi("/sys/devices/system/cpu/cpu0/cpufreq/scaling_available_governors").unwrap_or_default();
    let scelta = ["schedutil", "ondemand", "powersave"]
        .into_iter()
        .find(|g| disponibili.split_whitespace().any(|d| d == *g))
        .ok_or_else(|| format!("nessuna regolazione adatta tra: {}", disponibili))?;
    for cpu in voci("/sys/devices/system/cpu").iter().filter(|n| n.starts_with("cpu") && n[3..].chars().all(|c| c.is_ascii_digit())) {
        scrivi(format!("/sys/devices/system/cpu/{}/cpufreq/scaling_governor", cpu), scelta)?;
    }
    fatto.push(format!("frequenza del processore: {}", scelta));
    Ok(())
}

/// dischi meccanici: bfq, che tiene reattivo il sistema mentre il disco lavora
fn disco(hw: &Hardware, fatto: &mut Vec<String>) -> Result<(), String> {
    for (nome, meccanico) in &hw.dischi {
        if *meccanico {
            scrivi(format!("/sys/block/{}/queue/scheduler", nome), "bfq")?;
            fatto.push(format!("{}: disco meccanico, accesso con bfq", nome));
        }
    }
    Ok(())
}

/// per ora il desktop si disegna sempre col processore: e' l'unica strada che funziona su tutte le schede.
/// ponytail: l'accelerazione con la gpu andra' accesa solo sui driver provati (crocus, iris, r600).
fn grafica(fatto: &mut Vec<String>) -> Result<(), String> {
    scrivi(format!("{}/ambiente", RUN), "WLR_RENDERER=pixman\n")?;
    fatto.push("desktop disegnato col processore".into());
    Ok(())
}

/// batteria: carica fermata all'80% dove il kernel lo permette (thinkpad e molti altri), per non rovinarla
fn batteria(hw: &Hardware, fatto: &mut Vec<String>) -> Result<(), String> {
    if hw.batterie.is_empty() {
        fatto.push("nessuna batteria".into());
        return Ok(());
    }
    for b in &hw.batterie {
        let fine = format!("/sys/class/power_supply/{}/charge_control_end_threshold", b);
        let inizio = format!("/sys/class/power_supply/{}/charge_control_start_threshold", b);
        if esiste(&fine) {
            // prima la fine: l'inizio dev'essere sempre sotto la fine
            scrivi(&fine, "80")?;
            if esiste(&inizio) {
                scrivi(&inizio, "75")?;
            }
            fatto.push(format!("{}: carica fermata all'80% (riparte sotto il 75%)", b));
        } else {
            fatto.push(format!("{}: questa batteria non permette di fermare la carica", b));
        }
    }
    Ok(())
}

/// le ventole le comanda il servizio mantice-sorveglia: qui si dice solo quali
fn ventole(fatto: &mut Vec<String>) -> Result<(), String> {
    let v = sorveglia::trova_ventole();
    if v.is_empty() {
        fatto.push("nessuna ventola comandabile: le regola il bios".into());
    } else {
        fatto.push(format!("{} ventole comandate in base alla temperatura, al bios sopra gli 80 gradi", v.len()));
    }
    Ok(())
}

const AREE: [&str; 6] = ["memoria", "processore", "disco", "grafica", "batteria", "ventole"];

/// il file delle regolazioni spente esiste sempre, di root e leggibile da tutti
fn prepara_disattivati() {
    if !Path::new(DISATTIVATI).exists() {
        let _ = fs::write(DISATTIVATI, "");
    }
}

/// mantice disattiva|riattiva AREA: solo aree conosciute, il file lo scrive root
fn cambia_area(spegni: bool, area: Option<&str>) -> i32 {
    let Some(area) = area.filter(|a| AREE.contains(a)) else {
        eprintln!("area sconosciuta: le aree sono {}", AREE.join(", "));
        return 2;
    };
    let _ = fs::create_dir_all(Path::new(DISATTIVATI).parent().unwrap());
    let mut spente: Vec<String> = leggi(DISATTIVATI)
        .unwrap_or_default()
        .lines()
        .map(|l| l.trim().to_string())
        .filter(|l| AREE.contains(&l.as_str()) && l != area)
        .collect();
    if spegni {
        spente.push(area.to_string());
    }
    spente.sort();
    let testo: String = spente.iter().map(|a| format!("{}\n", a)).collect();
    match fs::write(DISATTIVATI, testo) {
        Ok(()) => 0,
        Err(e) => {
            eprintln!("{}: {}", DISATTIVATI, e);
            1
        }
    }
}

fn applica() -> i32 {
    let hw = rileva();
    let p = profilo(hw.ram_mb);
    let disattivati = leggi(DISATTIVATI).unwrap_or_default();
    let spento = |voce: &str| disattivati.lines().any(|l| l.trim() == voce);
    let _ = fs::create_dir_all(RUN);
    let _ = fs::create_dir_all(Path::new(RAPPORTO).parent().unwrap());

    prepara_disattivati();
    let mut fatto = Vec::new();
    let mut per_area: Vec<(String, String)> = Vec::new();
    let mut errori = Vec::new();
    let passi: [(&str, Box<dyn Fn(&mut Vec<String>) -> Result<(), String>>); 6] = [
        ("memoria", Box::new(|f| memoria(&hw, &p, f))),
        ("processore", Box::new(|f| processore(&hw, f))),
        ("disco", Box::new(|f| disco(&hw, f))),
        ("grafica", Box::new(grafica)),
        ("batteria", Box::new(|f| batteria(&hw, f))),
        ("ventole", Box::new(ventole)),
    ];
    for (nome, passo) in passi.iter() {
        let inizio = fatto.len();
        if spento(nome) {
            fatto.push(format!("{}: spento a mano", nome));
        } else if let Err(e) = passo(&mut fatto) {
            errori.push(format!("{}: {}", nome, e));
        }
        for f in &fatto[inizio..] {
            per_area.push((nome.to_string(), f.clone()));
        }
    }
    let elenco: String = per_area.iter().map(|(a, t)| format!("{}\t{}\n", a, t)).collect();
    let _ = fs::write(FATTO, elenco);
    // favilla legge quante schede tenere sveglie
    if let Err(e) = scrivi(format!("{}/favilla", RUN), &format!("schede_attive={}\n", p.schede_attive)) {
        errori.push(e);
    }

    let mut r = String::new();
    r.push_str("Ho trovato\n");
    r.push_str(&format!("  processore: {} ({} nuclei{})\n", hw.processore, hw.nuclei, if hw.aes { ", con AES-NI" } else { "" }));
    r.push_str(&format!("  memoria: {} MB\n", hw.ram_mb));
    for (nome, meccanico) in &hw.dischi {
        r.push_str(&format!("  disco {}: {}\n", nome, if *meccanico { "meccanico" } else { "a stato solido" }));
    }
    r.push_str(&format!("  scheda grafica: {}\n", if hw.grafica.is_empty() { "nessun driver".into() } else { hw.grafica.join(", ") }));
    for (zona, t) in &hw.temperature {
        r.push_str(&format!("  temperatura {}: {:.0} gradi\n", zona, t));
    }
    r.push_str(&format!("  ventole comandabili: {}\n", if hw.ventole.is_empty() { "nessuna".into() } else { hw.ventole.join(", ") }));
    r.push_str(&format!("  batteria: {}\n", if hw.batterie.is_empty() { "nessuna".into() } else { hw.batterie.join(", ") }));
    r.push_str("\nHo fatto\n");
    for f in &fatto {
        r.push_str(&format!("  {}\n", f));
    }
    r.push_str(&format!(
        "  schede del browser sveglie: {}\n  cifratura del disco consigliata: {}\n",
        if p.schede_attive == 0 { "senza limite".into() } else { p.schede_attive.to_string() },
        if hw.aes { "aes-xts" } else { "adiantum (il processore non ha AES-NI)" }
    ));
    if !errori.is_empty() {
        r.push_str("\nNon sono riuscito\n");
        for e in &errori {
            r.push_str(&format!("  {}\n", e));
        }
    }
    let _ = fs::write(RAPPORTO, &r);
    print!("{}", r);
    if errori.is_empty() { 0 } else { 1 }
}

fn main() {
    let codice = match std::env::args().nth(1).as_deref() {
        Some("applica") => applica(),
        Some("sorveglia") => sorveglia::sorveglia(),
        Some("utile") => sorveglia::utile(),
        Some("disattiva") => cambia_area(true, std::env::args().nth(2).as_deref()),
        Some("riattiva") => cambia_area(false, std::env::args().nth(2).as_deref()),
        Some("ripristina") => sorveglia::ripristina(),
        Some("mostra") => match leggi(RAPPORTO) {
            Some(r) => {
                println!("{}", r);
                0
            }
            None => {
                eprintln!("nessun rapporto: mantice non è ancora partito");
                1
            }
        },
        _ => {
            eprintln!("uso: mantice applica | mostra | sorveglia | ripristina | disattiva AREA | riattiva AREA");
            2
        }
    };
    std::process::exit(codice);
}

#[cfg(test)]
mod prove {
    use super::*;

    /// un /sys finto con ventola it87, thinkpad, batteria e cpufreq: la curva, il bios sopra 80 gradi,
    /// il sensore che sparisce, la frequenza che scende, la batteria all'80%
    #[test]
    fn ventole_temperatura_batteria() {
        use sorveglia::*;
        let radice = std::env::temp_dir().join(format!("mantice-prova-{}", std::process::id()));
        let _ = fs::remove_dir_all(&radice);
        let metti = |p: &str, v: &str| {
            let f = radice.join(p.trim_start_matches('/'));
            fs::create_dir_all(f.parent().unwrap()).unwrap();
            fs::write(f, v).unwrap();
        };
        let vedi = |p: &str| fs::read_to_string(radice.join(p.trim_start_matches('/'))).unwrap();
        metti("/sys/class/thermal/thermal_zone0/temp", "45000");
        metti("/sys/class/hwmon/hwmon0/name", "it87");
        metti("/sys/class/hwmon/hwmon0/pwm1", "255");
        metti("/sys/class/hwmon/hwmon0/pwm1_enable", "2");
        metti("/sys/class/hwmon/hwmon1/name", "pippo");
        metti("/sys/class/hwmon/hwmon1/pwm1", "255");
        metti("/sys/class/hwmon/hwmon1/pwm1_enable", "2");
        metti("/proc/acpi/ibm/fan", "status: enabled\nlevel: auto\ncommands: level <level> (<level> is 0-7, auto)");
        metti("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_min_freq", "800000");
        metti("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq", "2000000");
        metti("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq", "2000000");
        metti("/sys/class/power_supply/BAT0/type", "Battery");
        metti("/sys/class/power_supply/BAT0/charge_control_end_threshold", "100");
        metti("/sys/class/power_supply/BAT0/charge_control_start_threshold", "0");
        std::env::set_var("MANTICE_RADICE", &radice);

        // solo i driver conosciuti: it87 si', pippo no; piu' il thinkpad
        let v = trova_ventole();
        assert_eq!(v.len(), 2, "{:?}", v);
        assert!(!vedi("/sys/class/hwmon/hwmon1/pwm1_enable").contains('1'));

        // fresco: 30% e frequenza al massimo
        let stato = giro(&v, false);
        assert_eq!(vedi("/sys/class/hwmon/hwmon0/pwm1_enable"), "1");
        assert_eq!(vedi("/sys/class/hwmon/hwmon0/pwm1"), "77");
        assert_eq!(vedi("/proc/acpi/ibm/fan"), "level 2");
        assert!(stato.contains("ventole al 30%"), "{}", stato);

        // a meta' strada: 65%
        metti("/sys/class/thermal/thermal_zone0/temp", "62500");
        giro(&v, false);
        assert_eq!(vedi("/sys/class/hwmon/hwmon0/pwm1"), "166");

        // sopra 80: ventole al bios (valore originale), frequenza giu' di un decimo, mai sotto il 30%
        metti("/sys/class/thermal/thermal_zone0/temp", "85000");
        let stato = giro(&v, false);
        assert_eq!(vedi("/sys/class/hwmon/hwmon0/pwm1_enable"), "2");
        assert_eq!(vedi("/proc/acpi/ibm/fan"), "level auto");
        assert_eq!(vedi("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"), "1880000");
        for _ in 0..20 {
            giro(&v, false);
        }
        assert_eq!(vedi("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"), "1160000");
        assert!(stato.contains("al bios"), "{}", stato);

        // si raffredda: la frequenza risale
        metti("/sys/class/thermal/thermal_zone0/temp", "60000");
        giro(&v, false);
        assert_eq!(vedi("/sys/devices/system/cpu/cpu0/cpufreq/scaling_max_freq"), "1280000");

        // una lettura impossibile e' un allarme: ventole al bios
        metti("/sys/class/thermal/thermal_zone0/temp", "150000");
        let stato = giro(&v, false);
        assert_eq!(vedi("/sys/class/hwmon/hwmon0/pwm1_enable"), "2");
        assert!(stato.contains("al bios"), "{}", stato);
        metti("/sys/class/thermal/thermal_zone0/temp", "60000");
        giro(&v, false);

        // all'avvio la ventola era gia' in manuale: l'originale da rimettere e' comunque l'automatico
        metti("/sys/class/hwmon/hwmon0/pwm1_enable", "1");
        let v2 = trova_ventole();
        assert!(v2.iter().any(|x| matches!(x, Ventola::Pwm { originale, .. } if originale == "2")), "{:?}", v2);

        // il sensore sparisce: ventole al bios
        fs::remove_file(radice.join("sys/class/thermal/thermal_zone0/temp")).unwrap();
        let stato = giro(&v, false);
        assert_eq!(vedi("/sys/class/hwmon/hwmon0/pwm1_enable"), "2");
        assert!(stato.contains("non leggibile"), "{}", stato);

        // batteria
        let hw = rileva();
        let mut fatto = Vec::new();
        batteria(&hw, &mut fatto).unwrap();
        assert_eq!(vedi("/sys/class/power_supply/BAT0/charge_control_end_threshold"), "80");
        assert_eq!(vedi("/sys/class/power_supply/BAT0/charge_control_start_threshold"), "75");

        std::env::remove_var("MANTICE_RADICE");
        let _ = fs::remove_dir_all(&radice);
    }

    #[test]
    fn curva_delle_ventole() {
        assert_eq!(sorveglia::curva(30.0), 0.3);
        assert_eq!(sorveglia::curva(75.0), 1.0);
        assert!((sorveglia::curva(62.5) - 0.65).abs() < 1e-9);
    }

    #[test]
    fn fasce_di_memoria() {
        assert_eq!(profilo(480).schede_attive, 2);
        assert_eq!(profilo(970).zram_percento, 100);
        assert_eq!(profilo(1990).schede_attive, 6);
        assert_eq!(profilo(3900).schede_attive, 0);
    }
}
