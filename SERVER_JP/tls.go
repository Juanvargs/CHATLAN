package main

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/sha256"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/hex"
	"encoding/pem"
	"errors"
	"math/big"
	"net"
	"os"
	"path/filepath"
	"time"
)

func privateDir(path string) error { return os.MkdirAll(path, 0700) }
func writeNew(path string, data []byte) error {
	f, e := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0600)
	if e != nil {
		return e
	}
	defer f.Close()
	_, e = f.Write(data)
	return e
}
func initializeTLS(dir string, hosts []string) (string, error) {
	if e := privateDir(dir); e != nil {
		return "", e
	}
	certPath, keyPath := filepath.Join(dir, "server.pem"), filepath.Join(dir, "server.key")
	if _, e := os.Stat(certPath); e == nil {
		return "", errors.New("ya existe server.pem")
	}
	if _, e := os.Stat(keyPath); e == nil {
		return "", errors.New("ya existe server.key")
	}
	key, e := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if e != nil {
		return "", e
	}
	serial, e := rand.Int(rand.Reader, new(big.Int).Lsh(big.NewInt(1), 128))
	if e != nil {
		return "", e
	}
	now := time.Now()
	cert := &x509.Certificate{SerialNumber: serial, Subject: pkix.Name{CommonName: "Chat LAN"}, NotBefore: now.Add(-5 * time.Minute), NotAfter: now.Add(365 * 24 * time.Hour), BasicConstraintsValid: true, IsCA: true, MaxPathLenZero: true, KeyUsage: x509.KeyUsageDigitalSignature | x509.KeyUsageCertSign | x509.KeyUsageCRLSign, ExtKeyUsage: []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth}}
	for _, h := range hosts {
		if ip := net.ParseIP(h); ip != nil {
			cert.IPAddresses = append(cert.IPAddresses, ip)
		} else {
			cert.DNSNames = append(cert.DNSNames, h)
		}
	}
	der, e := x509.CreateCertificate(rand.Reader, cert, cert, &key.PublicKey, key)
	if e != nil {
		return "", e
	}
	priv, e := x509.MarshalPKCS8PrivateKey(key)
	if e != nil {
		return "", e
	}
	if e = writeNew(keyPath, pem.EncodeToMemory(&pem.Block{Type: "PRIVATE KEY", Bytes: priv})); e != nil {
		return "", e
	}
	if e = writeNew(certPath, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: der})); e != nil {
		return "", e
	}
	sum := sha256.Sum256(der)
	return hex.EncodeToString(sum[:]), nil
}
func loadTLS(dir string) (*tls.Config, string, error) {
	cert, e := tls.LoadX509KeyPair(filepath.Join(dir, "server.pem"), filepath.Join(dir, "server.key"))
	if e != nil {
		return nil, "", e
	}
	sum := sha256.Sum256(cert.Certificate[0])
	return &tls.Config{Certificates: []tls.Certificate{cert}, MinVersion: tls.VersionTLS12}, hex.EncodeToString(sum[:]), nil
}
