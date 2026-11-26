//! mantice sorveglia: ogni 5 secondi guarda la temperatura e regola ventole e frequenza del processore.
//!
//! ventole: solo driver conosciuti (pwm1 di it87, nct6775 e dell_smm, livelli del thinkpad, applesmc dei mac
//! intel). curva: 30% fino a 50 gradi, sale fino al 100% a 75. mai sotto il 30%.
//! sicurezza: sopra 80 gradi, o se nessun sensore risponde, le ventole tornano al bios (valore originale).
//! frequenza: sopra 80 gradi il massimo scende di un decimo dell'intervallo per giro, mai sotto il 30%;
//! sotto 70 risale. "mantice ripristina" (allo stop del servizio) rimette tutto com'era.
//! le aree "ventole" e "processore" in /var/lib/mantice/disattivati spengono le due parti.

use crate::{esiste, leggi, scrivi, voci, DISATTIVATI, RUN};
use std::{fs, thread, time::Duration};

pub const SOGLIA_BIOS: f64 = 80.0;
const CALDO: f64 = 75.0;
const TIEPIDO: f64 = 50.0;
const FRESCO: f64 = 70.0;
const MINIMO: f64 = 0.3;
const DRIVER_PWM: [&str; 3] = ["it87", "nct6775", "dell_smm"];
const ORIGINALI: &str = "/run/mantice/ventole-originali";
const FAN_THINKPAD: &str = "/proc/acpi/ibm/fan";

#[derive(Debug, Clone, PartialEq)]
pub enum Ventola {
    Pwm { cartella: String, originale: String },
    Thinkpad,
    Applesmc { cartella: String, min: f64, max: f64 },
}

fn numero(percorso: String) -> Option<f64> {
    leggi(percorso)?.parse().ok()
}

pub fn trova_ventole() -> Vec<Ventola> {
    let mut v = Vec::new();
    for n in voci("/sys/class/hwmon") {
        let c = format!("/sys/class/hwmon/{}", n);
        let nome = leggi(format!("{}/name", c)).unwrap_or_default();
        if DRIVER_PWM.contains(&nome.as_str()) && esiste(format!("{}/pwm1", c)) && esiste(format!("{}/pwm1_enable", c)) {
            // il valore "del bios" vale solo se e' automatico (2 o piu'): manuale (1) o fisso (0) diventano 2
            let originale = leggi(format!("{}/pwm1_enable", c))
                .filter(|v| v.parse::<u32>().is_ok_and(|n| n >= 2))
                .unwrap_or_else(|| "2".into());
            v.push(Ventola::Pwm { cartella: c, originale });
        } else if nome == "applesmc" && esiste(format!("{}/fan1_manual", c)) {
            let min = numero(format!("{}/fan1_min", c)).unwrap_or(1000.0);
            let max = numero(format!("{}/fan1_max", c)).unwrap_or(6000.0);
            v.push(Ventola::Applesmc { cartella: c, min, max });
        }
    }
    // il thinkpad si lascia comandare solo con thinkpad_acpi fan_control=1 (in /etc/modprobe.d/mantice.conf)
    if leggi(FAN_THINKPAD).is_some_and(|f| f.contains("level <level>")) {
        v.push(Ventola::Thinkpad);
    }
    v
}

/// la temperatura piu' alta tra zone termiche e sensori hwmon; None se non si legge niente.
/// una lettura impossibile (130 gradi o piu') e' un allarme, non rumore: vale 130 e manda tutto al bios
pub fn temperatura() -> Option<f64> {
    let mut t: Vec<f64> = voci("/sys/class/thermal")
        .into_iter()
        .filter(|n| n.starts_with("thermal_zone"))
        .filter_map(|n| numero(format!("/sys/class/thermal/{}/temp", n)))
        .collect();
    for n in voci("/sys/class/hwmon") {
        for k in 1..=8 {
            if let Some(x) = numero(format!("/sys/class/hwmon/{}/temp{}_input", n, k)) {
                t.push(x);
            }
        }
    }
    t.into_iter().map(|m| m / 1000.0).filter(|x| *x > 0.0).map(|x| x.min(130.0)).reduce(f64::max)
}

/// da temperatura a quota della velocita'
pub fn curva(t: f64) -> f64 {
    if t <= TIEPIDO {
        MINIMO
    } else if t >= CALDO {
        1.0
    } else {
        MINIMO + (1.0 - MINIMO) * (t - TIEPIDO) / (CALDO - TIEPIDO)
    }
}

pub fn al_bios(ventole: &[Ventola]) {
    for v in ventole {
        match v {
            Ventola::Pwm { cartella, originale } => {
                let _ = scrivi(format!("{}/pwm1_enable", cartella), originale);
            }
            Ventola::Thinkpad => {
                let _ = scrivi(FAN_THINKPAD, "level auto");
            }
            Ventola::Applesmc { cartella, .. } => {
                let _ = scrivi(format!("{}/fan1_manual", cartella), "0");
            }
        }
    }
}

pub fn regola(ventole: &[Ventola], quota: f64) -> Result<(), String> {
    for v in ventole {
        match v {
            Ventola::Pwm { cartella, .. } => {
                scrivi(format!("{}/pwm1_enable", cartella), "1")?;
                scrivi(format!("{}/pwm1", cartella), &((quota * 255.0).round() as u32).to_string())?;
            }
            Ventola::Thinkpad => {
                scrivi(FAN_THINKPAD, &format!("level {}", ((quota * 7.0).round() as u32).clamp(1, 7)))?;
            }
            Ventola::Applesmc { cartella, min, max } => {
                scrivi(format!("{}/fan1_manual", cartella), "1")?;
                scrivi(format!("{}/fan1_output", cartella), &((min + (max - min) * quota).round() as u32).to_string())?;
            }
        }
    }
    Ok(())
}

