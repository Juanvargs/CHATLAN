package main

import (
	"context"
	"encoding/json"
	"flag"
	"fmt"
	"log"
	"net"
	"os"
	"os/signal"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
)

// serverSettings contiene las opciones persistentes de ejecución.
type serverSettings struct {
	// Host y puertos controlan en qué direcciones escucha el servidor.
	Host          string `json:"host"`
	Port          int    `json:"port"`
	DiscoveryPort int    `json:"discovery_port"`
	// Allow limita quién se conecta; AutoApprove decide el estado inicial de nuevas cuentas.
	Allow         string `json:"allow"`
	AutoApprove   *bool  `json:"auto_approve"`
}

// readServerSettings carga settings.json; si no existe, deja que se usen los valores predeterminados.
func readServerSettings(dir string) (serverSettings, error) {
	var settings serverSettings
	// Une la carpeta de datos con el nombre fijo del archivo de configuración.
	data, err := os.ReadFile(filepath.Join(dir, "settings.json"))
	// La ausencia del archivo es normal: la aplicación seguirá con valores por defecto.
	if os.IsNotExist(err) {
		return settings, nil
	}
	if err != nil {
		return settings, err
	}
	// Convierte el JSON en opciones tipadas y da contexto si está mal formado.
	if err := json.Unmarshal(data, &settings); err != nil {
		return settings, fmt.Errorf("settings.json: %w", err)
	}
	return settings, nil
}

