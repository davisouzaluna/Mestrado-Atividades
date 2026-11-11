//go:build linux

package transport

import "syscall"

// tcpCongestionOpt is TCP_CONGESTION from <linux/tcp.h>.
const tcpCongestionOpt = 13

// tcpControl returns a net.Dialer/ListenConfig Control function that selects
// the congestion controller of the socket, or nil when cc is empty.
func tcpControl(cc string) func(network, address string, c syscall.RawConn) error {
	if cc == "" {
		return nil
	}
	return func(_, _ string, c syscall.RawConn) error {
		var serr error
		if err := c.Control(func(fd uintptr) {
			serr = syscall.SetsockoptString(int(fd), syscall.IPPROTO_TCP, tcpCongestionOpt, cc)
		}); err != nil {
			return err
		}
		return serr
	}
}
