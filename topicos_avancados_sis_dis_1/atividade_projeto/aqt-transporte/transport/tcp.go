package transport

import (
	"context"
	"crypto/tls"
	"net"
	"sync"
)

// tcpStream is a TLS 1.3 connection over TCP.
type tcpStream struct{ *tls.Conn }

func (s tcpStream) Kind() Kind { return TCP }

// Abort closes the socket without sending close_notify and asks the kernel to
// reset the connection.
func (s tcpStream) Abort() {
	nc := s.Conn.NetConn()
	if tc, ok := nc.(*net.TCPConn); ok {
		_ = tc.SetLinger(0)
	}
	_ = nc.Close()
}

// TCPDialer dials TLS 1.3 over TCP.
type TCPDialer struct{ cfg Config }

// NewTCPDialer returns a dialer for TCP with TLS 1.3.
func NewTCPDialer(cfg *Config) *TCPDialer { return &TCPDialer{cfg.withDefaults()} }

// Kind implements Dialer.
func (d *TCPDialer) Kind() Kind { return TCP }

// Dial implements Dialer.
func (d *TCPDialer) Dial(ctx context.Context, addr string) (Stream, error) {
	tc, err := d.cfg.tlsConfig()
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(ctx, d.cfg.HandshakeTimeout)
	defer cancel()
	nd := &net.Dialer{KeepAlive: d.cfg.KeepAlive, Control: tcpControl(d.cfg.TCPCongestion)}
	c, err := (&tls.Dialer{NetDialer: nd, Config: tc}).DialContext(ctx, "tcp", addr)
	if err != nil {
		return nil, err
	}
	return tcpStream{c.(*tls.Conn)}, nil
}

// TCPListener accepts TLS 1.3 connections over TCP. Handshakes run in their
// own goroutines, so a slow client does not block the accept loop.
type TCPListener struct {
	ln   net.Listener
	cfg  Config
	out  chan Stream
	done chan struct{}
	once sync.Once
}

// ListenTCP listens on addr ("host:port") for TLS 1.3 over TCP.
func ListenTCP(addr string, cfg *Config) (*TCPListener, error) {
	c := cfg.withDefaults()
	tc, err := c.tlsConfig()
	if err != nil {
		return nil, err
	}
	lc := net.ListenConfig{Control: tcpControl(c.TCPCongestion)} // accepted sockets inherit the controller
	ln, err := lc.Listen(context.Background(), "tcp", addr)
	if err != nil {
		return nil, err
	}
	l := &TCPListener{ln: ln, cfg: c, out: make(chan Stream, 64), done: make(chan struct{})}
	go l.loop(tc)
	return l, nil
}

func (l *TCPListener) loop(tc *tls.Config) {
	for {
		nc, err := l.ln.Accept()
		if err != nil {
			l.Close()
			return
		}
		if t, ok := nc.(*net.TCPConn); ok {
			_ = t.SetKeepAlive(true)
			_ = t.SetKeepAlivePeriod(l.cfg.KeepAlive)
		}
		go func() {
			sc := tls.Server(nc, tc)
			ctx, cancel := context.WithTimeout(context.Background(), l.cfg.HandshakeTimeout)
			defer cancel()
			if err := sc.HandshakeContext(ctx); err != nil {
				_ = nc.Close()
				return
			}
			select {
			case l.out <- tcpStream{sc}:
			case <-l.done:
				_ = nc.Close()
			default:
				_ = nc.Close() // accept queue full: shed load
			}
		}()
	}
}

// Kind implements Listener.
func (l *TCPListener) Kind() Kind { return TCP }

// Addr implements Listener.
func (l *TCPListener) Addr() net.Addr { return l.ln.Addr() }

// Accept implements Listener.
func (l *TCPListener) Accept(ctx context.Context) (Stream, error) {
	select {
	case s := <-l.out:
		return s, nil
	case <-l.done:
		return nil, ErrClosed
	case <-ctx.Done():
		return nil, ctx.Err()
	}
}

// Close implements Listener.
func (l *TCPListener) Close() error {
	var err error
	l.once.Do(func() { close(l.done); err = l.ln.Close() })
	return err
}
