// Package transport adapts TCP (with TLS 1.3) and QUIC (TLS 1.3 embutido) to
// uma única interface: um fluxo de bytes ordenado, confiável e criptografado.
// Quem usa o pacote enxerga apenas Stream, Dialer e Listener; acrescentar um
// novo transporte significa implementar essas três interfaces.
//
// Extraído do projeto aqt (middleware adaptativo TCP/QUIC) - meu repositorio privado.
package transport

import (
	"context"
	"crypto/tls"
	"errors"
	"fmt"
	"io"
	"net"
	"strings"
	"time"
)

// ALPN is the application protocol negotiated by TLS on both transports. A
// single identifier prevents a downgrade to an unrelated protocol on fallback.
const ALPN = "aqt/1"

// Kind identifies a transport protocol.
type Kind uint8

// Supported transports.
const (
	TCP  Kind = 1
	QUIC Kind = 2
)

// Kinds lists every supported transport.
var Kinds = []Kind{TCP, QUIC}

func (k Kind) String() string {
	switch k {
	case TCP:
		return "tcp"
	case QUIC:
		return "quic"
	case 0:
		return "-" // no transport (for example, before the first connection)
	}
	return fmt.Sprintf("kind(%d)", uint8(k))
}

// Other returns the transport that is not k (valid for TCP and QUIC only).
func (k Kind) Other() Kind {
	if k == TCP {
		return QUIC
	}
	return TCP
}

// ParseKind converts "tcp" or "quic" (any case) to a Kind.
func ParseKind(s string) (Kind, error) {
	switch strings.ToLower(strings.TrimSpace(s)) {
	case "tcp":
		return TCP, nil
	case "quic":
		return QUIC, nil
	}
	return 0, fmt.Errorf("transport: unknown kind %q", s)
}

// Stream is an ordered, reliable, encrypted byte stream between two peers.
type Stream interface {
	io.ReadWriteCloser
	SetReadDeadline(t time.Time) error
	SetWriteDeadline(t time.Time) error
	Kind() Kind
	LocalAddr() net.Addr
	RemoteAddr() net.Addr
	// Abort tears the stream down at once, without flushing. It is used on
	// failures and by fault-injection tests.
	Abort()
}

// Dialer opens outgoing streams of one transport.
type Dialer interface {
	Kind() Kind
	// Dial connects to addr ("host:port") and completes the cryptographic
	// handshake before returning.
	Dial(ctx context.Context, addr string) (Stream, error)
}

// Listener accepts incoming streams of one transport.
type Listener interface {
	Kind() Kind
	Accept(ctx context.Context) (Stream, error)
	Addr() net.Addr
	Close() error
}

// Config holds the settings shared by the TCP and QUIC adapters.
type Config struct {
	// TLS is the base TLS configuration. The adapters clone it and force
	// TLS 1.3 and the ALPN identifier. Servers need Certificates; clients
	// need ServerName and RootCAs (or, in tests only, InsecureSkipVerify).
	TLS *tls.Config
	// HandshakeTimeout bounds the transport and TLS handshakes. Default 5 s.
	HandshakeTimeout time.Duration
	// KeepAlive is the TCP keep-alive period and the QUIC keep-alive period.
	// Default 5 s.
	KeepAlive time.Duration
	// MaxIdle is the QUIC idle timeout. Default 15 s.
	MaxIdle time.Duration
	// TCPCongestion names the congestion controller of TCP sockets ("cubic",
	// "bbr", "reno"). Linux only; empty keeps the kernel default. QUIC always
	// uses the controller of quic-go (Cubic), so this knob exists to separate
	// the effect of the protocol from the effect of the controller.
	TCPCongestion string
}

func (c *Config) withDefaults() Config {
	var o Config
	if c != nil {
		o = *c
	}
	if o.HandshakeTimeout <= 0 {
		o.HandshakeTimeout = 5 * time.Second
	}
	if o.KeepAlive <= 0 {
		o.KeepAlive = 5 * time.Second
	}
	if o.MaxIdle <= 0 {
		o.MaxIdle = 15 * time.Second
	}
	return o
}

// tlsConfig returns a TLS 1.3-only clone carrying the ALPN identifier.
func (c Config) tlsConfig() (*tls.Config, error) {
	if c.TLS == nil {
		return nil, errors.New("transport: Config.TLS is required")
	}
	t := c.TLS.Clone()
	t.MinVersion = tls.VersionTLS13
	t.NextProtos = []string{ALPN}
	return t, nil
}

// ErrClosed is returned by Accept after the listener is closed.
var ErrClosed = errors.New("transport: listener closed")

// TLSState devolve o estado da sessão TLS 1.3 de um Stream (versão, suite de
// cifras, ALPN negociado). Serve para comprovar que o canal é criptografado. 
func TLSState(s Stream) (tls.ConnectionState, bool) {
	if cs, ok := s.(interface{ ConnectionState() tls.ConnectionState }); ok {
		return cs.ConnectionState(), true
	}
	return tls.ConnectionState{}, false
}

// :)