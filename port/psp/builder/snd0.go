package main

// XMB music (SND0.AT3) from the disc: the character select track, encoded by
// mvc2-snd0 (port/psp/snd0), which the release packages put next to the base
// EBOOT. Same as psp_pack.py's.

import (
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

const selectTrack = 15 // AFS00 entry: the character select music

// xmbSound returns SND0.AT3 made with the mvc2-snd0 found in toolDir.
func xmbSound(d *iso, toolDir string) ([]byte, error) {
	name := "mvc2-snd0"
	if runtime.GOOS == "windows" {
		name += ".exe"
	}
	tool, err := filepath.Abs(filepath.Join(toolDir, name))
	if err != nil {
		return nil, err
	}
	if _, err := os.Stat(tool); err != nil {
		return nil, fmt.Errorf("no %s next to the base EBOOT", name)
	}
	a, err := openAFS(d, 0)
	if err != nil {
		return nil, err
	}
	if len(a.entries) <= selectTrack {
		return nil, errBadData
	}
	e := a.entries[selectTrack]
	adx, err := d.read(a.path, int64(e.off), int64(e.size))
	if err != nil {
		return nil, err
	}
	tmp, err := os.MkdirTemp("", "mvc2-snd0-")
	if err != nil {
		return nil, err
	}
	defer os.RemoveAll(tmp)
	if err := os.WriteFile(filepath.Join(tmp, "track.adx"), adx, 0o600); err != nil {
		return nil, err
	}
	// relative names: the tool takes no non-ASCII paths on Windows
	cmd := exec.Command(tool, "track.adx", "SND0.AT3")
	cmd.Dir = tmp
	if msg, err := cmd.CombinedOutput(); err != nil {
		return nil, fmt.Errorf("%s: %v %s", name, err, strings.TrimSpace(string(msg)))
	}
	return os.ReadFile(filepath.Join(tmp, "SND0.AT3"))
}
