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

// maxFrame limita el tamaño del JSON recibido o enviado por conexión.
const maxFrame = 65536

// packet es el objeto JSON común del protocolo de red.
type packet map[string]any

// event agrega versión y tipo a los campos específicos de cada evento.
func event(t string, fields packet) packet {
	// Todos los eventos llevan versión para que extremos incompatibles puedan rechazarse.
	p := packet{"v": 1, "type": t}
	// Copia los datos propios del evento al mismo objeto JSON.
	for k, v := range fields {
		p[k] = v
	}
	return p
}
// field obtiene un campo textual y devuelve cadena vacía si falta o tiene otro tipo.
func field(p packet, k string) string { s, _ := p[k].(string); return s }

// writePacket serializa JSON y lo precede con su longitud de 4 bytes big-endian.
func writePacket(w io.Writer, p packet) error {
	// JSON convierte el mapa Go en los bytes enviados por la conexión.
	b, e := json.Marshal(p)
	if e != nil {
		return e
	}
	if len(b) == 0 || len(b) > maxFrame {
		return errors.New("trama demasiado grande")
	}
	var header [4]byte
	// TCP no conserva límites de mensajes; este prefijo indica el largo del JSON.
	binary.BigEndian.PutUint32(header[:], uint32(len(b)))
	// Escribe prefijo y cuerpo juntos; TLS cifra ambos en el transporte.
	_, e = w.Write(append(header[:], b...))
	return e
}
// readPacket lee una trama completa y valida tamaño, JSON, versión y tipo.
func readPacket(r io.Reader) (packet, error) {
	var header [4]byte
	// ReadFull espera los cuatro bytes aunque TCP los entregue por partes.
	if _, e := io.ReadFull(r, header[:]); e != nil {
		return nil, e
	}
	n := binary.BigEndian.Uint32(header[:])
	// Rechaza longitudes vacías o excesivas antes de reservar memoria.
	if n == 0 || n > maxFrame {
		return nil, errors.New("longitud inválida")
	}
	b := make([]byte, n)
	// Lee exactamente el número de bytes indicado por el prefijo.
	if _, e := io.ReadFull(r, b); e != nil {
		return nil, e
	}
	var p packet
	// Interpreta el cuerpo como JSON y comprueba campos comunes del protocolo.
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
// unb64 decodifica Base64 estricto y exige el tamaño binario esperado.
func unb64(s string, n int) ([]byte, error) {
	b, e := base64.StdEncoding.Strict().DecodeString(s)
	if e != nil || len(b) != n {
		return nil, errors.New("credencial inválida")
	}
	return b, nil
}
// proofPayload arma los bytes exactos que cliente y servidor firman/verifican.
func proofPayload(nonce, public, fingerprint string) ([]byte, error) {
	// El nonce Base64 debe representar exactamente 32 bytes aleatorios.
	n, e := unb64(nonce, 32)
	if e != nil {
		return nil, e
	}
	// La clave pública Ed25519 también tiene un tamaño fijo de 32 bytes.
	p, e := unb64(public, 32)
	if e != nil {
		return nil, e
	}
	// La huella se transmite en hexadecimal, pero se firma en sus 32 bytes originales.
	f, e := hex.DecodeString(fingerprint)
	if e != nil || len(f) != 32 {
		return nil, errors.New("huella inválida")
	}
	// Este prefijo separa la firma de autenticación de firmas para otros usos.
	b := []byte("chat-lan/auth/v1\x00")
	b = append(b, n...)
	b = append(b, p...)
	return append(b, f...), nil
}
// challenge genera el nonce aleatorio que evita reutilizar una firma de autenticación.
func challenge() (string, error) {
	// rand.Read usa el generador criptográfico del sistema operativo.
	b := make([]byte, 32)
	_, e := rand.Read(b)
	return base64.StdEncoding.EncodeToString(b), e
}
// verifyProof valida la firma Ed25519 usando la clave pública declarada por el cliente.
func verifyProof(public, nonce, fingerprint, signature string) error {
	// Decodifica la clave y la firma con sus longitudes definidas por Ed25519.
	key, e := unb64(public, 32)
	if e != nil {
		return e
	}
	sig, e := unb64(signature, 64)
	if e != nil {
		return e
	}
	// Reconstruye los mismos bytes que el cliente firmó.
	payload, e := proofPayload(nonce, public, fingerprint)
	if e != nil {
		return e
	}
	// Verifica la firma; una clave que no corresponda produce rechazo.
	if !ed25519.Verify(key, payload, sig) {
		return errors.New("firma inválida")
	}
	return nil
}
// uuidValid comprueba la forma textual de un UUID antes de aceptar un mensaje.
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
// setDeadline evita que la lectura de la conexión quede bloqueada indefinidamente.
func setDeadline(c net.Conn, d time.Duration) error { return c.SetReadDeadline(time.Now().Add(d)) }
// require devuelve un error descriptivo cuando no se cumple una condición interna.
func require(cond bool, description string) error {
	if !cond {
		return fmt.Errorf("%s", description)
	}
	return nil
}
