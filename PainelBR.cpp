/*
 * VanitySearch-Bitcrack-BR - painel ao vivo em portugues (PT-BR)
 * Layout alinhado ao do BitCrack-BR. Veja PainelBR.h.
 * Licenca: GNU AGPL v3 (ou posterior).
 */

#include "PainelBR.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#if defined(_WIN32) || defined(_WIN64)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#ifdef HAVE_NVML
#include <nvml.h>
#endif

namespace PainelBR {

// ---- Cores ANSI -----------------------------------------------------------
static const char *RESET  = "\033[0m";
static const char *BOLD   = "\033[1m";
static const char *DIM    = "\033[2m";
static const char *RED    = "\033[31m";
static const char *GREEN  = "\033[32m";
static const char *YELLOW = "\033[33m";
static const char *CYAN   = "\033[36m";

// ---- Reguas (UTF-8) -------------------------------------------------------
static std::string repeat(const char *s, int n)
{
	std::string r;
	for(int i = 0; i < n; i++) { r += s; }
	return r;
}
static const std::string RULE       = repeat("\xe2\x94\x80", 40);  // 40x ─
static const std::string RULE_THICK = repeat("\xe2\x95\x90", 14);  // 14x ═

static const char *PRODUCT = "VANITYSEARCH BR";

// Numero de linhas do painel de busca (deve bater com o que render() imprime).
static const int PANEL_LINES = 11;
static bool g_panelDrawn = false;

// Estado guardado pela fase de preparo.
static std::string g_gpuName;
static int g_nTargets = 0;

// ---- Formatacao (mesmo padrao do BitCrack-BR: milhar ',' e decimal '.') ----
std::string thousands(uint64_t v)
{
	char buf[32];
	snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
	std::string s(buf), out;
	int n = (int)s.size();
	for(int i = 0; i < n; i++) {
		out += s[i];
		int rest = n - 1 - i;
		if(rest > 0 && rest % 3 == 0) { out += ','; }
	}
	return out;
}

std::string hms(double sec)
{
	if(!(sec >= 0.0) || sec > 99.0 * 86400.0 * 365.0) { return "--:--:--"; }
	uint64_t t = (uint64_t)sec;
	char buf[48];
	if(t >= 86400) {
		snprintf(buf, sizeof(buf), "%llud %02llu:%02llu:%02llu",
			(unsigned long long)(t / 86400), (unsigned long long)((t % 86400) / 3600),
			(unsigned long long)((t % 3600) / 60), (unsigned long long)(t % 60));
	} else {
		snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu",
			(unsigned long long)(t / 3600), (unsigned long long)((t % 3600) / 60), (unsigned long long)(t % 60));
	}
	return buf;
}

// Posicao da chave no espaco total (0 -> 2^256) em %, em notacao cientifica
// quando minuscula. Virgula decimal, igual ao BitCrack-BR.
static std::string posPercent(double key)
{
	double pct = ldexp(key, -256) * 100.0;
	char buf[64];
	if(pct <= 0.0)            { return "0 %"; }
	else if(pct >= 0.0001)    { snprintf(buf, sizeof(buf), "%.6f %%", pct); }
	else                      { snprintf(buf, sizeof(buf), "%.3e %%", pct); }
	std::string s(buf);
	for(size_t i = 0; i < s.size(); i++) { if(s[i] == '.') s[i] = ','; }
	return s;
}

static std::string fmt1(const char *f, double v)
{
	char buf[64];
	snprintf(buf, sizeof(buf), f, v);
	return buf;
}

// "GPU #0 NVIDIA GeForce RTX 3060 (30x128 cores) Grid(2048x256)" -> "NVIDIA GeForce RTX 3060"
static std::string cleanName(const std::string &raw)
{
	std::string s = raw;
	if(s.compare(0, 5, "GPU #") == 0) {
		size_t sp = s.find(' ', 5);
		if(sp != std::string::npos) { s = s.substr(sp + 1); }
	}
	size_t par = s.find(" (");
	if(par != std::string::npos) { s = s.substr(0, par); }
	return s;
}

