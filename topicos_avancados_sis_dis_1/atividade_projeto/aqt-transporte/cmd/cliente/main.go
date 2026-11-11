// Cliente de eco: conecta por TCP+TLS 1.3 ou QUIC, envia mensagens e mede o
// tempo de ida e volta de cada uma.
//
//	go run ./cmd/cliente -transporte quic -addr 127.0.0.1:4433 -n 5
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"time"

	"aqt-transporte/transport"
)

func main() {
	nome := flag.String("transporte", "tcp", "tcp ou quic")
	addr := flag.String("addr", "127.0.0.1:4433", "endereço host:porta do servidor")
	pemPath := flag.String("cert", "server.pem", "certificado público gravado pelo servidor")
	n := flag.Int("n", 5, "número de mensagens")
	flag.Parse()

	kind, err := transport.ParseKind(*nome)
	if err != nil {
		log.Fatal(err)
	}
	pemData, err := os.ReadFile(*pemPath)
	if err != nil {
		log.Fatal(err)
	}
	pool, err := transport.PoolFromPEM(pemData)
	if err != nil {
		log.Fatal(err)
	}
	cfg := &transport.Config{TLS: transport.ClientTLS(pool)}

	var d transport.Dialer
	switch kind {
	case transport.TCP:
		d = transport.NewTCPDialer(cfg)
	case transport.QUIC:
		d = transport.NewQUICDialer(cfg)
	}

	t0 := time.Now()
	s, err := d.Dial(context.Background(), *addr)
	if err != nil {
		log.Fatalf("conexão falhou: %v", err)
	}
	defer s.Close()
	fmt.Printf("conectado por %s em %v\n", s.Kind(), time.Since(t0).Round(time.Microsecond))
	if st, ok := transport.TLSState(s); ok {
		fmt.Printf("TLS versão 0x%04x (0x0304 = TLS 1.3), ALPN %q\n", st.Version, st.NegotiatedProtocol)
	}

	r := bufio.NewReader(s)
	for i := 1; i <= *n; i++ {
		msg := fmt.Sprintf("mensagem %d", i)
		t := time.Now()
		if _, err := s.Write([]byte(msg + "\n")); err != nil {
			log.Fatal(err)
		}
		resp, err := r.ReadString('\n')
		if err != nil {
			log.Fatal(err)
		}
		fmt.Printf("enviou %q, recebeu %q (%v)\n", msg, resp[:len(resp)-1], time.Since(t).Round(time.Microsecond))
	}
}
