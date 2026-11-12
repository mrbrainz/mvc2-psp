// mvc2-eboot-builder: makes a ready-to-play MVC2/EBOOT.PBP from the player's
// own Marvel vs. Capcom 2 (USA) PS2 disc image and the release's base EBOOT.
//
//	mvc2-eboot-builder [-base EBOOT.PBP] [-o OUTDIR] [game.iso]
//
// With no ISO argument (double-click, or a macOS .app) it asks with the
// system file picker and reports the result in a dialog.
package main

import (
	"bufio"
	"bytes"
	"flag"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"strings"
)

var (
	gui   bool // started without an ISO argument: talk through dialogs
	pause bool // wait for Enter before exiting (Windows console)
)

func main() {
	basePath := flag.String("base", "", "base EBOOT.PBP (default: found next to the builder)")
	outDir := flag.String("o", "", "folder to create MVC2/ in (default: next to the ISO)")
	quiet := flag.Bool("q", false, "never wait for Enter or show dialogs")
	flag.Parse()
	isoPath := flag.Arg(0)
	pause = runtime.GOOS == "windows" && !*quiet // drag-and-drop opens a console that would close at once
	if runtime.GOOS == "linux" && !*quiet && !isTerminal() {
		// Started from a file manager (double-click or a dropped ISO): no
		// terminal to show progress or errors in, so reopen in one.
		if os.Getenv(termEnv) == "" && relaunchInTerminal() {
			return
		}
		gui = true // no terminal program: report through zenity dialogs
	}
	if os.Getenv(termEnv) != "" {
		pause = true
	}
	if isoPath == "" {
		gui = true
		var err error
		isoPath, err = pickFile()
		if _, cancelled := err.(*exec.ExitError); err != nil && !cancelled {
			fail(err.Error())
		}
		if isoPath == "" {
			fail("No disc image chosen.")
		}
	}
	out, err := run(isoPath, *basePath, *outDir)
	if err != nil {
		fail(err.Error())
	}
	done(fmt.Sprintf("Done: %s\n\nCopy the MVC2 folder into PSP/GAME/ on your memory stick. "+
		"The ISO is not needed on the PSP.\n\nThis EBOOT contains game data: do not share it.", out))
}

func run(isoPath, basePath, outDir string) (string, error) {
	if err := checkImage(isoPath); err != nil {
		return "", err
	}
	if basePath == "" {
		basePath = findBase()
	}
	if basePath == "" {
		return "", fmt.Errorf("can't find the base EBOOT.PBP that comes with the builder; keep the release folder together")
	}
	base, err := os.ReadFile(basePath)
	if err != nil {
		return "", err
	}
	d, err := openISO(isoPath)
	if err != nil {
		return "", fmt.Errorf("%s: %v", filepath.Base(isoPath), err)
	}
	defer d.f.Close()
	if _, ok := d.files["/SLUS_204.86"]; !ok {
		for k := range d.files {
			if strings.HasPrefix(k, "/SLES_") || strings.HasPrefix(k, "/SLPM_") || strings.HasPrefix(k, "/SLPS_") {
				return "", fmt.Errorf("this is the %s release (%s); only the USA disc (SLUS-20486) is supported", region(k), k[1:])
			}
		}
		return "", fmt.Errorf("%s is not Marvel vs. Capcom 2 for PS2 (USA, SLUS-20486)", filepath.Base(isoPath))
	}
	if outDir == "" {
		outDir = filepath.Dir(isoPath)
	}
	dir := filepath.Join(outDir, "MVC2")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	out := filepath.Join(dir, "EBOOT.PBP")
	fmt.Printf("Building %s\n", out)
	last := -1
	err = build(base, d, out, func(done, total int64) {
		if p := int(done * 100 / total); p != last {
			last = p
			fmt.Printf("\r%3d%%", p)
		}
	})
	fmt.Println()
	if err != nil {
		os.Remove(dir) // only if empty
		return "", err
	}
	return out, nil
}

// checkImage catches the common wrong inputs before parsing.
func checkImage(path string) error {
	f, err := os.Open(path)
	if err != nil {
		return err
	}
	defer f.Close()
	head := make([]byte, 16)
	f.Read(head)
	raw := make([]byte, 6)
	f.ReadAt(raw, 16*2352+16)
	switch {
	case strings.EqualFold(filepath.Ext(path), ".cue"):
		return fmt.Errorf("that's a .cue sheet; convert the BIN/CUE to an .iso first")
	case bytes.HasPrefix(head, []byte("CISO")) || bytes.HasPrefix(head, []byte("ZISO")):
		return fmt.Errorf("that's a compressed CSO/ZSO image; decompress it to an .iso first")
	case bytes.HasPrefix(head, []byte("MComprHD")):
		return fmt.Errorf("that's a CHD image; convert it to an .iso first (chdman extractdvd)")
	case bytes.HasPrefix(head, []byte("PK\x03\x04")) || bytes.HasPrefix(head, []byte("7z\xbc\xaf")) || bytes.HasPrefix(head, []byte("Rar!")):
		return fmt.Errorf("that's an archive; extract the .iso from it first")
	case string(raw[1:6]) == "CD001":
		return fmt.Errorf("that's a raw BIN (2352-byte sectors); convert the BIN/CUE to an .iso first")
	}
	return nil
}