// ---- Telemetria NVML ------------------------------------------------------
#ifdef HAVE_NVML
static nvmlDevice_t g_dev;
static bool g_ok = false;
static unsigned int g_throttle = 87;

static void telInit(unsigned int idx)
{
	if(nvmlInit() != NVML_SUCCESS) { return; }
	if(nvmlDeviceGetHandleByIndex(idx, &g_dev) != NVML_SUCCESS) { nvmlShutdown(); return; }
	g_ok = true;

	unsigned int thr = 0;
	if(nvmlDeviceGetTemperatureThreshold(g_dev, NVML_TEMPERATURE_THRESHOLD_ACOUSTIC_CURR, &thr) == NVML_SUCCESS && thr >= 50 && thr <= 110) {
		g_throttle = thr;
	} else if(nvmlDeviceGetTemperatureThreshold(g_dev, NVML_TEMPERATURE_THRESHOLD_SLOWDOWN, &thr) == NVML_SUCCESS && thr > 15) {
		g_throttle = thr - 15;
	}
}
static void telShutdown() { if(g_ok) { nvmlShutdown(); g_ok = false; } }

// " │ 58°C 1905 MHz Fan 42%" colorido pelo throttle; "" se indisponivel.
static std::string telStr(const std::string &pipe)
{
	if(!g_ok) { return ""; }
	unsigned int temp = 0, clk = 0, f = 0;
	if(nvmlDeviceGetTemperature(g_dev, NVML_TEMPERATURE_GPU, &temp) != NVML_SUCCESS) { return ""; }
	if(nvmlDeviceGetClockInfo(g_dev, NVML_CLOCK_SM, &clk) != NVML_SUCCESS) { clk = 0; }
	int fan = (nvmlDeviceGetFanSpeed(g_dev, &f) == NVML_SUCCESS) ? (int)f : -1;

	const char *col = GREEN;
	if(temp >= g_throttle)          { col = RED; }
	else if(temp + 7 >= g_throttle) { col = YELLOW; }

	char buf[96];
	snprintf(buf, sizeof(buf), "%u\xc2\xb0" "C", temp);
	std::string s = pipe + col + buf + RESET;
	if(clk > 0) { snprintf(buf, sizeof(buf), " %u MHz", clk); s += buf; }
	if(fan >= 0) { snprintf(buf, sizeof(buf), " Fan %d%%", fan); s += std::string(DIM) + buf + RESET; }
	return s;
}

static std::string vramStr(const std::string &pipe)
{
	if(!g_ok) { return ""; }
	nvmlMemory_t m;
	if(nvmlDeviceGetMemoryInfo(g_dev, &m) != NVML_SUCCESS) { return ""; }
	char buf[64];
	snprintf(buf, sizeof(buf), "VRAM %llu/%llu MB", (unsigned long long)(m.used >> 20), (unsigned long long)(m.total >> 20));
	return pipe + CYAN + buf + RESET;
}
#else
static void telInit(unsigned int) {}
static void telShutdown() {}
static std::string telStr(const std::string &) { return ""; }
static std::string vramStr(const std::string &) { return ""; }
#endif

// ---- API ------------------------------------------------------------------
void init(int gpuIndex)
{
#if defined(_WIN32) || defined(_WIN64)
	HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD mode = 0;
	if(h != INVALID_HANDLE_VALUE && GetConsoleMode(h, &mode)) {
		SetConsoleMode(h, mode | 0x0004);  // ENABLE_VIRTUAL_TERMINAL_PROCESSING
		SetConsoleOutputCP(65001);         // UTF-8
	}
#endif
	telInit((unsigned int)gpuIndex);
}

void shutdown() { telShutdown(); }

void breakPanel() { g_panelDrawn = false; }

static std::string pipeStr() { return std::string("  ") + DIM + "\xe2\x94\x82" + RESET + "  "; }  // " │ "
static std::string sepStr()  { return std::string("   ") + DIM + "\xc2\xb7" + RESET + "   "; }     // " · "

