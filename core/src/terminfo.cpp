#include "termcore/terminfo.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if !defined(_WIN32)
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace termcore {

// Embedded terminfo source as a raw string literal
static const char kTerminfoSource[] = R"TI(
xterm-breadterminal|BreadTerminal terminal emulator,
	am,
	bce,
	km,
	mir,
	msgr,
	xenl,
	colors#256,
	cols#80,
	it#8,
	lines#24,
	pairs#32767,
	cup=\E[%i%p1%d;%p2%dH,
	cuu=\E[%p1%dA,
	cud=\E[%p1%dB,
	cuf=\E[%p1%dC,
	cub=\E[%p1%dD,
	home=\E[H,
	cuu1=\E[A,
	cud1=\n,
	cuf1=\E[C,
	cub1=\b,
	cr=\r,
	ed=\E[J,
	el=\E[K,
	el1=\E[1K,
	ech=\E[%p1%dX,
	clear=\E[H\E[2J,
	dch=\E[%p1%dP,
	dch1=\E[P,
	ich=\E[%p1%d@,
	csr=\E[%i%p1%d;%p2%dr,
	indn=\E[%p1%dS,
	rin=\E[%p1%dT,
	ind=\n,
	ri=\EM,
	il=\E[%p1%dL,
	il1=\E[L,
	dl=\E[%p1%dM,
	dl1=\E[M,
	Ss=\E[%p1%d q,
	Se=\E[2 q,
	civis=\E[?25l,
	cnorm=\E[?12l\E[?25h,
	cvvis=\E[?12;25h,
	smkx=\E[?1h\E=,
	rmkx=\E[?1l\E>,
	smcup=\E[?1049h\E[22;0;0t,
	rmcup=\E[?1049l\E[23;0;0t,
	smam=\E[?7h,
	rmam=\E[?7l,
	bold=\E[1m,
	dim=\E[2m,
	sitm=\E[3m,
	ritm=\E[23m,
	smul=\E[4m,
	rmul=\E[24m,
	blink=\E[5m,
	rev=\E[7m,
	invis=\E[8m,
	smxx=\E[9m,
	rmxx=\E[29m,
	sgr0=\E[m,
	sgr=\E[0%?%p6%t;1%;%?%p2%t;4%;%?%p1%p3%|%t;7%;%?%p4%t;5%;%?%p5%t;2%;m%?%p9%t\016%e\017%;,
	smso=\E[7m,
	rmso=\E[27m,
	smacs=\E(0,
	rmacs=\E(B,
	setaf=\E[%?%p1%{8}%<%t3%p1%d%e%p1%{16}%<%t9%p1%{8}%-%d%e38;5;%p1%d%;m,
	setab=\E[%?%p1%{8}%<%t4%p1%d%e%p1%{16}%<%t10%p1%{8}%-%d%e48;5;%p1%d%;m,
	op=\E[39;49m,
	ht=\t,
	hts=\EH,
	tbc=\E[3g,
	cbt=\E[Z,
	bel=^G,
	flash=\E[?5h$<100/>\E[?5l,
	kf1=\EOP,
	kf2=\EOQ,
	kf3=\EOR,
	kf4=\EOS,
	kf5=\E[15~,
	kf6=\E[17~,
	kf7=\E[18~,
	kf8=\E[19~,
	kf9=\E[20~,
	kf10=\E[21~,
	kf11=\E[23~,
	kf12=\E[24~,
	kf13=\E[1;2P,
	kf14=\E[1;2Q,
	kf15=\E[1;2R,
	kf16=\E[1;2S,
	kf17=\E[15;2~,
	kf18=\E[17;2~,
	kf19=\E[18;2~,
	kf20=\E[19;2~,
	kf21=\E[20;2~,
	kf22=\E[21;2~,
	kf23=\E[23;2~,
	kf24=\E[24;2~,
	kcuu1=\EOA,
	kcud1=\EOB,
	kcuf1=\EOC,
	kcub1=\EOD,
	khome=\EOH,
	kend=\EOF,
	kpp=\E[5~,
	knp=\E[6~,
	kich1=\E[2~,
	kdch1=\E[3~,
	kbs=\177,
	kmous=\E[M,
	XM=\E[?1006;1000%?%p1%{1}%=%th%el%;,
	BE=\E[?2004h,
	BD=\E[?2004l,
	PS=\E[200~,
	PE=\E[201~,
	Tc,
	RGB,
	Sync=\E[?2026%?%p1%{1}%=%th%el%;,
	fullkbd,
	Sixel,
)TI";

const char* breadTerminalTermName() {
    return "xterm-breadterminal";
}

const char* breadTerminalTerminfoSource() {
    return kTerminfoSource;
}

#if defined(_WIN32)

// Terminfo is not used on Windows (ConPTY handles terminal capabilities)
TerminfoInstallResult installTerminfo() {
    TerminfoInstallResult result;
    result.term_name = "xterm-256color";
    result.success = true;
    return result;
}

#else // Unix

/// Get the platform-specific terminfo installation directory
static std::string getTerminfoDir() {
    const char* home = getenv("HOME");
    if (!home || home[0] == '\0') {
        return {};
    }

#if defined(__APPLE__)
    return std::string(home) + "/Library/Application Support/BreadTerminal/terminfo";
#else
    return std::string(home) + "/.local/share/breadterminal/terminfo";
#endif
}

/// Recursively create directories (like mkdir -p)
static bool mkdirRecursive(const std::string& path) {
    if (path.empty()) return false;

    // Check if already exists
    struct stat st{};
    if (stat(path.c_str(), &st) == 0) {
        return S_ISDIR(st.st_mode);
    }

    // Create parent first
    auto pos = path.rfind('/');
    if (pos != std::string::npos && pos > 0) {
        if (!mkdirRecursive(path.substr(0, pos))) {
            return false;
        }
    }

    return mkdir(path.c_str(), 0755) == 0 || errno == EEXIST;
}

/// Check if a compiled terminfo entry already exists for xterm-breadterminal
static bool compiledEntryExists(const std::string& terminfo_dir) {
    // terminfo entries are stored under a subdirectory named by first letter
    // e.g., terminfo_dir/x/xterm-breadterminal
    std::string entry_path = terminfo_dir + "/x/xterm-breadterminal";
    struct stat st{};
    return stat(entry_path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

TerminfoInstallResult installTerminfo() {
    TerminfoInstallResult result;
    result.term_name = "xterm-breadterminal";

    std::string terminfo_dir = getTerminfoDir();
    if (terminfo_dir.empty()) {
        result.term_name = "xterm-256color";
        result.error = "Could not determine HOME directory";
        return result;
    }

    result.terminfo_dir = terminfo_dir;

    // If already compiled, just return success
    if (compiledEntryExists(terminfo_dir)) {
        result.success = true;
        return result;
    }

    // Create the terminfo directory
    if (!mkdirRecursive(terminfo_dir)) {
        result.term_name = "xterm-256color";
        result.error = "Failed to create terminfo directory: " + terminfo_dir;
        return result;
    }

    // Write the terminfo source to a temporary file
    std::string tmp_path = terminfo_dir + "/xterm-breadterminal.ti.tmp";
    FILE* fp = fopen(tmp_path.c_str(), "w");
    if (!fp) {
        result.term_name = "xterm-256color";
        result.error = "Failed to write temporary terminfo source file";
        return result;
    }
    fputs(kTerminfoSource, fp);
    fclose(fp);

    // Compile with tic
    std::string cmd = "tic -x -o \"" + terminfo_dir + "\" \"" + tmp_path + "\" 2>&1";
    FILE* proc = popen(cmd.c_str(), "r");
    if (!proc) {
        unlink(tmp_path.c_str());
        result.term_name = "xterm-256color";
        result.error = "Failed to run tic command";
        return result;
    }

    // Read any error output
    char buf[256];
    std::string tic_output;
    while (fgets(buf, sizeof(buf), proc) != nullptr) {
        tic_output += buf;
    }

    int status = pclose(proc);

    // Clean up temp file
    unlink(tmp_path.c_str());

    if (status != 0) {
        result.term_name = "xterm-256color";
        result.error = "tic failed: " + tic_output;
        return result;
    }

    // Verify the compiled entry exists
    if (!compiledEntryExists(terminfo_dir)) {
        result.term_name = "xterm-256color";
        result.error = "tic succeeded but compiled entry not found";
        return result;
    }

    result.success = true;
    return result;
}

#endif // _WIN32

} // namespace termcore
