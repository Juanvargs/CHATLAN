package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"encoding/base64"
	"encoding/json"
	"net"
	"os"
	"path/filepath"
	"testing"
	"time"
)

func receiveType(t *testing.T, c *tls.Conn, want string) packet {
	t.Helper()
	for i := 0; i < 12; i++ {
		_ = c.SetReadDeadline(time.Now().Add(3 * time.Second))
		p, e := readPacket(c)
		if e != nil {
			t.Fatal(e)
		}
		if field(p, "type") == want {
			return p
		}
	}
	t.Fatalf("no llegó %s", want)
	return nil
}
func loginTest(t *testing.T, sv *Server, certPath, name string) (*tls.Conn, string) {
	t.Helper()
	cert, e := os.ReadFile(certPath)
	if e != nil {
		t.Fatal(e)
	}
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(cert) {
		t.Fatal("certificado inválido")
	}
	c, e := tls.Dial("tcp", "127.0.0.1:"+itoa(sv.port), &tls.Config{RootCAs: pool, ServerName: "localhost", MinVersion: tls.VersionTLS12})
	if e != nil {
		t.Fatal(e)
	}
	pub, priv, e := ed25519.GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	public := base64.StdEncoding.EncodeToString(pub)
	if e = writePacket(c, event("hello", packet{"name": name, "public": public})); e != nil {
		t.Fatal(e)
	}
	challenge := receiveType(t, c, "challenge")
	payload, e := proofPayload(field(challenge, "nonce"), public, field(challenge, "fingerprint"))
	if e != nil {
		t.Fatal(e)
	}
	signature := base64.StdEncoding.EncodeToString(ed25519.Sign(priv, payload))
	if e = writePacket(c, event("proof", packet{"signature": signature})); e != nil {
		t.Fatal(e)
	}
	auth := receiveType(t, c, "authenticated")
	return c, field(auth, "id")
}
func itoa(n int) string {
	if n == 0 {
		return "0"
	}
	b := []byte{}
	for n > 0 {
		b = append([]byte{byte('0' + n%10)}, b...)
		n /= 10
	}
	return string(b)
}
func TestProtocolAndConversation(t *testing.T) {
	dir := t.TempDir()
	if _, e := initializeTLS(dir, []string{"localhost", "127.0.0.1"}); e != nil {
		t.Fatal(e)
	}
	config, fp, e := loadTLS(dir)
	if e != nil {
		t.Fatal(e)
	}
	store, e := openStore(filepath.Join(dir, "clients.sqlite3"), true)
	if e != nil {
		t.Fatal(e)
	}
	defer store.close()
	sv := newServer(store, config, fp, "127.0.0.1", 0, 0, "auto")
	if e = sv.start(); e != nil {
		t.Fatal(e)
	}
	defer sv.stop()
	udp, e := net.DialUDP("udp4", nil, &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: sv.udpPort})
	if e != nil {
		t.Fatal(e)
	}
	if _, e = udp.Write([]byte("CHAT_LAN_DISCOVER_V1")); e != nil {
		t.Fatal(e)
	}
	_ = udp.SetReadDeadline(time.Now().Add(time.Second))
	buf := make([]byte, 512)
	n, _, e := udp.ReadFromUDP(buf)
	if e != nil {
		t.Fatal(e)
	}
	udp.Close()
	var discovery packet
	if e = json.Unmarshal(buf[:n], &discovery); e != nil || field(discovery, "fingerprint") != fp || discovery["port"] != float64(sv.port) {
		t.Fatalf("descubrimiento: %v %v", discovery, e)
	}
	a, aID := loginTest(t, sv, filepath.Join(dir, "server.pem"), "Alicia")
	defer a.Close()
	b, bID := loginTest(t, sv, filepath.Join(dir, "server.pem"), "Beto")
	defer b.Close()
	id, e := newUUID()
	if e != nil {
		t.Fatal(e)
	}
	msg := event("send_message", packet{"message_id": id, "to": bID, "text": "Hola Beto"})
	if e = writePacket(a, msg); e != nil {
		t.Fatal(e)
	}
	m := receiveType(t, b, "message")
	if field(m, "text") != "Hola Beto" {
		t.Fatal(m)
	}
	accepted := receiveType(t, a, "accepted")
	if accepted["duplicate"] != false {
		t.Fatal(accepted)
	}
	if e = writePacket(b, event("received", packet{"sender": aID, "message_id": id})); e != nil {
		t.Fatal(e)
	}
	_ = receiveType(t, a, "delivered")
	history, e := store.history(bID)
	if e != nil || len(history) != 1 {
		t.Fatalf("historial: %v %v", history, e)
	}
	if e = writePacket(a, msg); e != nil {
		t.Fatal(e)
	}
	duplicate := receiveType(t, a, "accepted")
	if duplicate["duplicate"] != true {
		t.Fatal(duplicate)
	}
	if e = store.setStatus(bID, "revoked"); e != nil {
		t.Fatal(e)
	}
	_ = receiveType(t, b, "revoked")
}