static std::string title(const std::string &t)
{
	return std::string(DIM) + RULE_THICK + RESET + " " + BOLD + YELLOW + PRODUCT + " \xc2\xb7 " + t + RESET
	     + " " + DIM + RULE_THICK + RESET;
}

void printHeader(const std::string &device, int nbThread, int stepSize,
                 int rangeBits, const std::string &startHex, const std::string &endHex,
                 int nTargets, bool randomMode)
{
	std::string pipe = pipeStr(), sep = sepStr();
	g_gpuName = cleanName(device);
	g_nTargets = nTargets;

	uint64_t parallel = (uint64_t)nbThread * (uint64_t)stepSize;

	std::string o;
	o += "\n" + title("PREPARANDO BUSCA") + "\n";
	o += std::string("  ") + BOLD + CYAN + g_gpuName + RESET + telStr(pipe) + pipe + "compress.: comprimida" + "\n";
	o += "  Config:  " + thousands((uint64_t)nbThread) + " threads " + DIM + "\xc3\x97" + RESET + " "
	   + thousands((uint64_t)stepSize) + " chaves/passo  =  " + BOLD + thousands(parallel) + RESET + " chaves por lan\xc3\xa7" "amento\n";
	o += std::string(DIM) + RULE + RESET + "\n";
	o += "  In\xc3\xad" "cio: " + startHex + "\n";
	o += "  Fim:    " + endHex + "\n";
	o += std::string("  Passo:  1  ") + DIM + "(sequencial)" + RESET + sep + "intervalo: " + std::to_string(rangeBits) + " bits"
	   + (randomMode ? std::string(sep) + YELLOW + "modo aleat\xc3\xb3rio" + RESET : std::string("")) + "\n";
	o += std::string(DIM) + RULE + RESET + "\n";

	// Pontos iniciais: x e y de 32 bytes cada por thread.
	double mb = (double)nbThread * 64.0 / (1024.0 * 1024.0);
	o += std::string("  \xe2\x9a\x99 Gerando pontos iniciais na GPU") + sep + thousands((uint64_t)nbThread) + " pontos (" + fmt1("%.1f", mb) + " MB)\n";
	fputs(o.c_str(), stdout);
	fflush(stdout);
	g_panelDrawn = false;
}

void prepProgress(double pct)
{
	if(pct < 0.0) pct = 0.0;
	if(pct > 100.0) pct = 100.0;
	const int BAR = 40;
	int filled = (int)(pct / 100.0 * BAR);
	std::string bar;
	for(int i = 0; i < BAR; i++) { bar += (i < filled ? "\xe2\x96\x88" : "\xc2\xb7"); }  // █ / ·
	printf("\r\033[K  [%s%s%s] %.0f%%", GREEN, bar.c_str(), RESET, pct);
	fflush(stdout);
}

void prepDone(double seconds)
{
	printf("\r\033[K  [%s%s%s] 100%%\n", GREEN, repeat("\xe2\x96\x88", 40).c_str(), RESET);
	printf("  %s\xe2\x9c\x94%s Pontos iniciais gerados em %.2f s\n", GREEN, RESET, seconds);
	printf("  %s\xe2\x9c\x94%s Alvos: %s endere\xc3\xa7os\n", GREEN, RESET, thousands((uint64_t)g_nTargets).c_str());
	fflush(stdout);
}

