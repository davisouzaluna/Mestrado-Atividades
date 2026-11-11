//go:build !linux

package transport

import "syscall"

func tcpControl(string) func(network, address string, c syscall.RawConn) error { return nil }
