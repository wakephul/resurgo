// fa partire un disco di resurgo aarch64 in una finestra, con Virtualization.framework di macOS.
// niente da installare: e' la stessa parte di macOS che usano i programmi di virtualizzazione veri.
// si compila e si avvia con build/prova-sul-mac.sh, che spiega anche le opzioni.
//   resurgo-vm disco.img [memoria in MB]
import Cocoa
import Virtualization

let argomenti = CommandLine.arguments
guard argomenti.count >= 2 else {
	print("uso: resurgo-vm disco.img [memoria in MB]")
	exit(2)
}
let disco = URL(fileURLWithPath: argomenti[1])
let memoriaMB = argomenti.count > 2 ? UInt64(argomenti[2]) ?? 1024 : 1024
// le variabili dell'uefi (l'ordine di avvio) stanno accanto al disco
let variabili = URL(fileURLWithPath: disco.path + ".efi")

func configurazione() throws -> VZVirtualMachineConfiguration {
	let c = VZVirtualMachineConfiguration()
	c.platform = VZGenericPlatformConfiguration()
	let avvio = VZEFIBootLoader()
	avvio.variableStore = FileManager.default.fileExists(atPath: variabili.path)
		? VZEFIVariableStore(url: variabili)
		: try VZEFIVariableStore(creatingVariableStoreAt: variabili)
	c.bootLoader = avvio
	c.cpuCount = 2
	c.memorySize = memoriaMB * 1024 * 1024

	let disco = try VZDiskImageStorageDeviceAttachment(url: disco, readOnly: false)
	c.storageDevices = [VZVirtioBlockDeviceConfiguration(attachment: disco)]

	let rete = VZVirtioNetworkDeviceConfiguration()
	rete.attachment = VZNATNetworkDeviceAttachment()
	c.networkDevices = [rete]

	let video = VZVirtioGraphicsDeviceConfiguration()
	video.scanouts = [VZVirtioGraphicsScanoutConfiguration(widthInPixels: 1280, heightInPixels: 800)]
	c.graphicsDevices = [video]
	c.keyboards = [VZUSBKeyboardConfiguration()]
	c.pointingDevices = [VZUSBScreenCoordinatePointingDeviceConfiguration()]
	c.entropyDevices = [VZVirtioEntropyDeviceConfiguration()]

	try c.validate()
	return c
}

final class Programma: NSObject, NSApplicationDelegate, NSWindowDelegate, VZVirtualMachineDelegate {
	var finestra: NSWindow!
	var macchina: VZVirtualMachine!

	func applicationDidFinishLaunching(_ notification: Notification) {
		do {
			macchina = VZVirtualMachine(configuration: try configurazione())
		} catch {
			esci("La macchina virtuale non si può preparare: \(error.localizedDescription)")
		}
		macchina.delegate = self

		let vista = VZVirtualMachineView()
		vista.virtualMachine = macchina
		vista.capturesSystemKeys = true
		// lo schermo resta a 1280x800: se seguisse la finestra, sul retina andrebbe al doppio e
		// pannello e scritte verrebbero minuscoli

		finestra = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 1280, height: 800),
			styleMask: [.titled, .closable, .miniaturizable, .resizable], backing: .buffered, defer: false)
		finestra.title = "Resurgo"
		finestra.contentView = vista
		finestra.delegate = self
		finestra.center()
		finestra.makeKeyAndOrderFront(nil)
		finestra.makeFirstResponder(vista)
		NSApp.activate(ignoringOtherApps: true)

		macchina.start { risultato in
			if case .failure(let errore) = risultato {
				self.esci("Resurgo non parte: \(errore.localizedDescription)")
			}
		}
	}

	// chiudere la finestra spegne di colpo, come togliere la corrente: il sistema e' in sola lettura,
	// i dati stanno su ext4 col journal
	func windowWillClose(_ notification: Notification) {
		if macchina.canStop {
			macchina.stop { _ in NSApp.terminate(nil) }
		} else {
			NSApp.terminate(nil)
		}
	}

	func guestDidStop(_ virtualMachine: VZVirtualMachine) { NSApp.terminate(nil) }

	func virtualMachine(_ virtualMachine: VZVirtualMachine, didStopWithError error: Error) {
		esci("Resurgo si è fermato per un errore: \(error.localizedDescription)")
	}

	func esci(_ messaggio: String) -> Never {
		FileHandle.standardError.write((messaggio + "\n").data(using: .utf8)!)
		exit(1)
	}
}

let app = NSApplication.shared
let programma = Programma()
app.delegate = programma
app.setActivationPolicy(.regular)
// un menu minimo, per uscire anche quando la tastiera va tutta a resurgo
let menu = NSMenu()
let voce = NSMenuItem()
menu.addItem(voce)
voce.submenu = NSMenu()
voce.submenu!.addItem(withTitle: "Esci da Resurgo", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
app.mainMenu = menu
app.run()