// defaultDir respeta XDG_DATA_HOME y usa ~/.local/share cuando no está definido.
func defaultDir() string {
	base := os.Getenv("XDG_DATA_HOME")
	// En Linux, XDG_DATA_HOME permite al usuario elegir dónde guardar datos de aplicaciones.
	if base == "" {
		home, _ := os.UserHomeDir()
		base = filepath.Join(home, ".local", "share")
	}
	return filepath.Join(base, "chat-lan-go", "server")
}
// main convierte los errores de run en un mensaje y un código de salida distinto de cero.
func main() {
	if e := run(os.Args[1:]); e != nil {
		fmt.Fprintln(os.Stderr, "Error:", e)
		os.Exit(1)
	}
}
// run interpreta el subcomando y ejecuta el servidor o una operación administrativa.
func run(args []string) error {
	if len(args) == 0 {
		return fmt.Errorf("uso: chat-lan-go server|init|list|fingerprint|approve|revoke|disconnect|delete|recover|backup [opciones]")
	}
	action := args[0]
	// Cada comando procesa sus banderas por separado para compartir opciones comunes.
	fs := flag.NewFlagSet(action, flag.ContinueOnError)
	// Define rutas y valores iniciales; el usuario puede reemplazarlos por banderas.
	dir := fs.String("data", defaultDir(), "carpeta de datos")
	host := fs.String("host", "0.0.0.0", "dirección de escucha")
	port := fs.Int("port", 8765, "puerto TCP")
	udpPort := fs.Int("discovery-port", 8764, "puerto UDP")
	allow := fs.String("allow", "auto", "auto o subredes CIDR separadas por coma")
	hosts := fs.String("hosts", "localhost,127.0.0.1", "SAN del certificado, separados por coma")
	initTLS := fs.Bool("init", false, "crear certificado si falta")
	if e := fs.Parse(args[1:]); e != nil {
		return e
	}
	tail := fs.Args()
	// tail contiene los argumentos posicionales que quedan después de procesar banderas.
	switch action {
	case "init":
		// Inicializa la identidad TLS y crea una base SQLite vacía.
		// El certificado permite que el cliente autentique al servidor antes del chat.
		fp, e := initializeTLS(*dir, strings.Split(*hosts, ","))
		if e != nil {
			return e
		}
		store, e := openStore(filepath.Join(*dir, "clients.sqlite3"), false)
		if e != nil {
			return e
		}
		store.close()
		// Muestra la huella para que se pueda comparar o incluir al preparar clientes.
		fmt.Println("Huella SHA-256:", fp)
		return nil
	case "fingerprint":
		// Calcula la huella del certificado para compararla con la que usa el cliente.
		_, fp, e := loadTLS(*dir)
		if e == nil {
			fmt.Println(fp)
		}
		return e
	case "server":
		// Combina configuración y banderas antes de abrir TLS, SQLite y los sockets.
		if len(tail) != 0 {
			return fmt.Errorf("argumentos inesperados")
		}
		settings, err := readServerSettings(*dir)
		if err != nil {
			return err
		}
		// Registra cuáles banderas se escribieron para distinguirlas de sus valores por defecto.
		provided := map[string]bool{}
		// Solo reemplaza valores predeterminados de banderas que el usuario no indicó.
		fs.Visit(func(f *flag.Flag) { provided[f.Name] = true })
		if !provided["host"] && settings.Host != "" {
			*host = settings.Host
		}
		if !provided["port"] && settings.Port != 0 {
			*port = settings.Port
		}
		if !provided["discovery-port"] && settings.DiscoveryPort != 0 {
			*udpPort = settings.DiscoveryPort
		}
		if !provided["allow"] && settings.Allow != "" {
			*allow = settings.Allow
		}
		autoApprove := true
		// Si el JSON no define esta opción, una identidad nueva se aprueba mientras haya cupo.
		if settings.AutoApprove != nil {
			autoApprove = *settings.AutoApprove
		}
		if ip := net.ParseIP(*host); ip == nil || ip.To4() == nil {
			return fmt.Errorf("--host exige una dirección IPv4")
		}
		if *port < 1 || *port > 65535 || *udpPort < 1 || *udpPort > 65535 {
			return fmt.Errorf("puerto inválido")
		}
		if *allow != "auto" {
			// Valida de antemano cada subred CIDR para no arrancar con una regla inválida.
			for _, raw := range strings.Split(*allow, ",") {
				if _, _, e := net.ParseCIDR(strings.TrimSpace(raw)); e != nil {
					return fmt.Errorf("subred inválida: %s", raw)
				}
			}
		}
		if *initTLS {
			// Permite crear certificados en el primer arranque sin sobrescribir identidad existente.
			_, certErr := os.Stat(filepath.Join(*dir, "server.pem"))
			_, keyErr := os.Stat(filepath.Join(*dir, "server.key"))
			if os.IsNotExist(certErr) && os.IsNotExist(keyErr) {
				if _, e := initializeTLS(*dir, strings.Split(*hosts, ",")); e != nil {
					return e
				}
			} else if certErr != nil || keyErr != nil {
				return fmt.Errorf("certificado o clave incompletos: revisa server.pem y server.key")
			}
		}
		tlsConfig, fp, e := loadTLS(*dir)
		if e != nil {
			return e
		}
		store, e := openStore(filepath.Join(*dir, "clients.sqlite3"), autoApprove)
		if e != nil {
			return e
		}
		defer store.close()
		if e = store.resetOnline(); e != nil {
			return e
		}
		sv := newServer(store, tlsConfig, fp, *host, *port, *udpPort, *allow)
		if e = sv.start(); e != nil {
			return e
		}
		log.Printf("Chat LAN Go: %s:%d UDP %d", *host, sv.port, *udpPort)
		// El contexto recibe la señal del sistema y permite apagar listeners y sesiones ordenadamente.
		ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
		defer stop()
		<-ctx.Done()
		sv.stop()
		return nil
	case "list", "approve", "revoke", "disconnect", "delete", "recover", "backup":
		// Ejecuta operaciones administrativas directamente sobre la base del servidor.
		store, e := openStore(filepath.Join(*dir, "clients.sqlite3"), false)
		if e != nil {
			return e
		}
		defer store.close()
		if action == "list" {
			users, e := store.list()
			if e != nil {
				return e
			}
			for _, u := range users {
				fmt.Printf("%s  %03d  %-8s %s\n", u.ID, u.Code, u.Status, u.Name)
			}
			return nil
		}
		if action == "backup" {
			if len(tail) != 1 {
				return fmt.Errorf("backup exige ruta")
			}
			return store.backup(tail[0])
		}
		if len(tail) < 1 {
			return fmt.Errorf("falta UUID")
		}
		switch action {
		case "approve":
			return store.setStatus(tail[0], "approved")
		case "revoke":
			return store.setStatus(tail[0], "revoked")
		case "disconnect":
			return store.disconnect(tail[0])
		case "delete":
			return store.delete(tail[0], false)
		case "recover":
			if len(tail) != 2 {
				return fmt.Errorf("recover exige UUID y clave pública")
			}
			return store.recover(tail[0], tail[1])
		}
	default:
		return fmt.Errorf("acción desconocida: %s", strconv.Quote(action))
	}
	return nil
}