func region(serial string) string {
	if strings.HasPrefix(serial, "/SLES_") {
		return "European"
	}
	return "Japanese"
}

// findBase looks for the base EBOOT where the release packages put it.
func findBase() string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	if r, err := filepath.EvalSymlinks(exe); err == nil {
		exe = r
	}
	dir := filepath.Dir(exe)
	for _, p := range []string{
		filepath.Join(dir, "base", "EBOOT.PBP"),
		filepath.Join(dir, "..", "Resources", "EBOOT.PBP"), // macOS .app
		filepath.Join(dir, "..", "base", "EBOOT.PBP"),
	} {
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p
		}
	}
	return ""
}

const termEnv = "MVC2_BUILDER_IN_TERMINAL"

// relaunchInTerminal reopens the builder, with the same arguments, in the
// first terminal program found; false if there is none.
func relaunchInTerminal() bool {
	exe, err := os.Executable()
	if err != nil {
		return false
	}
	cmd := append([]string{exe}, os.Args[1:]...)
	for _, t := range []struct {
		name string
		flag []string
	}{
		{"x-terminal-emulator", []string{"-e"}},
		{"xfce4-terminal", []string{"-x"}},
		{"gnome-terminal", []string{"--"}},
		{"konsole", []string{"-e"}},
		{"xterm", []string{"-e"}},
	} {
		path, err := exec.LookPath(t.name)
		if err != nil {
			continue
		}
		c := exec.Command(path, append(t.flag, cmd...)...)
		c.Env = append(os.Environ(), termEnv+"=1")
		if c.Start() == nil {
			return true
		}
	}
	return false
}

const prompt = "Choose your Marvel vs. Capcom 2 (USA) PS2 disc image (.iso)"

func pickFile() (string, error) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "darwin":
		cmd = exec.Command("osascript", "-e", `POSIX path of (choose file with prompt "`+prompt+`")`)
	case "windows":
		cmd = exec.Command("powershell", "-NoProfile", "-STA", "-Command",
			`Add-Type -AssemblyName System.Windows.Forms; $d = New-Object System.Windows.Forms.OpenFileDialog; `+
				`$d.Title = '`+prompt+`'; $d.Filter = 'PS2 disc image (*.iso)|*.iso|All files (*.*)|*.*'; `+
				`if ($d.ShowDialog() -eq 'OK') { $d.FileName }`)
	default:
		if _, err := exec.LookPath("zenity"); err == nil {
			cmd = exec.Command("zenity", "--file-selection", "--title="+prompt)
		} else if isTerminal() {
			gui = false
			fmt.Print(prompt + ": ")
			s, _ := bufio.NewReader(os.Stdin).ReadString('\n')
			return strings.Trim(strings.TrimSpace(s), `"'`), nil
		} else {
			return "", fmt.Errorf("no file picker: install zenity, or run from a terminal with the .iso path")
		}
	}
	b, err := cmd.Output()
	return strings.TrimSpace(string(b)), err
}

func dialog(msg string, isErr bool) {
	fmt.Println(msg)
	if pause {
		fmt.Print("\nPress Enter to close.")
		fmt.Scanln()
		return
	}
	if !gui {
		return
	}
	switch runtime.GOOS {
	case "darwin":
		icon := "note"
		if isErr {
			icon = "stop"
		}
		q := strings.ReplaceAll(strings.ReplaceAll(msg, `\`, `\\`), `"`, `\"`)
		exec.Command("osascript", "-e", `display dialog "`+q+`" with title "MvC2 EBOOT Builder" buttons {"OK"} default button 1 with icon `+icon).Run()
	default:
		kind := "--info"
		if isErr {
			kind = "--error"
		}
		if exec.Command("zenity", kind, "--title=MvC2 EBOOT Builder", "--text="+msg).Run() != nil {
			fmt.Print("\nPress Enter to close.")
			fmt.Scanln()
		}
	}
}

func fail(msg string) {
	dialog("Error: "+msg, true)
	os.Exit(1)
}

func done(msg string) { dialog(msg, false) }
