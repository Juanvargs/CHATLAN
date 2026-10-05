package main

import (
	"crypto/ed25519"
	"crypto/rand"
	"encoding/base64"
	"encoding/binary"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net"
	"time"
)

const maxFrame = 65536

type packet map[string]any

func event(t string, fields packet) packet {
	p := packet{"v": 1, "type": t}
	for k, v := range fields {
		p[k] = v
	}
	return p
}
func field(p packet, k string) string { s, _ := p[k].(string); return s }

func writePacket(w io.Writer, p packet) error {
	b, e := json.Marshal(p)
	if e != nil {
		return e
	}
	if len(b) == 0 || len(b) > maxFrame {
		return errors.New("trama demasiado grande")
	}
	var header [4]byte
	binary.BigEndian.PutUint32(header[:], uint32(len(b)))
	_, e = w.Write(append(header[:], b...))
	return e
}
func readPacket(r io.Reader) (packet, error) {
	var header [4]byte
	if _, e := io.ReadFull(r, header[:]); e != nil {
		return nil, e
	}
	n := binary.BigEndian.Uint32(header[:])
	if n == 0 || n > maxFrame {
		return nil, errors.New("longitud inválida")
	}
	b := make([]byte, n)
	if _, e := io.ReadFull(r, b); e != nil {
		return nil, e
	}
	var p packet
	if e := json.Unmarshal(b, &p); e != nil {
		return nil, e
	}
	if p == nil || p["v"] != float64(1) {
		return nil, errors.New("protocolo incompatible")
	}
	if _, ok := p["type"].(string); !ok {
		return nil, errors.New("tipo inválido")
	}
	return p, nil
}
func unb64(s string, n int) ([]byte, error) {
	b, e := base64.StdEncoding.Strict().DecodeString(s)
	if e != nil || len(b) != n {
		return nil, errors.New("credencial inválida")
	}
	return b, nil
}
func proofPayload(nonce, public, fingerprint string) ([]byte, error) {
	n, e := unb64(nonce, 32)
	if e != nil {
		return nil, e
	}
	p, e := unb64(public, 32)
	if e != nil {
		return nil, e
	}
	f, e := hex.DecodeString(fingerprint)
	if e != nil || len(f) != 32 {
		return nil, errors.New("huella inválida")
	}
	b := []byte("chat-lan/auth/v1\x00")
	b = append(b, n...)
	b = append(b, p...)
	return append(b, f...), nil
}
func challenge() (string, error) {
	b := make([]byte, 32)
	_, e := rand.Read(b)
	return base64.StdEncoding.EncodeToString(b), e
}
func verifyProof(public, nonce, fingerprint, signature string) error {
	key, e := unb64(public, 32)
	if e != nil {
		return e
	}
	sig, e := unb64(signature, 64)
	if e != nil {
		return e
	}
	payload, e := proofPayload(nonce, public, fingerprint)
	if e != nil {
		return e
	}
	if !ed25519.Verify(key, payload, sig) {
		return errors.New("firma inválida")
	}
	return nil
}
func uuidValid(s string) bool {
	if len(s) != 36 {
		return false
	}
	for i, c := range s {
		if i == 8 || i == 13 || i == 18 || i == 23 {
			if c != '-' {
				return false
			}
			continue
		}
		if !((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) {
			return false
		}
	}
	return true
}
func setDeadline(c net.Conn, d time.Duration) error { return c.SetReadDeadline(time.Now().Add(d)) }
func require(cond bool, description string) error {
	if !cond {
		return fmt.Errorf("%s", description)
	}
	return nil
}