func TestAccountsAndAdmin(t *testing.T) {
	dir := t.TempDir()
	store, e := openStore(filepath.Join(dir, "clients.sqlite3"), true)
	if e != nil {
		t.Fatal(e)
	}
	defer store.close()
	users := []User{}
	for i := 0; i < 11; i++ {
		pub, _, e := ed25519.GenerateKey(rand.Reader)
		if e != nil {
			t.Fatal(e)
		}
		u, e := store.register(base64.StdEncoding.EncodeToString(pub), "Usuario")
		if e != nil {
			t.Fatal(e)
		}
		users = append(users, u)
		want := "approved"
		if i == 10 {
			want = "pending"
		}
		if u.Status != want {
			t.Fatalf("cuenta %d: %s", i, u.Status)
		}
	}
	if e = store.setStatus(users[10].ID, "approved"); e == nil {
		t.Fatal("se aprobaron once cuentas")
	}
	if e = store.setStatus(users[0].ID, "revoked"); e != nil {
		t.Fatal(e)
	}
	if e = store.setStatus(users[10].ID, "approved"); e != nil {
		t.Fatal(e)
	}
	newKey, _, e := ed25519.GenerateKey(rand.Reader)
	if e != nil {
		t.Fatal(e)
	}
	if e = store.recover(users[1].ID, base64.StdEncoding.EncodeToString(newKey)); e != nil {
		t.Fatal(e)
	}
	if e = store.delete(users[1].ID, false); e != nil {
		t.Fatal(e)
	}
	deleted, e := store.register(base64.StdEncoding.EncodeToString(newKey), "Reingreso")
	if e != nil || deleted.Status != "deleted" {
		t.Fatalf("lápida: %v %v", deleted, e)
	}
	backup := filepath.Join(dir, "copia.sqlite3")
	if e = store.backup(backup); e != nil {
		t.Fatal(e)
	}
	if _, e = os.Stat(backup); e != nil {
		t.Fatal(e)
	}
}

func TestServerSettings(t *testing.T) {
	dir := t.TempDir()
	configured := []byte(`{"host":"127.0.0.1","port":19865,"discovery_port":19864,"allow":"127.0.0.0/8","auto_approve":false}`)
	if e := os.WriteFile(filepath.Join(dir, "settings.json"), configured, 0600); e != nil {
		t.Fatal(e)
	}
	settings, e := readServerSettings(dir)
	if e != nil {
		t.Fatal(e)
	}
	if settings.Host != "127.0.0.1" || settings.Port != 19865 || settings.DiscoveryPort != 19864 || settings.Allow != "127.0.0.0/8" || settings.AutoApprove == nil || *settings.AutoApprove {
		t.Fatalf("configuración incorrecta: %+v", settings)
	}
}
