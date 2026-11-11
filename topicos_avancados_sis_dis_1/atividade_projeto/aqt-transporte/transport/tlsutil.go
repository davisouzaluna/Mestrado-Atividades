package transport

import (
	"crypto/ecdsa"
	"crypto/elliptic"
	"crypto/rand"
	"crypto/tls"
	"crypto/x509"
	"crypto/x509/pkix"
	"encoding/pem"
	"errors"
	"math/big"
	"net"
	"time"
)

// DefaultServerName is the DNS name of the certificates SelfSigned issues.
// Clients that trust the generated pool set Config.TLS.ServerName to it, so
// the certificate is verified for real even when the address is an IP.
const DefaultServerName = "aqt.local"

// SelfSigned creates a throw-away ECDSA P-256 certificate valid for
// DefaultServerName, "localhost" and the given extra names or IP addresses. It
// returns the certificate for the server and a pool that clients use as RootCAs.
// It is meant for tests, experiments and demos, not for production.
func SelfSigned(extra ...string) (tls.Certificate, *x509.CertPool, error) {
	key, err := ecdsa.GenerateKey(elliptic.P256(), rand.Reader)
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	tpl := &x509.Certificate{
		SerialNumber:          big.NewInt(time.Now().UnixNano()),
		Subject:               pkix.Name{CommonName: DefaultServerName},
		NotBefore:             time.Now().Add(-time.Hour),
		NotAfter:              time.Now().Add(48 * time.Hour),
		KeyUsage:              x509.KeyUsageDigitalSignature,
		ExtKeyUsage:           []x509.ExtKeyUsage{x509.ExtKeyUsageServerAuth},
		BasicConstraintsValid: true,
		IsCA:                  true,
		DNSNames:              []string{DefaultServerName, "localhost"},
		IPAddresses:           []net.IP{net.ParseIP("127.0.0.1"), net.ParseIP("::1")},
	}
	for _, e := range extra {
		if ip := net.ParseIP(e); ip != nil {
			tpl.IPAddresses = append(tpl.IPAddresses, ip)
		} else {
			tpl.DNSNames = append(tpl.DNSNames, e)
		}
	}
	der, err := x509.CreateCertificate(rand.Reader, tpl, tpl, &key.PublicKey, key)
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	leaf, err := x509.ParseCertificate(der)
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	pool := x509.NewCertPool()
	pool.AddCert(leaf)
	return tls.Certificate{Certificate: [][]byte{der}, PrivateKey: key, Leaf: leaf}, pool, nil
}

// ServerTLS returns a server-side TLS configuration with the certificate.
func ServerTLS(cert tls.Certificate) *tls.Config {
	return &tls.Config{Certificates: []tls.Certificate{cert}}
}

// ClientTLS returns a client-side TLS configuration that verifies the server
// certificate against pool for DefaultServerName.
func ClientTLS(pool *x509.CertPool) *tls.Config {
	return &tls.Config{RootCAs: pool, ServerName: DefaultServerName}
}

// SelfSignedPEM is SelfSigned for tools that run as separate processes: it also
// returns the certificate in PEM form, which the server writes to a file and
// the client loads with PoolFromPEM.
func SelfSignedPEM(extra ...string) (tls.Certificate, []byte, error) {
	cert, _, err := SelfSigned(extra...)
	if err != nil {
		return tls.Certificate{}, nil, err
	}
	return cert, pem.EncodeToMemory(&pem.Block{Type: "CERTIFICATE", Bytes: cert.Certificate[0]}), nil
}

// PoolFromPEM builds a certificate pool from PEM data.
func PoolFromPEM(data []byte) (*x509.CertPool, error) {
	pool := x509.NewCertPool()
	if !pool.AppendCertsFromPEM(data) {
		return nil, errors.New("transport: no certificate found in PEM data")
	}
	return pool, nil
}
