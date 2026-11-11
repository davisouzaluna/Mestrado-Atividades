# aqt-transporte

Comunicação cliente-servidor sobre **TCP + TLS 1.3** e **QUIC (TLS 1.3)** atrás de uma única interface em Go.
Trecho extraído do projeto **aqt**, um middleware adaptativo TCP/QUIC.

## Estrutura

```
transport/
  transport.go   interfaces Stream, Dialer, Listener; tipo Kind (TCP, QUIC); Config; TLS 1.3 e ALPN "aqt/1"
  tcp.go         TCPDialer e TCPListener (TLS 1.3 sobre TCP)
  quic.go        QUICDialer e QUICListener (quic-go; um stream bidirecional por conexão)
  tlsutil.go     certificado autoassinado (ECDSA P-256) para testes e demonstrações
  tcpcc_*.go     escolha opcional do controle de congestionamento do TCP (só Linux)
  transport_test.go  testes: eco em TCP e QUIC, verificação real do certificado, falha rápida do QUIC
cmd/servidor     servidor de eco (devolve a mensagem em maiúsculas)
cmd/cliente      cliente que envia mensagens e mede o tempo de ida e volta
```

## Ideia central

TCP e QUIC têm APIs diferentes. O pacote esconde essa diferença atrás de três interfaces:

- `Dialer.Dial(ctx, addr)` abre a conexão e conclui o handshake criptográfico.
- `Listener.Accept(ctx)` entrega cada conexão aceita.
- `Stream` é um fluxo de bytes ordenado, confiável e criptografado (`Read`, `Write`, `Close`, prazos de leitura e escrita).

Quem usa o `Stream` não sabe qual transporte está por baixo. Nos dois casos:

- TLS 1.3 é obrigatório (`MinVersion = TLS 1.3`). No QUIC ele já faz parte do protocolo.
- O ALPN `aqt/1` é negociado, o que evita cair num protocolo diferente do esperado.
- O certificado do servidor é de fato verificado pelo cliente (`TestTLSVerificationIsReal`).
- O handshake tem prazo (`HandshakeTimeout`, 5 s por padrão), então uma porta UDP fechada falha rápido.

## Como executar

Requer Go 1.24 ou superior.

```bash
go mod tidy          # baixa o quic-go e gera o go.sum (precisa de internet, só na primeira vez)
go test ./transport/ # testes
```

Demonstração, em dois terminais (troque `tcp` por `quic` para o outro transporte):

```bash
# terminal 1
go run ./cmd/servidor -transporte tcp -addr 127.0.0.1:4433

# terminal 2
go run ./cmd/cliente -transporte tcp -addr 127.0.0.1:4433 -n 5
```

O servidor gera um certificado autoassinado e o grava em `server.pem`. O cliente o carrega e passa a confiar só nele.

Saída esperada do cliente:

```
conectado por tcp em 1.5ms
TLS versão 0x0304 (0x0304 = TLS 1.3), ALPN "aqt/1"
enviou "mensagem 1", recebeu "MENSAGEM 1" (49µs)
...
```

## Observações

- O certificado autoassinado serve só para teste e demonstração. Em produção, use um certificado emitido por uma CA.
- No QUIC, o servidor só vê o stream quando chegam os primeiros bytes, por isso o cliente escreve logo após o `Dial`.
- `Abort()` derruba a conexão sem enviar o encerramento educado (no TCP, com RST). `Close()` encerra de forma graciosa.
