package main

import (
	"crypto/rand"
	"crypto/tls"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log"
	"net"
	"os"
	"strings"
	"sync"
	"sync/atomic"
	"time"
	"unicode/utf8"
)

// newUUID crea un identificador aleatorio para cuentas.
func newUUID() (string, error) {
	b := make([]byte, 16)
	if _, e := rand.Read(b); e != nil {
		return "", e
	}
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("%x-%x-%x-%x-%x", b[:4], b[4:6], b[6:8], b[8:10], b[10:]), nil
}

// receipt conserva temporalmente los destinatarios que deben confirmar recepción.
type receipt struct {
	at           time.Time
	target, text string
	waiting      map[string]bool
}
// session guarda transporte, identidad y cola de salida de una conexión cliente.
type session struct {
	conn    net.Conn
	out     chan packet
	user    User
	public  string
	recent  []time.Time
	once    sync.Once
	pending atomic.Int64
}

// close cierra la conexión una sola vez aunque varios procesos la soliciten.
func (s *session) close() { s.once.Do(func() { _ = s.conn.Close() }) }
// send encola un evento sin bloquear el hilo de manejo de la sesión.
func (s *session) send(p packet) bool {
	s.pending.Add(1)
	select {
	case s.out <- p:
		return true
	default:
		s.pending.Add(-1)
		s.close()
		return false
	}
}
// pump escribe los eventos en cola sobre la conexión cifrada.
func (s *session) pump() {
	for p := range s.out {
		_ = s.conn.SetWriteDeadline(time.Now().Add(5 * time.Second))
		e := writePacket(s.conn, p)
		s.pending.Add(-1)
		if e != nil {
			s.close()
			return
		}
	}
}

// Server reúne sockets, sesiones, reglas de acceso y estado temporal de recibos.
type Server struct {
	store         *Store
	fingerprint   string
	tls           *tls.Config
	host          string
	port, udpPort int
	allow         string
	mu            sync.Mutex
	connections   map[*session]bool
	sessions      map[string]*session
	seen          map[string]*receipt
	listener      net.Listener
	udp           *net.UDPConn
	stopped       chan struct{}
}

