package main

import (
	"os"
	"syscall"
	"unsafe"
)

// isTerminal: stdin is a real terminal (TCGETS works), not /dev/null or a pipe.
func isTerminal() bool {
	var t syscall.Termios
	_, _, e := syscall.Syscall(syscall.SYS_IOCTL, os.Stdin.Fd(), syscall.TCGETS, uintptr(unsafe.Pointer(&t)))
	return e == 0
}
