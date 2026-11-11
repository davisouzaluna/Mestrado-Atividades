package transport

import (
	"bytes"
	"context"
	"io"
	"testing"
	"time"
)

func testConfigs(t *testing.T) (srv, cli *Config) {
	t.Helper()
	cert, pool, err := SelfSigned()
	if err != nil {
		t.Fatal(err)
	}
	return &Config{TLS: ServerTLS(cert)}, &Config{TLS: ClientTLS(pool)}
}

// echoOnce checks that bytes flow both ways on a Dialer/Listener pair.
func echoOnce(t *testing.T, l Listener, d Dialer) {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	done := make(chan error, 1)
	go func() {
		s, err := l.Accept(ctx)
		if err != nil {
			done <- err
			return
		}
		defer s.Close()
		buf := make([]byte, 5)
		if _, err := io.ReadFull(s, buf); err != nil {
			done <- err
			return
		}
		_, err = s.Write(bytes.ToUpper(buf))
		done <- err
	}()
	c, err := d.Dial(ctx, l.Addr().String())
	if err != nil {
		t.Fatalf("dial: %v", err)
	}
	defer c.Close()
	if c.Kind() != d.Kind() {
		t.Fatalf("kind %v", c.Kind())
	}
	if _, err := c.Write([]byte("hello")); err != nil { // QUIC: opens the stream on the peer
		t.Fatal(err)
	}
	got := make([]byte, 5)
	if _, err := io.ReadFull(c, got); err != nil || string(got) != "HELLO" {
		t.Fatalf("got %q err=%v", got, err)
	}
	if err := <-done; err != nil {
		t.Fatalf("server: %v", err)
	}
}

func TestTCP(t *testing.T) {
	s, c := testConfigs(t)
	l, err := ListenTCP("127.0.0.1:0", s)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	echoOnce(t, l, NewTCPDialer(c))
}

func TestQUIC(t *testing.T) {
	s, c := testConfigs(t)
	l, err := ListenQUIC("127.0.0.1:0", s)
	if err != nil {
		t.Fatal(err)
	}
	defer l.Close()
	echoOnce(t, l, NewQUICDialer(c))
}

func TestTLSVerificationIsReal(t *testing.T) {
	s, _ := testConfigs(t)
	// A client that does not trust the server certificate must fail on both transports.
	_, other, err := SelfSigned()
	if err != nil {
		t.Fatal(err)
	}
	bad := &Config{TLS: ClientTLS(other)}
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	tl, _ := ListenTCP("127.0.0.1:0", s)
	defer tl.Close()
	if _, err := NewTCPDialer(bad).Dial(ctx, tl.Addr().String()); err == nil {
		t.Fatal("TCP accepted an untrusted certificate")
	}
	ql, _ := ListenQUIC("127.0.0.1:0", s)
	defer ql.Close()
	if _, err := NewQUICDialer(bad).Dial(ctx, ql.Addr().String()); err == nil {
		t.Fatal("QUIC accepted an untrusted certificate")
	}
}

func TestQUICUnavailableFailsFast(t *testing.T) {
	_, c := testConfigs(t)
	c.HandshakeTimeout = 700 * time.Millisecond
	start := time.Now()
	// Nothing listens on this UDP port: the dial must give up at the timeout.
	_, err := NewQUICDialer(c).Dial(context.Background(), "127.0.0.1:1")
	if err == nil {
		t.Fatal("dial to a closed port succeeded")
	}
	if d := time.Since(start); d > 3*time.Second {
		t.Fatalf("took %v", d)
	}
}

func TestParseKind(t *testing.T) {
	for in, want := range map[string]Kind{"tcp": TCP, "QUIC": QUIC, " Tcp ": TCP} {
		if got, err := ParseKind(in); err != nil || got != want {
			t.Errorf("%q: %v %v", in, got, err)
		}
	}
	if _, err := ParseKind("udp"); err == nil {
		t.Error("udp must not parse yet")
	}
	if TCP.Other() != QUIC || QUIC.Other() != TCP {
		t.Error("Other")
	}
}