fn nuclei() -> Vec<String> {
    voci("/sys/devices/system/cpu")
        .into_iter()
        .filter(|n| n.len() > 3 && n.starts_with("cpu") && n[3..].chars().all(|c| c.is_ascii_digit()))
        .collect()
}

/// il nuovo massimo della frequenza, applicato a tutti i nuclei; None se non e' regolabile
pub fn frequenza(t: f64) -> Option<u64> {
    let base = "/sys/devices/system/cpu/cpu0/cpufreq";
    let min = numero(format!("{}/cpuinfo_min_freq", base))? as u64;
    let max = numero(format!("{}/cpuinfo_max_freq", base))? as u64;
    let ora = numero(format!("{}/scaling_max_freq", base))? as u64;
    let passo = (max - min) / 10;
    let pavimento = min + (max - min) * 3 / 10;
    let nuova = if t >= SOGLIA_BIOS {
        ora.saturating_sub(passo).max(pavimento)
    } else if t <= FRESCO {
        (ora + passo).min(max)
    } else {
        ora
    };
    if nuova != ora {
        for cpu in nuclei() {
            let _ = scrivi(format!("/sys/devices/system/cpu/{}/cpufreq/scaling_max_freq", cpu), &nuova.to_string());
        }
    }
    Some(nuova)
}

/// un giro di sorveglianza; restituisce lo stato in una riga
pub fn giro(ventole: &[Ventola], frequenza_spenta: bool) -> String {
    let freq = |t: f64| {
        if frequenza_spenta {
            "frequenza lasciata stare".to_string()
        } else {
            frequenza(t).map(|f| format!("frequenza massima {} MHz", f / 1000)).unwrap_or_else(|| "frequenza non regolabile".into())
        }
    };
    match temperatura() {
        None => {
            al_bios(ventole);
            "temperatura non leggibile: ventole al bios".into()
        }
        Some(t) if t >= SOGLIA_BIOS => {
            al_bios(ventole);
            format!("{:.0} gradi: ventole al bios, {}", t, freq(t))
        }
        Some(t) => {
            let q = curva(t);
            let esito = if ventole.is_empty() {
                "nessuna ventola comandabile".to_string()
            } else {
                match regola(ventole, q) {
                    Ok(()) => format!("ventole al {:.0}%", q * 100.0),
                    Err(e) => {
                        al_bios(ventole);
                        format!("errore ({}), ventole al bios", e)
                    }
                }
            };
            format!("{:.0} gradi: {}, {}", t, esito, freq(t))
        }
    }
}

/// cosa c'e' da sorvegliare su questo computer, tolte le aree spente a mano
fn cosa_sorvegliare() -> (Vec<Ventola>, bool) {
    let disattivati = leggi(DISATTIVATI).unwrap_or_default();
    let spento = |a: &str| disattivati.lines().any(|l| l.trim() == a);
    let ventole = if spento("ventole") { Vec::new() } else { trova_ventole() };
    let frequenza_spenta = spento("processore") || !esiste("/sys/devices/system/cpu/cpu0/cpufreq");
    (ventole, frequenza_spenta)
}

/// mantice utile: 0 se c'e' qualcosa da sorvegliare (il servizio parte solo allora)
pub fn utile() -> i32 {
    let (ventole, frequenza_spenta) = cosa_sorvegliare();
    if ventole.is_empty() && frequenza_spenta { 1 } else { 0 }
}

pub fn sorveglia() -> i32 {
    let (ventole, frequenza_spenta) = cosa_sorvegliare();
    if ventole.is_empty() && frequenza_spenta {
        println!("niente da sorvegliare");
        return 0;
    }
    let _ = fs::create_dir_all(RUN);
    // per "mantice ripristina": i valori del bios, prima di toccare qualsiasi cosa
    let originali: String = ventole
        .iter()
        .filter_map(|v| match v {
            Ventola::Pwm { cartella, originale } => Some(format!("{}/pwm1_enable\t{}\n", cartella, originale)),
            _ => None,
        })
        .collect();
    let _ = fs::write(ORIGINALI, originali);
    loop {
        let stato = giro(&ventole, frequenza_spenta);
        let _ = fs::write(format!("{}/sorveglia", RUN), format!("{}\n", stato));
        thread::sleep(Duration::from_secs(5));
    }
}

/// allo stop del servizio: ventole al bios, frequenza al massimo
pub fn ripristina() -> i32 {
    // prima tutte le ventole trovate tornano all'automatico, poi i valori salvati all'avvio (se ci sono)
    // precisano quale automatico; un valore salvato che non e' automatico non si rimette
    al_bios(&trova_ventole());
    for riga in leggi(ORIGINALI).unwrap_or_default().lines() {
        if let Some((percorso, valore)) = riga.split_once('\t') {
            if valore.parse::<u32>().is_ok_and(|n| n >= 2) {
                let _ = scrivi(percorso, valore);
            }
        }
    }
    if let Some(max) = numero("/sys/devices/system/cpu/cpu0/cpufreq/cpuinfo_max_freq".into()) {
        for cpu in nuclei() {
            let _ = scrivi(format!("/sys/devices/system/cpu/{}/cpufreq/scaling_max_freq", cpu), &(max as u64).to_string());
        }
    }
    0
}