void render(const Status &s)
{
	std::string pipe = pipeStr(), sep = sepStr();

	// Barra de progresso / ETA (so no modo sequencial com intervalo conhecido).
	std::string prog;
	if(s.randomMode) {
		prog = std::string("  ") + YELLOW + "\xe2\x9a\xa0 MODO ALEAT\xc3\x93RIO" + RESET
		     + DIM + " \xe2\x80\x94 sem barra \xc2\xb7 ETA indispon\xc3\xad" "vel" + RESET;
	} else if(s.taskSize > 0.0) {
		double pct = (double)s.keysDone * 100.0 / s.taskSize;
		if(pct > 100.0) pct = 100.0;
		const int BAR = 40;
		int filled = (int)(pct / 100.0 * BAR);
		std::string bar;
		for(int i = 0; i < BAR; i++) { bar += (i < filled ? "\xe2\x96\x88" : "\xc2\xb7"); }
		std::string eta = "--:--:--";
		double avg = (s.runSec > 0.0) ? (double)s.keysDone / s.runSec : 0.0;  // chaves/s
		if(avg > 1.0) { eta = hms((s.taskSize - (double)s.keysDone) / avg); }
		prog = std::string("  Progresso: [") + GREEN + bar + RESET + "] " + BOLD + fmt1("%.2f", pct) + "%" + RESET
		     + "   " + DIM + "ETA " + eta + RESET;
	} else {
		prog = std::string("  ") + YELLOW + "\xe2\x9a\xa0 INTERVALO DESCONHECIDO" + RESET;
	}

	std::string speed;
	if(s.paused)                  { speed = std::string(YELLOW) + "PAUSADO" + RESET; }
	else if(s.speedMKs < 0.01)    { speed = std::string(BOLD) + GREEN + "< 0.01 MKey/s" + RESET; }
	else                          { speed = std::string(BOLD) + GREEN + fmt1("%.2f", s.speedMKs) + " MKey/s" + RESET; }

	std::string curKey = s.curKeyHex.empty() ? std::string(DIM) + "(indispon\xc3\xad" "vel no modo aleat\xc3\xb3rio)" + RESET
	                                         : std::string(BOLD) + GREEN + s.curKeyHex + RESET;
	std::string pos = s.curKeyHex.empty() ? std::string(DIM) + "\xe2\x80\x94" + RESET
	                                      : std::string(BOLD) + YELLOW + posPercent(s.curKeyApprox) + RESET;

	std::string foundStr = (s.nFound > 0)
		? std::string(BOLD) + GREEN + std::to_string(s.nFound) + " ENCONTRADA(S)" + RESET
		: std::string("0");

	std::string p;
	if(g_panelDrawn) { p += "\033[" + std::to_string(PANEL_LINES) + "A"; }
	g_panelDrawn = true;

	auto L = [](const std::string &x) { return "\r\033[K" + x + "\n"; };

	p += L(title(s.paused ? "BUSCA PAUSADA" : "BUSCA EM ANDAMENTO"));
	p += L(std::string("  ") + BOLD + CYAN + g_gpuName + RESET + vramStr(pipe) + telStr(pipe));
	p += L(std::string(DIM) + RULE + RESET);
	p += L(std::string("  Alvos: ") + BOLD + thousands((uint64_t)s.nTargets) + RESET + " endere\xc3\xa7o(s)" + sep + speed
	       + sep + DIM + "varridas: " + RESET + thousands(s.keysDone) + " chaves");
	p += L(prog);
	p += L(std::string(DIM) + RULE + RESET);
	p += L("  Chave atual (segmento 0):");
	p += L(std::string("  ") + curKey);
	p += L(std::string("  Posi\xc3\xa7\xc3\xa3o no total (0 \xe2\x86\x92 2\xc2\xb2\xe2\x81\xb5\xe2\x81\xb6):  ") + pos);
	p += L(std::string(DIM) + RULE + RESET);
	p += L(std::string(DIM) + "  decorrido " + hms(s.runSec) + "   \xc2\xb7   encontradas: " + RESET + foundStr
	       + DIM + "   \xc2\xb7   [p] pausa/retoma   \xc2\xb7   Ctrl+C para parar" + RESET);

	fputs(p.c_str(), stdout);
	fflush(stdout);
}

void finished(double avgSpeedMKs, int nFound)
{
	printf("\n%s%sINTERVALO CONCLU\xc3\x8d" "DO%s \xe2\x80\x94 velocidade m\xc3\xa9" "dia: %s%.2f MKey/s%s \xc2\xb7 encontradas: %d\n",
		BOLD, GREEN, RESET, BOLD, avgSpeedMKs, RESET, nFound);
	fflush(stdout);
	g_panelDrawn = false;
}

}  // namespace PainelBR
