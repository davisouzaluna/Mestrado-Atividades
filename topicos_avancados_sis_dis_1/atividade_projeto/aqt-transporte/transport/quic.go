package transport

import (
	"context"
	"crypto/tls"
	"net"
	"sync"
	"time"

	"github.com/quic-go/quic-go"
)

// quicStream is the first bidirectional stream of a QUIC connection. The
// middleware uses one stream per link, so the stream and the connection have
// the same lifetime.
type quicStream struct {
	st *quic.Stream
	c  *quic.Conn
}

func (s *quicStream) Read(p []byte) (int, error)           { return s.st.Read(p) }
func (s *quicStream) Write(p []byte) (int, error)          { return s.st.Write(p) }
func (s *quicStream) SetReadDeadline(t time.Time) error    { return s.st.SetReadDeadline(t) }
func (s *quicStream) SetWriteDeadline(t time.Time) error   { return s.st.SetWriteDeadline(t) }
func (s *quicStream) Kind() Kind                           { return QUIC }
func (s *quicStream) ConnectionState() tls.ConnectionState { return s.c.ConnectionState().TLS }
func (s *quicStream) LocalAddr() net.Addr                  { return s.c.LocalAddr() }
func (s *quicStream) RemoteAddr() net.Addr                 { return s.c.RemoteAddr() }

// closeGrace is how long Close keeps the QUIC connection open so that data
// already written to the stream is flushed and acknowledged before the
// connection closes.
const closeGrace = 2 * time.Second

// Close ends the stream gracefully: it sends FIN after the pending data and
// closes the connection when the peer closes it or after closeGrace, whichever
// comes first. It does not block.
func (s *quicStream) Close() error {
	err := s.st.Close()
	go func() {
		t := time.NewTimer(closeGrace)
		defer t.Stop()
		select {
		case <-s.c.Context().Done():
		case <-t.C:
		}
		_ = s.c.CloseWithError(0, "closed")
	}()
	return err
}

func (s *quicStream) Abort() {
	s.st.CancelRead(1)
	s.st.CancelWrite(1)
	_ = s.c.CloseWithError(1, "aborted")
}

func quicConfig(c Config) *quic.Config {
	return &quic.Config{
		HandshakeIdleTimeout: c.HandshakeTimeout,
		MaxIdleTimeout:       c.MaxIdle,
		KeepAlivePeriod:      c.KeepAlive,
	}
}

// QUICDialer dials QUIC (TLS 1.3 is part of the protocol).
type QUICDialer struct{ cfg Config }

// NewQUICDialer returns a dialer for QUIC.
func NewQUICDialer(cfg *Config) *QUICDialer { return &QUICDialer{cfg.withDefaults()} }

// Kind implements Dialer.
func (d *QUICDialer) Kind() Kind { return QUIC }

// Dial implements Dialer. It returns after the QUIC handshake and after the
// stream is open. The peer sees the stream only when the first bytes arrive,
// so the caller must write the session handshake frame right away.
func (d *QUICDialer) Dial(ctx context.Context, addr string) (Stream, error) {
	tc, err := d.cfg.tlsConfig()
	if err != nil {
		return nil, err
	}
	ctx, cancel := context.WithTimeout(ctx, d.cfg.HandshakeTimeout)
	defer cancel()
	c, err := quic.DialAddr(ctx, addr, tc, quicConfig(d.cfg))
	if err != nil {
		return nil, err
	}
	st, err := c.OpenStreamSync(ctx)
	if err != nil {
		_ = c.CloseWithError(1, "open stream")
		return nil, err
	}
	return &quicStream{st: st, c: c}, nil
}

// QUICListener accepts QUIC connections and returns the first stream of each.
type QUICListener struct {
	ln   *quic.Listener
	cfg  Config
	out  chan Stream
	done chan struct{}
	once sync.Once
}

// ListenQUIC listens on addr ("host:port", UDP) for QUIC.
func ListenQUIC(addr string, cfg *Config) (*QUICListener, error) {
	c := cfg.withDefaults()
	tc, err := c.tlsConfig()
	if err != nil {
		return nil, err
	}
	ln, err := quic.ListenAddr(addr, tc, quicConfig(c))
	if err != nil {
		return nil, err
	}
	l := &QUICListener{ln: ln, cfg: c, out: make(chan Stream, 64), done: make(chan struct{})}
	go l.loop()
	return l, nil
}

func (l *QUICListener) loop() {
	for {
		c, err := l.ln.Accept(context.Background())
		if err != nil {
			l.Close()
			return
		}
		go func() {
			ctx, cancel := context.WithTimeout(context.Background(), l.cfg.HandshakeTimeout)
			defer cancel()
			st, err := c.AcceptStream(ctx)
			if err != nil {
				_ = c.CloseWithError(1, "no stream")
				return
			}
			select {
			case l.out <- &quicStream{st: st, c: c}:
			case <-l.done:
				_ = c.CloseWithError(1, "closing")
			default:
				_ = c.CloseWithError(1, "busy")
			}
		}()
	}
}

// Kind implements Listener.
func (l *QUICListener) Kind() Kind { return QUIC }

// Addr implements Listener.
func (l *QUICListener) Addr() net.Addr { return l.ln.Addr() }

// Accept implements Listener.
func (l *QUICListener) Accept(ctx context.Context) (Stream, error) {
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
func (l *QUICListener) Close() error {
	var err error
	l.once.Do(func() { close(l.done); err = l.ln.Close() })
	return err
}
