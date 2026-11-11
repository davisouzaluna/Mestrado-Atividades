// Servidor de eco sobre TCP+TLS 1.3 ou QUIC (TLS 1.3). Devolve em maiúsculas
// cada mensagem recebida (uma linha por mensagem).
//
//	go run ./cmd/servidor -transporte quic -addr 127.0.0.1:4433
package main

import (
	"bufio"
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"strings"

	"aqt-transporte/transport"
)

func main() {
	nome := flag.String("transporte", "tcp", "tcp ou quic")
	addr := flag.String("addr", "127.0.0.1:4433", "endereço host:porta")
	pemPath := flag.String("cert", "server.pem", "arquivo onde o certificado público é gravado")
	flag.Parse()

	kind, err := transport.ParseKind(*nome)
	if err != nil {
		log.Fatal(err)
	}
	cert, pemData, err := transport.SelfSignedPEM()
	if err != nil {
		log.Fatal(err)
	}
	if err := os.WriteFile(*pemPath, pemData, 0o644); err != nil {
		log.Fatal(err)
	}
	cfg := &transport.Config{TLS: transport.ServerTLS(cert)}

	var ln transport.Listener
	switch kind {
	case transport.TCP:
		ln, err = transport.ListenTCP(*addr, cfg)
	case transport.QUIC:
		ln, err = transport.ListenQUIC(*addr, cfg)
	}
	if err != nil {
		log.Fatal(err)
	}
	defer ln.Close()
	fmt.Printf("servidor %s ouvindo em %s (certificado em %s)\n", kind, ln.Addr(), *pemPath)

	for {
		s, err := ln.Accept(context.Background())
		if err != nil {
			log.Fatal(err)
		}
		go atender(s)
	}
}

func atender(s transport.Stream) {
	defer s.Close()
	if st, ok := transport.TLSState(s); ok {
		fmt.Printf("[%s] cliente %s: %s, ALPN %q\n", s.Kind(), s.RemoteAddr(), tlsNome(st.Version), st.NegotiatedProtocol)
	}
	r := bufio.NewReader(s)
	for {
		linha, err := r.ReadString('\n')
		if err != nil {
			return
		}
		if _, err := s.Write([]byte(strings.ToUpper(linha))); err != nil {
			return
		}
	}
}

func tlsNome(v uint16) string {
	if v == 0x0304 {
		return "TLS 1.3"
	}
	return fmt.Sprintf("TLS 0x%04x", v)
}
