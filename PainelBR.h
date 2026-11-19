/*
 * VanitySearch-Bitcrack-BR - painel ao vivo em portugues (PT-BR)
 *
 * Modulo de apresentacao independente do motor de busca: nao conhece Int,
 * Point nem GPUEngine, so recebe numeros ja calculados. Visual e telemetria
 * portados do BitCrack-BR (Tarcisio Soria / KKND).
 *
 * Este programa e software livre: voce pode redistribui-lo e/ou modifica-lo
 * sob os termos da GNU Affero General Public License v3 (ou posterior).
 */

#ifndef PAINELBR_H
#define PAINELBR_H

#include <stdint.h>
#include <string>

namespace PainelBR {

	// Estado de uma atualizacao do painel de busca.
	struct Status {
		double      speedMKs;     // velocidade instantanea (MKey/s)
		uint64_t    keysDone;     // chaves varridas ate agora
		double      taskSize;     // tamanho total do intervalo (chaves), 0 se desconhecido
		double      runSec;       // tempo de execucao (s)
		int         nFound;       // chaves encontradas
		int         nTargets;     // quantidade de alvos
		bool        paused;       // busca pausada pelo usuario
		bool        randomMode;   // modo aleatorio (sem barra/ETA)
		std::string curKeyHex;    // chave atual, 64 digitos hex ("" se N/A)
		double      curKeyApprox; // chave atual como double (para a posicao em 2^256)
	};

	// Ativa sequencias ANSI no console (necessario no Windows) e liga o NVML.
	void init(int gpuIndex);
	void shutdown();

	// Forca o proximo render a desenhar um painel novo (abaixo do texto atual).
	// Chamar antes de imprimir qualquer coisa "no meio" do painel.
	void breakPanel();

	// Fase de preparo.
	void printHeader(const std::string &device, int nbThread, int stepSize,
	                 int rangeBits, const std::string &startHex, const std::string &endHex,
	                 int nTargets, bool randomMode);
	void prepProgress(double pct);
	void prepDone(double seconds);

	// Fase de busca.
	void render(const Status &s);
	void finished(double avgSpeedMKs, int nFound);

	// Bloco de "chave encontrada" no mesmo estilo do BitCrack-BR.
	// savedFile vazio = nao ha arquivo de saida.
	void found(const std::string &address, const std::string &privHex64, const std::string &wif,
	           const std::string &pubKey, bool compressed, const std::string &savedFile);

	// Auxiliares de formatacao (expostos para reuso).
	std::string thousands(uint64_t v);
	std::string hms(double sec);

}

#endif // PAINELBR_H
