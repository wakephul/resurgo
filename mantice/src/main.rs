//! mantice: legge l'hardware all'avvio e adatta resurgo al computer su cui gira.
//!
//! gira come root dal servizio openrc, prima che parta il desktop.
//!   mantice applica   legge l'hardware, applica le regolazioni, scrive il rapporto
//!   mantice mostra    stampa l'ultimo rapporto
//!
//! cosa regola si puo' spegnere riga per riga in /var/lib/mantice/disattivati
//! (memoria, processore, disco, grafica): lo fa il benvenuto della shell. la radice e' in sola lettura,
//! per questo sta in /var. il file e' scrivibile dal gruppo resurgo.
//! in /var/lib/mantice/fatto ogni regolazione ha la sua area (area<TAB>testo), per il benvenuto.
//! solo libreria standard: un eseguibile piccolo.

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

fn leggi(percorso: impl AsRef<Path>) -> Option<String> {
    fs::read_to_string(percorso).ok().map(|s| s.trim().to_string())
}

fn scrivi(percorso: impl AsRef<Path>, valore: &str) -> Result<(), String> {
    let p = percorso.as_ref();
    fs::write(p, valore).map_err(|e| format!("{}: {}", p.display(), e))
}

fn voci(cartella: &str) -> Vec<String> {
    let mut v: Vec<String> = fs::read_dir(cartella)
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
        .filter_map(|n| fs::read_link(format!("/sys/class/drm/{}/device/driver", n)).ok())
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
        .filter(|n| Path::new(&format!("/sys/class/hwmon/{}/pwm1", n)).exists())
        .map(|n| leggi(format!("/sys/class/hwmon/{}/name", n)).unwrap_or(n))
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
        cpufreq: Path::new("/sys/devices/system/cpu/cpu0/cpufreq").exists(),
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
    if !Path::new("/sys/block/zram0").exists() {
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

/// il benvenuto (utente normale) deve poter spegnere le regolazioni: file del gruppo resurgo, 0664
fn prepara_disattivati() {
    use std::os::unix::fs::PermissionsExt;
    if !Path::new(DISATTIVATI).exists() {
        let _ = fs::write(DISATTIVATI, "");
    }
    let gid = leggi("/etc/group")
        .and_then(|g| g.lines().find(|l| l.starts_with("resurgo:")).and_then(|l| l.split(':').nth(2)).and_then(|n| n.parse::<u32>().ok()));
    if let Some(gid) = gid {
        let _ = std::os::unix::fs::chown(DISATTIVATI, Some(0), Some(gid));
    }
    let _ = fs::set_permissions(DISATTIVATI, fs::Permissions::from_mode(0o664));
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
    let passi: [(&str, Box<dyn Fn(&mut Vec<String>) -> Result<(), String>>); 4] = [
        ("memoria", Box::new(|f| memoria(&hw, &p, f))),
        ("processore", Box::new(|f| processore(&hw, f))),
        ("disco", Box::new(|f| disco(&hw, f))),
        ("grafica", Box::new(grafica)),
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
            eprintln!("uso: mantice applica | mantice mostra");
            2
        }
    };
    std::process::exit(codice);
}

#[cfg(test)]
mod prove {
    use super::*;

    #[test]
    fn fasce_di_memoria() {
        assert_eq!(profilo(480).schede_attive, 2);
        assert_eq!(profilo(970).zram_percento, 100);
        assert_eq!(profilo(1990).schede_attive, 6);
        assert_eq!(profilo(3900).schede_attive, 0);
    }
}