// newServer configura el estado en memoria sin abrir todavía los puertos.
func newServer(store *Store, tlsConfig *tls.Config, fingerprint, host string, port, udpPort int, allow string) *Server {
	return &Server{store: store, tls: tlsConfig, fingerprint: fingerprint, host: host, port: port, udpPort: udpPort, allow: allow, connections: map[*session]bool{}, sessions: map[string]*session{}, seen: map[string]*receipt{}, stopped: make(chan struct{})}
}
// allowed limita las conexiones TCP a loopback, la subred local o los CIDR configurados.
func (sv *Server) allowed(addr net.Addr) bool {
	host, _, e := net.SplitHostPort(addr.String())
	if e != nil {
		return false
	}
	ip := net.ParseIP(host)
	if ip == nil {
		return false
	}
	if sv.allow == "auto" {
		if ip.IsLoopback() {
			return true
		}
		iface := defaultRouteInterface()
		if iface == "" {
			return false
		}
		it, e := net.InterfaceByName(iface)
		if e != nil {
			return false
		}
		addrs, _ := it.Addrs()
		for _, a := range addrs {
			_, sub, e := net.ParseCIDR(a.String())
			if e == nil && sub.Contains(ip) {
				return true
			}
		}
		return false
	}
	for _, raw := range strings.Split(sv.allow, ",") {
		_, sub, e := net.ParseCIDR(strings.TrimSpace(raw))
		if e == nil && sub.Contains(ip) {
			return true
		}
	}
	return false
}
// defaultRouteInterface encuentra la interfaz IPv4 usada por la ruta principal.
func defaultRouteInterface() string {
	data, e := os.ReadFile("/proc/net/route")
	if e != nil {
		return ""
	}
	for _, line := range strings.Split(string(data), "\n")[1:] {
		fields := strings.Fields(line)
		if len(fields) > 3 && fields[1] == "00000000" && fields[3] != "0000" {
			return fields[0]
		}
	}
	return ""
}
// start abre TCP/UDP y lanza las rutinas de aceptación, descubrimiento y supervisión.
func (sv *Server) start() error {
	ln, e := net.Listen("tcp", fmt.Sprintf("%s:%d", sv.host, sv.port))
	if e != nil {
		return e
	}
	sv.listener = ln
	sv.port = ln.Addr().(*net.TCPAddr).Port
	addr := &net.UDPAddr{IP: net.ParseIP(sv.host), Port: sv.udpPort}
	udp, e := net.ListenUDP("udp4", addr)
	if e == nil {
		sv.udp = udp
		sv.udpPort = udp.LocalAddr().(*net.UDPAddr).Port
		go sv.discover()
	} else {
		log.Printf("Descubrimiento UDP no disponible: %v", e)
	}
	go sv.monitor()
	go sv.accept()
	return nil
}
// stop cierra sockets y sesiones activas para apagar el servicio limpiamente.
func (sv *Server) stop() {
	select {
	case <-sv.stopped:
		return
	default:
		close(sv.stopped)
	}
	if sv.listener != nil {
		sv.listener.Close()
	}
	if sv.udp != nil {
		sv.udp.Close()
	}
	sv.mu.Lock()
	for c := range sv.connections {
		c.close()
	}
	sv.mu.Unlock()
}
// discover responde por UDP a las búsquedas de clientes de la LAN.
func (sv *Server) discover() {
	buf := make([]byte, 256)
	for {
		n, addr, e := sv.udp.ReadFromUDP(buf)
		if e != nil {
			return
		}
		if string(buf[:n]) != "CHAT_LAN_DISCOVER_V1" {
			continue
		}
		reply, _ := json.Marshal(packet{"service": "chat-lan", "port": sv.port, "fingerprint": sv.fingerprint})
		_, _ = sv.udp.WriteToUDP(reply, addr)
	}
}
// accept filtra IPs, limita conexiones simultáneas y crea una sesión TLS por cliente.
func (sv *Server) accept() {
	for {
		c, e := sv.listener.Accept()
		if e != nil {
			return
		}
		if !sv.allowed(c.RemoteAddr()) {
			c.Close()
			continue
		}
		sv.mu.Lock()
		if len(sv.connections) >= 30 {
			sv.mu.Unlock()
			c.Close()
			continue
		}
		ss := &session{conn: tls.Server(c, sv.tls), out: make(chan packet, 100)}
		sv.connections[ss] = true
		sv.mu.Unlock()
		go sv.handle(ss)
	}
}
// presence difunde a las sesiones aprobadas la lista actual de usuarios conectados.
func (sv *Server) presence() {
	sv.mu.Lock()
	users := []packet{}
	for _, ss := range sv.sessions {
		users = append(users, ss.user.publicInfo())
	}
	recipients := make([]*session, 0, len(sv.sessions))
	for _, ss := range sv.sessions {
		recipients = append(recipients, ss)
	}
	sv.mu.Unlock()
	for _, ss := range recipients {
		ss.send(event("presence", packet{"users": users}))
	}
}
// handle ejecuta negociación TLS, reto Ed25519, autorización e intercambio de eventos.
func (sv *Server) handle(ss *session) {
	defer func() {
		sv.mu.Lock()
		delete(sv.connections, ss)
		active := ss.user.ID != "" && sv.sessions[ss.user.ID] == ss
		if active {
			delete(sv.sessions, ss.user.ID)
		}
		sv.mu.Unlock()
		if active {
			_ = sv.store.setOnline(ss.user.ID, false)
			sv.presence()
		}
		deadline := time.Now().Add(500 * time.Millisecond)
		for ss.pending.Load() > 0 && time.Now().Before(deadline) {
			time.Sleep(10 * time.Millisecond)
		}
		ss.close()
	}()
	tlsConn := ss.conn.(*tls.Conn)
	_ = tlsConn.SetDeadline(time.Now().Add(10 * time.Second))
	if e := tlsConn.Handshake(); e != nil {
		return
	}
	_ = tlsConn.SetDeadline(time.Time{})
	go ss.pump()
	_ = setDeadline(ss.conn, 15*time.Second)
	hello, e := readPacket(ss.conn)
	if e != nil {
		return
	}
	if field(hello, "type") != "hello" {
		ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
		return
	}
	public := field(hello, "public")
	if _, e = unb64(public, 32); e != nil {
		ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
		return
	}
	nonce, e := challenge()
	if e != nil {
		return
	}
	ss.send(event("challenge", packet{"nonce": nonce, "fingerprint": sv.fingerprint}))
	proof, e := readPacket(ss.conn)
	if e != nil {
		return
	}
	if field(proof, "type") != "proof" || verifyProof(public, nonce, sv.fingerprint, field(proof, "signature")) != nil {
		ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
		return
	}
	name, ok := hello["name"].(string)
	if !ok {
		ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
		return
	}
	u, e := sv.store.register(public, name)
	if e != nil {
		ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
		return
	}
	if u.Status != "approved" {
		// Los perfiles no aprobados reciben su estado y no entran a la sala.
		kind := map[string]string{"pending": "registration_pending", "revoked": "revoked", "deleted": "account_deleted"}[u.Status]
		ss.send(event(kind, u.publicInfo()))
		return
	}
	ss.user = u
	ss.public = public
	sv.mu.Lock()
	old := sv.sessions[u.ID]
	sv.sessions[u.ID] = ss
	sv.mu.Unlock()
	if e = sv.store.setOnline(u.ID, true); e != nil {
		return
	}
	if old != nil {
		old.send(event("replaced", nil))
		time.AfterFunc(200*time.Millisecond, old.close)
	}
	ss.send(event("authenticated", u.publicInfo()))
	// Después de autenticar, envía primero historial y luego publica presencia.
	history, e := sv.store.history(u.ID)
	if e == nil {
		for _, m := range history {
			ss.send(event("message", packet{"message_id": m.ID, "sender": packet{"id": m.Sender, "name": m.SenderName, "code": m.SenderCode}, "to": m.Target, "text": m.Text, "time": m.Time, "history": true}))
		}
	}
	sv.presence()
	for {
		_ = setDeadline(ss.conn, 90*time.Second)
		msg, e := readPacket(ss.conn)
		if e != nil {
			if !errors.Is(e, io.EOF) {
				var netErr net.Error
				if !errors.As(e, &netErr) && !errors.Is(e, net.ErrClosed) {
					ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
				}
			}
			return
		}
		sv.mu.Lock()
		currentSession := sv.sessions[u.ID] == ss
		sv.mu.Unlock()
		if !currentSession {
			return
		}
		current, e := sv.store.get(u.ID)
		if e != nil || current.Status != "approved" || current.Public != public {
			ss.send(event("revoked", nil))
			return
		}
		now := time.Now()
		cutoff := now.Add(-5 * time.Second)
		filtered := ss.recent[:0]
		for _, t := range ss.recent {
			if t.After(cutoff) {
				filtered = append(filtered, t)
			}
		}
		ss.recent = filtered
		if len(ss.recent) >= 50 {
			ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
			return
		}
		ss.recent = append(ss.recent, now)
		if e = sv.dispatch(ss, msg); e != nil {
			ss.send(event("error", packet{"text": "Solicitud o firma inválida"}))
			return
		}
	}
}
// dispatch valida y ejecuta cada evento del cliente ya autenticado.
func (sv *Server) dispatch(ss *session, p packet) error {
	switch field(p, "type") {
	case "ping":
		ss.send(event("pong", nil))
		return nil
	case "received":
		// Marca el acuse del destinatario y notifica al emisor cuando corresponde.
		sender, id := field(p, "sender"), field(p, "message_id")
		if sender == "" || id == "" {
			return errors.New("acuse inválido")
		}
		key := sender + ":" + id
		sv.mu.Lock()
		receipt := sv.seen[key]
		if receipt != nil && receipt.waiting[ss.user.ID] {
			delete(receipt.waiting, ss.user.ID)
			from := sv.sessions[sender]
			sv.mu.Unlock()
			if from != nil {
				from.send(event("delivered", packet{"message_id": id, "recipient": ss.user.ID}))
			}
		} else {
			sv.mu.Unlock()
		}
		return nil
	case "send_message":
		// Valida límites e ID antes de deduplicar, guardar y distribuir el texto.
		id, text, target := field(p, "message_id"), field(p, "text"), field(p, "to")
		text = strings.TrimSpace(text)
		if !uuidValid(id) || !utf8.ValidString(text) || utf8.RuneCountInString(text) < 1 || utf8.RuneCountInString(text) > 2000 || target == "" {
			return errors.New("mensaje inválido")
		}
		key := ss.user.ID + ":" + id
		sv.mu.Lock()
		for k, v := range sv.seen {
			if time.Since(v.at) > 300*time.Second {
				delete(sv.seen, k)
			}
		}
		if previous := sv.seen[key]; previous != nil {
			sv.mu.Unlock()
			if previous.target != target || previous.text != text {
				return errors.New("ID reutilizado")
			}
			ss.send(event("accepted", packet{"message_id": id, "duplicate": true}))
			return nil
		}
		recipients := []*session{}
		if target == "general" {
			for _, r := range sv.sessions {
				recipients = append(recipients, r)
			}
		} else if r := sv.sessions[target]; r != nil {
			recipients = append(recipients, r)
		}
		sv.mu.Unlock()
		if len(recipients) == 0 && target != "general" {
			ss.send(event("error", packet{"message_id": id, "text": "Destinatario desconectado"}))
			return nil
		}
		m := Message{ID: id, Sender: ss.user.ID, Target: target, Text: text, Time: time.Now().UTC().Format("2006-01-02T15:04:05.999999+00:00")}
		out := event("message", packet{"message_id": id, "sender": ss.user.publicInfo(), "to": target, "text": text, "time": m.Time})
		waiting := map[string]bool{}
		eligible := []*session{}
		for _, r := range recipients {
			current, e := sv.store.get(r.user.ID)
			if e == nil && current.Status == "approved" && current.Public == r.public {
				waiting[r.user.ID] = true
				eligible = append(eligible, r)
			}
		}
		if e := sv.store.saveMessage(m); e != nil {
			return e
		}
		sv.mu.Lock()
		sv.seen[key] = &receipt{at: time.Now(), target: target, text: text, waiting: waiting}
		if len(sv.seen) > 1000 {
			var oldestKey string
			var oldest time.Time
			for k, v := range sv.seen {
				if oldestKey == "" || v.at.Before(oldest) {
					oldestKey = k
					oldest = v.at
				}
			}
			delete(sv.seen, oldestKey)
		}
		sv.mu.Unlock()
		for _, r := range eligible {
			if !r.send(out) {
				sv.mu.Lock()
				delete(waiting, r.user.ID)
				sv.mu.Unlock()
			}
		}
		if target != "general" && recipients[0] != ss {
			ss.send(out)
		}
		ss.send(event("accepted", packet{"message_id": id, "duplicate": false}))
		return nil
	default:
		return errors.New("tipo inválido")
	}
}
// monitor aplica solicitudes administrativas de desconexión/eliminación y revisa cuentas.
func (sv *Server) monitor() {
	ticker := time.NewTicker(2 * time.Second)
	defer ticker.Stop()
	for {
		select {
		case <-sv.stopped:
			return
		case <-ticker.C:
			sv.mu.Lock()
			list := make([]*session, 0, len(sv.sessions))
			for _, ss := range sv.sessions {
				list = append(list, ss)
			}
			sv.mu.Unlock()
			for _, ss := range list {
				u, e := sv.store.get(ss.user.ID)
				if e != nil {
					continue
				}
				if u.Delete {
					ss.send(event("account_deleted", nil))
					time.Sleep(150 * time.Millisecond)
					ss.close()
					_ = sv.store.delete(u.ID, true)
				} else if u.Disconnect {
					_ = sv.store.clearDisconnect(u.ID)
					ss.send(event("admin_disconnected", nil))
					time.Sleep(50 * time.Millisecond)
					ss.close()
				} else if u.Status != "approved" || u.Public != ss.public {
					ss.send(event("revoked", nil))
					time.Sleep(50 * time.Millisecond)
					ss.close()
				}
			}
		}
	}
}
