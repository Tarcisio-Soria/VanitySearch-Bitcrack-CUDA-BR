/*
 * This file is part of the VanitySearch distribution (https://github.com/JeanLucPons/VanitySearch).
 * Copyright (c) 2019 Jean Luc PONS.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 3.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Vanity.h"
#include "PainelBR.h"
#include "Base58.h"
#include "Bech32.h"
#include "hash/sha256.h"
#include "hash/sha512.h"
#include "IntGroup.h"
#include "Wildcard.h"
#include "Timer.h"
#include "hash/ripemd160.h"
#include <string.h>
#include <math.h>
#include <algorithm>
#include <thread>
#include <atomic>
#include <ctime> 
#include <iostream>
#include <fstream>



//#define GRP_SIZE 256

using namespace std;

//Point Gn[GRP_SIZE / 2];
//Point _2Gn;

VanitySearch::VanitySearch(Secp256K1* secp, vector<std::string>& inputAddresses, int searchMode,
	bool stop, string outputFile, uint32_t maxFound, BITCRACK_PARAM* bc):inputAddresses(inputAddresses) 
{
	this->secp = secp;
	this->searchMode = searchMode;
	this->stopWhenFound = stop;
	this->outputFile = outputFile;
	this->numGPUs = 0;
	this->maxFound = maxFound;	
	this->searchType = -1;
	this->bc = bc;	

	rseed(static_cast<unsigned long>(time(NULL)));
	
	addresses.clear();

	// Create a 65536 items lookup table
	ADDRESS_TABLE_ITEM t;
	t.found = true;
	t.items = NULL;
	for (int i = 0; i < 65536; i++)
		addresses.push_back(t);
	
	// Insert addresses
	bool loadingProgress = (inputAddresses.size() > 1000);
	if (loadingProgress)
		PainelBR::progress("Montando tabela de busca (1/2)", 0.0);

	nbAddress = 0;
	onlyFull = true;

	for (int i = 0; i < (int)inputAddresses.size(); i++) 
	{
		ADDRESS_ITEM it;
		std::vector<ADDRESS_ITEM> itAddresses;

		if (initAddress(inputAddresses[i], &it)) {
			bool* found = new bool;
			*found = false;
			it.found = found;
			itAddresses.push_back(it);
		}

		if (itAddresses.size() > 0) 
		{
			// Add the item to all correspoding addresses in the lookup table
			for (int j = 0; j < (int)itAddresses.size(); j++) 
			{
				address_t p = itAddresses[j].sAddress;

				if (addresses[p].items == NULL) {
					addresses[p].items = new vector<ADDRESS_ITEM>();
					addresses[p].found = false;
					usedAddress.push_back(p);
				}
				(*addresses[p].items).push_back(itAddresses[j]);
			}
			onlyFull &= it.isFull;
			nbAddress++;
		}

		if (loadingProgress && i % 1000 == 0)
			PainelBR::progress("Montando tabela de busca (1/2)", (((double)i) / (double)(inputAddresses.size() - 1)) * 100.0);
	}

	if (loadingProgress)
		PainelBR::progressEnd();

	if (nbAddress == 0) 
	{
		fprintf(stderr, "[ERROR] VanitySearch: nothing to search !\n");
		exit(-1);
	}

	// Second level lookup
	uint32_t unique_sAddress = 0;
	uint32_t minI = 0xFFFFFFFF;
	uint32_t maxI = 0;
	for (int i = 0; i < (int)addresses.size(); i++) 
	{
		
		if (addresses[i].items) 
		{
			
			LADDRESS lit;
			lit.sAddress = i;
			if (addresses[i].items) 
			{
				for (int j = 0; j < (int)addresses[i].items->size(); j++) 
				{
					lit.lAddresses.push_back((*addresses[i].items)[j].lAddress);
					
				}
			}

			sort(lit.lAddresses.begin(), lit.lAddresses.end());
			usedAddressL.push_back(lit);
			if ((uint32_t)lit.lAddresses.size() > maxI) maxI = (uint32_t)lit.lAddresses.size();
			if ((uint32_t)lit.lAddresses.size() < minI) minI = (uint32_t)lit.lAddresses.size();
			unique_sAddress++;
		}

		if (loadingProgress)
			PainelBR::progress("Montando tabela de busca (2/2)", ((double)i * 100.0) / (double)addresses.size());
	}

	if (loadingProgress)
		PainelBR::progressEnd();
	
	// Os alvos sao resumidos no painel de preparo ("Alvos: N endereco(s)").

	//// Compute Generator table G[n] = (n+1)*G
	//Point g = secp->G;
	//Gn[0] = g;
	//g = secp->DoubleDirect(g);
	//Gn[1] = g;
	//for (int i = 2; i < GRP_SIZE / 2; i++) {
	//	g = secp->AddDirect(g, secp->G);
	//	Gn[i] = g;
	//}
	//// _2Gn = CPU_GRP_SIZE*G
	//_2Gn = secp->DoubleDirect(Gn[GRP_SIZE / 2 - 1]);

	// Constant for endomorphism
	// if a is a nth primitive root of unity, a^-1 is also a nth primitive root.
	// beta^3 = 1 mod p implies also beta^2 = beta^-1 mop (by multiplying both side by beta^-1)
	// (beta^3 = 1 mod p),  beta2 = beta^-1 = beta^2
	// (lambda^3 = 1 mod n), lamba2 = lamba^-1 = lamba^2
	beta.SetBase16("7ae96a2b657c07106e64479eac3434e99cf0497512f58995c1396c28719501ee");
	lambda.SetBase16("5363ad4cc05c30e0a5261c028812645a122e22ea20816678df02967c1b23bd72");
	beta2.SetBase16("851695d49a83f8ef919bb86153cbcb16630fb68aed0a766a3ec693d68e6afa40");
	lambda2.SetBase16("ac9c52b33fa3cf1f5ad9e3fd77ed9ba4a880b9fc8ec739c2e0cfc810b51283ce");

	startKey.Set(&bc->ksNext);	

	fflush(stdout);
}

bool VanitySearch::isSingularAddress(std::string pref) {

	// check is the given address contains only 1
	bool only1 = true;
	int i = 0;
	while (only1 && i < (int)pref.length()) {
		only1 = pref.data()[i] == '1';
		i++;
	}
	return only1;
}

bool VanitySearch::initAddress(std::string& address, ADDRESS_ITEM* it) {

	std::vector<unsigned char> result;
	string dummy1 = address;
	int nbDigit = 0;
	bool wrong = false;

	if (address.length() < 2) {
		fprintf(stdout, "Ignorando endereco \"%s\" (muito curto)\n", address.c_str());
		return false;
	}

	int aType = -1;

	switch (address.data()[0]) {
	case '1':
		aType = P2PKH;
		break;
	case '3':
		aType = P2SH;
		break;
	case 'b':
	case 'B':
		std::transform(address.begin(), address.end(), address.begin(), ::tolower);
		if (strncmp(address.c_str(), "bc1q", 4) == 0)
			aType = BECH32;
		break;
	}

	if (aType == -1) {
		fprintf(stdout, "Ignorando endereco \"%s\" (deve comecar com 1, 3 ou bc1q)\n", address.c_str());
		return false;
	}

	if (searchType == -1) searchType = aType;
	if (aType != searchType) {
		fprintf(stdout, "Ignorando endereco \"%s\" (nao misture P2PKH, P2SH e BECH32)\n", address.c_str());
		return false;
	}

	if (aType == BECH32) {

		// BECH32
		uint8_t witprog[40];
		size_t witprog_len;
		int witver;
		const char* hrp = "bc";

		int ret = segwit_addr_decode(&witver, witprog, &witprog_len, hrp, address.c_str());

		// Try to attack a full address ?
		if (ret && witprog_len == 20) {
						
			it->isFull = true;
			memcpy(it->hash160, witprog, 20);
			it->sAddress = *(address_t*)(it->hash160);
			it->lAddress = *(addressl_t*)(it->hash160);
			it->address = (char*)address.c_str();
			it->addressLength = (int)address.length();
			return true;

		}

		if (address.length() < 5) {
			fprintf(stdout, "Ignorando endereco \"%s\" (too short, length<5 )\n", address.c_str());
			return false;
		}

		if (address.length() >= 36) {
			fprintf(stdout, "Ignorando endereco \"%s\" (too long, length>36 )\n", address.c_str());
			return false;
		}

		uint8_t data[64];
		memset(data, 0, 64);
		size_t data_length;
		if (!bech32_decode_nocheck(data, &data_length, address.c_str() + 4)) {
			fprintf(stdout, "Ignorando endereco \"%s\" (Only \"023456789acdefghjklmnpqrstuvwxyz\" allowed)\n", address.c_str());
			return false;
		}
		
		it->sAddress = *(address_t*)data;		
		it->isFull = false;
		it->lAddress = 0;
		it->address = (char*)address.c_str();
		it->addressLength = (int)address.length();

		return true;
	}
	else {

		// P2PKH/P2SH
		wrong = !DecodeBase58(address, result);

		if (wrong) {
			fprintf(stdout, "Ignorando endereco \"%s\" (0, I, O and l not allowed)\n", address.c_str());
			return false;
		}

		// Try to attack a full address ?
		if (result.size() > 21) {
			
			it->isFull = true;
			memcpy(it->hash160, result.data() + 1, 20);
			it->sAddress = *(address_t*)(it->hash160);
			it->lAddress = *(addressl_t*)(it->hash160);
			it->address = (char*)address.c_str();
			it->addressLength = (int)address.length();
			return true;
		}

		// Address containing only '1'
		if (isSingularAddress(address)) {

			if (address.length() > 21) {
				fprintf(stdout, "Ignorando endereco \"%s\" (Too much 1)\n", address.c_str());
				return false;
			}
			
			it->isFull = false;
			it->sAddress = 0;
			it->lAddress = 0;
			it->address = (char*)address.c_str();
			it->addressLength = (int)address.length();
			return true;
		}

		// Search for highest hash160 16bit address (most probable)
		while (result.size() < 25) {
			DecodeBase58(dummy1, result);
			if (result.size() < 25) {
				dummy1.append("1");
				nbDigit++;
			}
		}

		if (searchType == P2SH) {
			if (result.data()[0] != 5) {
				fprintf(stdout, "Ignorando endereco \"%s\" (Unreachable, 31h1 to 3R2c only)\n", address.c_str());
				return false;
			}
		}

		if (result.size() != 25) {
			fprintf(stdout, "Ignorando endereco \"%s\" (tamanho invalido)\n", address.c_str());
			return false;
		}

		it->sAddress = *(address_t*)(result.data() + 1);

		dummy1.append("1");
		DecodeBase58(dummy1, result);

		if (result.size() == 25) {
			it->sAddress = *(address_t*)(result.data() + 1);
			nbDigit++;
		}
		
		it->isFull = false;
		it->lAddress = 0;
		it->address = (char*)address.c_str();
		it->addressLength = (int)address.length();

		return true;
	}
}

void VanitySearch::enumCaseUnsentiveAddress(std::string s, std::vector<std::string>& list) {

	char letter[64];
	int letterpos[64];
	int nbLetter = 0;
	int length = (int)s.length();

	for (int i = 1; i < length; i++) {
		char c = s.data()[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
			letter[nbLetter] = tolower(c);
			letterpos[nbLetter] = i;
			nbLetter++;
		}
	}

	int total = 1 << nbLetter;

	for (int i = 0; i < total; i++) {

		char tmp[64];
		strcpy(tmp, s.c_str());

		for (int j = 0; j < nbLetter; j++) {
			int mask = 1 << j;
			if (mask & i) tmp[letterpos[j]] = toupper(letter[j]);
			else         tmp[letterpos[j]] = letter[j];
		}

		list.push_back(string(tmp));

	}

}

// ----------------------------------------------------------------------------

static std::string padHex64(std::string h) {
	while (h.size() < 64) h = "0" + h;
	return h;
}

void VanitySearch::output(string addr, string pAddr, string pAddrHex, std::string pubKey) {

#ifdef WIN64
	WaitForSingleObject(ghMutex, INFINITE);
#else
	pthread_mutex_lock(&ghMutex);
#endif

	FILE* f = stdout;
	bool needToClose = false;

	if (outputFile.length() > 0) {
		f = fopen(outputFile.c_str(), "a");
		if (f == NULL) {
			fprintf(stderr, "Cannot open %s for writing\n", outputFile.c_str());
			f = stdout;
		}
		else {
			needToClose = true;
		}
	}




	// Arquivo de saida: formato original mantido (compativel com scripts).
	if (f != stdout)
		fprintf(f, "\nPublic Addr: %s\n", addr.c_str());

	std::string wifLabel;
	switch (searchType) {
	case P2PKH:
		wifLabel = "p2pkh:" + pAddr;
		if (f != stdout)
			fprintf(f, "Priv (WIF): p2pkh:%s\n", pAddr.c_str());
		break;
	case P2SH:
		wifLabel = "p2wpkh-p2sh:" + pAddr;
		if (f != stdout)
			fprintf(f, "Priv (WIF): p2wpkh-p2sh:%s\n", pAddr.c_str());
		break;
	case BECH32:
		wifLabel = "p2wpkh:" + pAddr;
		if (f != stdout)
			fprintf(f, "Priv (WIF): p2wpkh:%s\n", pAddr.c_str());
		break;
	}

	if (f != stdout)
		fprintf(f, "Priv (HEX): 0x%s\n", padHex64(pAddrHex).c_str());

	if (f != stdout)
		fflush(f);

	// Tela: mesmo bloco de "chave encontrada" do BitCrack-BR.
	PainelBR::found(addr, padHex64(pAddrHex), wifLabel, pubKey, true,
		(f != stdout) ? outputFile : std::string(""));
	//fflush(stderr);	

	if (needToClose)
		fclose(f);

#ifdef WIN64
	ReleaseMutex(ghMutex);
#else
	pthread_mutex_unlock(&ghMutex);
#endif
}

void VanitySearch::updateFound() {

	// Check if all addresses has been found
	// Needed only if stopWhenFound is asked
	if (stopWhenFound) 	{

		bool allFound = true;
		for (int i = 0; i < (int)usedAddress.size(); i++) {
			bool iFound = true;
			address_t p = usedAddress[i];
			if (!addresses[p].found) {
				if (addresses[p].items) {
					for (int j = 0; j < (int)addresses[p].items->size(); j++) {
						iFound &= *((*addresses[p].items)[j].found);
					}
				}
				addresses[usedAddress[i]].found = iFound;
			}
			allFound &= iFound;
		}

		endOfSearch = allFound;
		if (allFound) allTargetsFound = true;
	}
}

bool VanitySearch::checkPrivKey(string addr, Int& key, int32_t incr, int endomorphism, bool mode) {

	Int k(&key);	

	if (incr < 0) {
		k.Add((uint64_t)(-incr));
		k.Neg();
		k.Add(&secp->order);		
	}
	else {
		k.Add((uint64_t)incr);
	}

	// Endomorphisms
	switch (endomorphism) {
	case 1:
		k.ModMulK1order(&lambda);		
		break;
	case 2:
		k.ModMulK1order(&lambda2);		
		break;
	}

	// Check addresses
	Point p = secp->ComputePublicKey(&k);	

	string chkAddr = secp->GetAddress(searchType, mode, p);
	if (chkAddr != addr) {

		// Key may be the opposite one (negative zero or compressed key)
		k.Neg();
		k.Add(&secp->order);
		p = secp->ComputePublicKey(&k);
		
		string chkAddr = secp->GetAddress(searchType, mode, p);
		if (chkAddr != addr) {
			fprintf(stdout, "\nWarning, wrong private key generated !\n");
			fprintf(stdout, "  Addr :%s\n", addr.c_str());
			fprintf(stdout, "  Check:%s\n", chkAddr.c_str());
			fprintf(stdout, "  Endo:%d incr:%d comp:%d\n", endomorphism, incr, mode);
			return false;
		}

	}

	// A mesma chave pode ser achada varias vezes (principalmente no modo aleatorio,
	// que revisita o intervalo): mostra e conta cada chave so uma vez.
	{
		std::string seenKey = addr + ":" + k.GetBase16();
		if (reportedKeys.count(seenKey)) {
			return false;
		}
		reportedKeys.insert(seenKey);
	}

	output(addr, secp->GetPrivAddress(mode, k), k.GetBase16(), secp->GetPublicKeyHex(mode, p));

	return true;
}

void VanitySearch::checkAddrSSE(uint8_t* h1, uint8_t* h2, uint8_t* h3, uint8_t* h4,
	int32_t incr1, int32_t incr2, int32_t incr3, int32_t incr4,
	Int& key, int endomorphism, bool mode) {

	vector<string> addr = secp->GetAddress(searchType, mode, h1, h2, h3, h4);

	for (int i = 0; i < (int)inputAddresses.size(); i++) {

		if (Wildcard::match(addr[0].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[0], key, incr1, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}

		if (Wildcard::match(addr[1].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[1], key, incr2, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}

		if (Wildcard::match(addr[2].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[2], key, incr3, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}

		if (Wildcard::match(addr[3].c_str(), inputAddresses[i].c_str())) {

			// Found it !      
			if (checkPrivKey(addr[3], key, incr4, endomorphism, mode)) {
				nbFoundKey++;
				//patternFound[i] = true;
				updateFound();
			}
		}
	}
}

void VanitySearch::checkAddr(int prefIdx, uint8_t* hash160, Int& key, int32_t incr, int endomorphism, bool mode) {
	
	vector<ADDRESS_ITEM>* pi = addresses[prefIdx].items;	


	if (onlyFull) {

		// Full addresses
		for (int i = 0; i < (int)pi->size(); i++) {

			if (stopWhenFound && *((*pi)[i].found))
				continue;

			if (ripemd160_comp_hash((*pi)[i].hash160, hash160)) {

				// Found it !
				*((*pi)[i].found) = true;
				// You believe it ?
				if (checkPrivKey(secp->GetAddress(searchType, mode, hash160), key, incr, endomorphism, mode)) {
					nbFoundKey++;
					updateFound();
				}

			}

		}

	}
	else {
		char a[64];

		string addr = secp->GetAddress(searchType, mode, hash160);

		for (int i = 0; i < (int)pi->size(); i++) {

			if (stopWhenFound && *((*pi)[i].found))
				continue;

			strncpy(a, addr.c_str(), (*pi)[i].addressLength);
			a[(*pi)[i].addressLength] = 0;

			if (strcmp((*pi)[i].address, a) == 0) {

				// Found it !
				*((*pi)[i].found) = true;
				if (checkPrivKey(addr, key, incr, endomorphism, mode)) {
					nbFoundKey++;
					updateFound();
				}

			}

		}

	}


}

#ifdef WIN64
DWORD WINAPI _FindKeyGPU(LPVOID lpParam) {
#else
void* _FindKeyGPU(void* lpParam) {
#endif
	TH_PARAM* p = (TH_PARAM*)lpParam;
	p->obj->FindKeyGPU(p);
	return 0;
}

void VanitySearch::checkAddresses(bool compressed, Int key, int i, Point p1) {

	unsigned char h0[20];
	Point pte1[1];
	Point pte2[1];

	// Point
	secp->GetHash160(searchType, compressed, p1, h0);
	address_t pr0 = *(address_t*)h0;
	if (addresses[pr0].items)
		checkAddr(pr0, h0, key, i, 0, compressed);	
}

void VanitySearch::checkAddressesSSE(bool compressed, Int key, int i, Point p1, Point p2, Point p3, Point p4) {

	unsigned char h0[20];
	unsigned char h1[20];
	unsigned char h2[20];
	unsigned char h3[20];
	Point pte1[4];
	Point pte2[4];
	address_t pr0;
	address_t pr1;
	address_t pr2;
	address_t pr3;

	// Point -------------------------------------------------------------------------
	secp->GetHash160(searchType, compressed, p1, p2, p3, p4, h0, h1, h2, h3);	

	pr0 = *(address_t*)h0;
	pr1 = *(address_t*)h1;
	pr2 = *(address_t*)h2;
	pr3 = *(address_t*)h3;

	if (addresses[pr0].items)
		checkAddr(pr0, h0, key, i, 0, compressed);
	if (addresses[pr1].items)
		checkAddr(pr1, h1, key, i + 1, 0, compressed);
	if (addresses[pr2].items)
		checkAddr(pr2, h2, key, i + 2, 0, compressed);
	if (addresses[pr3].items)
		checkAddr(pr3, h3, key, i + 3, 0, compressed);	
}

void VanitySearch::getGPUStartingKeys(Int& tRangeStart, Int& tRangeEnd, int groupSize, int nbThread, Point *p, uint64_t Progress) {
		
	uint32_t grp_startkeys = nbThread/256;

	//New setting key by fixedpaul using addition on secp with batch modular inverse, super fast, multithreading not needed

	Int stepThread;
	Int numthread;

	stepThread.Set(&bc->ksFinish);
	stepThread.Sub(&bc->ksStart);
	stepThread.AddOne();
	numthread.SetInt32(nbThread);
	// Arredonda para cima: com passo truncado, as ultimas chaves de um intervalo
	// que nao e multiplo do numero de threads ficariam sem ser varridas.
	stepThread.Add(&numthread);
	stepThread.SubOne();
	stepThread.Div(&numthread);

	Point Pdouble;
	Int kDouble;

	kDouble.Set(&stepThread);
	kDouble.Mult(grp_startkeys);
	Pdouble = secp->ComputePublicKey(&kDouble);

	Point P_start;
	Int kStart;

	kStart.Set(&stepThread);
	kStart.Mult(grp_startkeys / 2);
	kStart.Add(groupSize / 2 + Progress);

	

	P_start = secp->ComputePublicKey(&kStart);

	p[grp_startkeys / 2] = secp->ComputePublicKey(&tRangeStart);
	p[grp_startkeys / 2] = secp->AddDirect(p[grp_startkeys / 2], P_start);


	Int key_delta;
	Point* p_delta;
	p_delta = new Point[grp_startkeys / 2];

	key_delta.Set(&stepThread);

	

	p_delta[0] = secp->ComputePublicKey(&key_delta);
	key_delta.Add(&stepThread);
	p_delta[1] = secp->ComputePublicKey(&key_delta);

	for (size_t i = 2; i < grp_startkeys / 2; i++) {
		p_delta[i] = secp->AddDirect(p_delta[i - 1], p_delta[0]);
	}

	Int* dx;
	Int* subp;

	subp = new Int[grp_startkeys / 2 + 1];
	dx = new Int[grp_startkeys / 2 + 1];

	uint32_t j;
	//uint32_t i;

	for (size_t i = grp_startkeys / 2; i < nbThread; i += grp_startkeys) {

		double percentage = (100.0 * (double)(i + grp_startkeys / 2)) / (double)(nbThread);
		PainelBR::prepProgress(percentage);


		for (j = 0; j < grp_startkeys / 2; j++) {
			dx[j].ModSub(&p_delta[j].x, &p[i].x);
		}
		dx[grp_startkeys / 2].ModSub(&Pdouble.x, &p[i].x);

		Int newValue;
		Int inverse;

		subp[0].Set(&dx[0]);
		for (size_t j = 1; j < grp_startkeys / 2 + 1; j++) {
			subp[j].ModMulK1(&subp[j - 1], &dx[j]);
		}

		// Do the inversion
		inverse.Set(&subp[grp_startkeys / 2]);
		inverse.ModInv();

		for (j = grp_startkeys / 2; j > 0; j--) {
			newValue.ModMulK1(&subp[j - 1], &inverse);
			inverse.ModMulK1(&dx[j]);
			dx[j].Set(&newValue);
		}

		dx[0].Set(&inverse);

		Int _s;
		Int _p;
		Int dy;
		Int syn;
		syn.Set(&p[i].y);
		syn.ModNeg();



		for (j = 0; j < grp_startkeys / 2 - 1; j++) {

			dy.ModSub(&p_delta[j].y, &p[i].y);
			_s.ModMulK1(&dy, &dx[j]);

			_p.ModSquareK1(&_s);

			p[i + j + 1].x.ModSub(&_p, &p[i].x);
			p[i + j + 1].x.ModSub(&p_delta[j].x);

			p[i + j + 1].y.ModSub(&p_delta[j].x, &p[i + j + 1].x);
			p[i + j + 1].y.ModMulK1(&_s);
			p[i + j + 1].y.ModSub(&p_delta[j].y);

			dy.ModSub(&syn, &p_delta[j].y);
			_s.ModMulK1(&dy, &dx[j]);

			_p.ModSquareK1(&_s);

			p[i - j - 1].x.ModSub(&_p, &p[i].x);
			p[i - j - 1].x.ModSub(&p_delta[j].x);

			p[i - j - 1].y.ModSub(&p[i - j - 1].x, &p_delta[j].x);
			p[i - j - 1].y.ModMulK1(&_s);
			p[i - j - 1].y.ModSub(&p_delta[j].y, &p[i - j - 1].y);
		}

		dy.ModSub(&syn, &p_delta[j].y);
		_s.ModMulK1(&dy, &dx[j]);

		_p.ModSquareK1(&_s);


		p[i - j - 1].x.ModSub(&_p, &p[i].x);
		p[i - j - 1].x.ModSub(&p_delta[j].x);

		p[i - j - 1].y.ModSub(&p[i - j - 1].x, &p_delta[j].x);
		p[i - j - 1].y.ModMulK1(&_s);
		p[i - j - 1].y.ModSub(&p_delta[j].y, &p[i - j - 1].y);

		if (i + grp_startkeys < nbThread) {

			dy.ModSub(&Pdouble.y, &p[i].y);
			_s.ModMulK1(&dy, &dx[grp_startkeys / 2]);

			_p.ModSquareK1(&_s);

			p[i + grp_startkeys].x.ModSub(&_p, &p[i].x);
			p[i + grp_startkeys].x.ModSub(&Pdouble.x);

			p[i + grp_startkeys].y.ModSub(&Pdouble.x, &p[i + grp_startkeys].x);
			p[i + grp_startkeys].y.ModMulK1(&_s);
			p[i + grp_startkeys].y.ModSub(&Pdouble.y);
		}
	}

	delete[] subp;
	delete[] dx;
	delete[] p_delta;
}

void VanitySearch::FindKeyGPU(TH_PARAM* ph) {

	bool ok = true;

	double t0;
	double ttot;
	uint64_t keys_n = 0;
	static uint64_t keys_n_prev = 0;
	static double tprev = 0.0;
	keys_n_prev = 0;   // reinicia entre janelas/retomadas, senao a velocidade
	tprev = 0.0;       // da nova execucao sai zerada

	// Global init
	int thId = ph->threadId;
	GPUEngine g(ph->gpuId, maxFound);
	int numThreadsGPU = g.GetNbThread();
	int STEP_SIZE = g.GetStepSize();
	Point* publicKeys = new Point[numThreadsGPU];
	vector<ITEM> found;

	Point RandomJump_P;
	Int RandomJump_K;
	Int RandomJump_K_last;
	Int RandomJump_K_tot;
	RandomJump_K.SetInt32(STEP_SIZE);
	RandomJump_K_last.SetInt32(0);
	RandomJump_K_tot.SetInt32(0);
	bool kneg = false;

	PainelBR::init(ph->gpuId);
	{
		Int rangeSz;
		rangeSz.Set(&bc->ksFinish);
		rangeSz.Sub(&bc->ksStart);   // fim - inicio: bits do intervalo (2^36 -> 36)
		std::string sHex = bc->ksStart.GetBase16();
		std::string eHex = bc->ksFinish.GetBase16();
		while (sHex.size() < 64) sHex = "0" + sHex;
		while (eHex.size() < 64) eHex = "0" + eHex;
		PainelBR::printHeader(g.deviceName, numThreadsGPU, STEP_SIZE,
			rangeSz.GetBitLength(), sHex, eHex,
			(int)inputAddresses.size(), randomMode);
	}

	counters[thId] = 0;
	
	g.SetSearchMode(searchMode);
	g.SetSearchType(searchType);
	if (onlyFull) {
		g.SetAddress(usedAddressL, nbAddress);
	}
	else {
		g.SetAddress(usedAddress);
	}

	Int stepThread;
	Int taskSize;
	Int numthread;

	taskSize.Set(&bc->ksFinish);
	taskSize.Sub(&bc->ksStart);
	taskSize.AddOne();
	numthread.SetInt32(numThreadsGPU);
	stepThread.Set(&taskSize);
	// Mesmo arredondamento para cima de getGPUStartingKeys()
	stepThread.Add(&numthread);
	stepThread.SubOne();
	stepThread.Div(&numthread);

	Int privkey;
	Int part_key;
	Int keycount;

	t0 = Timer::get_tick();

	getGPUStartingKeys(bc->ksStart, bc->ksFinish, g.GetGroupSize(), numThreadsGPU, publicKeys, (uint64_t)(1ULL * idxcount * g.GetStepSize()));

	ok = g.SetKeys(publicKeys);
	delete[] publicKeys;

	ttot = Timer::get_tick() - t0;


	PainelBR::prepDone(ttot);

	ph->hasStarted = true;

	t0 = Timer::get_tick();

	endOfSearch = false;


	while (ok && !endOfSearch) {

		if (!Pause) {	


			if (randomMode) {
				RandomJump_K_last.Set(&RandomJump_K);
				RandomJump_K_tot.Add(&RandomJump_K);

				RandomJump_K.Rand(256);
				RandomJump_K.Mod(&stepThread);
				RandomJump_K.Sub(&RandomJump_K_tot);
				
				if (RandomJump_K.IsNegative()) {
					RandomJump_K.Neg();
					RandomJump_P = secp->ComputePublicKey(&RandomJump_K);
					RandomJump_P.y.ModNeg();
					RandomJump_K.Neg();
				}
				else {
					RandomJump_P = secp->ComputePublicKey(&RandomJump_K);
				}
				
				ok = g.SetRandomJump(RandomJump_P);
			}

			ok = g.Launch(found, true);
			idxcount += 1;

			if (!randomMode && idxcount%60==0) {
				
				saveBackup(idxcount, ttot, ph->gpuId);
			}
			//printf("\n rnd: %s  idx:  %d \n", RandomJump_K_tot.GetBase10().c_str(), idxcount);

			ttot = Timer::get_tick() - t0 + t_Paused;

			keycount.SetInt32(idxcount - 1);
			keycount.Mult(STEP_SIZE);


			for (int i = 0; i < (int)found.size() && !endOfSearch; i++) {

				ITEM it = found[i];
				part_key.Set(&stepThread);
				part_key.Mult(it.thId);
	
				privkey.Set(&bc->ksStart);
				privkey.Add(&part_key);

				if (randomMode) {
					privkey.Add(&RandomJump_K_tot);
					privkey.Sub(&RandomJump_K_last);
				}
				else {				
					privkey.Add(&keycount);
				}
			
				checkAddr(*(address_t*)(it.hash), it.hash, privkey, it.incr, it.endo, it.mode);
			}

			keycount.Add(STEP_SIZE);
			keycount.Mult(numThreadsGPU);

			keys_n = 1ULL * STEP_SIZE * numThreadsGPU;
			keys_n = keys_n * idxcount;
		
			

			

		} else {
			g.FreeGPUEngine();

			Paused = true;
			t_Paused = ttot;
		}
		

		// Chave atual do segmento 0 (a thread 0 varre ksStart + idx*STEP_SIZE)
		panelCurKey.Set(&bc->ksStart);
		{
			Int seg0;
			seg0.SetInt32(idxcount);
			seg0.Mult(STEP_SIZE);
			panelCurKey.Add(&seg0);
		}

		PrintStats(keys_n, keys_n_prev, ttot, tprev, taskSize, keycount);

		

		// Fim: cada thread ja varreu o seu segmento inteiro (passo de cada thread)
		Int doneStep;
		doneStep.SetInt32(idxcount);
		doneStep.Mult(STEP_SIZE);

		if (doneStep.IsGreaterOrEqual(&stepThread) && (!randomMode))
		{
			double avg_speed = static_cast<double>(keys_n) / (ttot * 1000000.0); // Avg speed in MK/s
			PainelBR::finished(avg_speed, nbFoundKey);

			endOfSearch = true;

			

		}

		keys_n_prev = keys_n;
		tprev = ttot ;

	}


	

	PainelBR::shutdown();

	ph->isRunning = false;

	endOfSearch = true;
}


void VanitySearch::PrintStats(uint64_t keys_n, uint64_t keys_n_prev, double ttot, double tprev, Int taskSize, Int keycount) {

	PainelBR::Status st;

	st.speedMKs = 0.0;
	if (ttot > tprev && keys_n >= keys_n_prev) {
		st.speedMKs = (double)(keys_n - keys_n_prev) / (ttot - tprev) / 1000000.0; // Mkey/s
	}

	st.keysDone = keys_n;
	// Intervalo gigante (> 2^62 chaves): sem barra nem ETA, como o BitCrack-BR
	st.taskSize = (taskSize.GetBitLength() > 62) ? 0.0 : taskSize.ToDouble();
	st.runSec = ttot;
	st.nFound = nbFoundKey;
	st.nTargets = (int)inputAddresses.size();
	st.paused = Paused;
	st.randomMode = randomMode;

	if (randomMode) {
		st.curKeyHex = "";
		st.curKeyApprox = 0.0;
	}
	else {
		std::string hex = panelCurKey.GetBase16();
		while (hex.size() < 64) hex = "0" + hex;
		st.curKeyHex = hex;
		st.curKeyApprox = panelCurKey.ToDouble();
	}

	PainelBR::render(st);

	// Ao pausar o motor e liberado e reiniciado: o proximo painel e novo.
	if (Paused) {
		PainelBR::breakPanel();
		endOfSearch = true;
	}
}


void VanitySearch::saveBackup(int idxcount, double t_Paused, int gpuid) {
	std::string filename = "VSbackup_gpu" + std::to_string(gpuid) + ".dat";
	std::ofstream outFile(filename, std::ios::binary);
	if (outFile) {
		outFile.write(reinterpret_cast<const char*>(&idxcount), sizeof(idxcount));
		outFile.write(reinterpret_cast<const char*>(&t_Paused), sizeof(t_Paused));
		outFile.close();
	}
	else {
		std::cerr << "Error opening file for writing: " << filename << "\n";
	}
}

bool VanitySearch::isAlive(TH_PARAM * p) {

	bool isAlive = true;
	int total = numGPUs;
	for (int i = 0; i < total; i++)
		isAlive = isAlive && p[i].isRunning;

	return isAlive;
}

bool VanitySearch::hasStarted(TH_PARAM * p) {

	bool hasStarted = true;
	int total = numGPUs;
	for (int i = 0; i < total; i++)
		hasStarted = hasStarted && p[i].hasStarted;

	return hasStarted;
}

uint64_t VanitySearch::getGPUCount() {

	uint64_t count = 0;
	for (int i = 0; i < numGPUs; i++) {
		count += counters[i];
	}
	return count;
}

void VanitySearch::saveProgress(TH_PARAM* p, Int& lastSaveKey, BITCRACK_PARAM* bc) {

	Int lowerKey;
	lowerKey.Set(&p[0].THnextKey);

	int total = numGPUs;
	for (int i = 0; i < total; i++) {
		if (p[i].THnextKey.IsLower(&lowerKey))
			lowerKey.Set(&p[i].THnextKey);
	}

	if (lowerKey.IsLowerOrEqual(&lastSaveKey)) return;
	lastSaveKey.Set(&lowerKey);
}

void VanitySearch::Search(std::vector<int> gpuId, std::vector<int> gridSize) {

	//double t0;
	//double t1;
	endOfSearch = false;
	/*numGPUs = ((int)gpuId.size());*/
	numGPUs = 1;
	nbFoundKey = 0;

	memset(counters, 0, sizeof(counters));	

	TH_PARAM* params = (TH_PARAM*)malloc(numGPUs * sizeof(TH_PARAM));
	memset(params, 0, numGPUs * sizeof(TH_PARAM));
	
	std::thread* threads = new std::thread[numGPUs];

#ifdef WIN64
	ghMutex = CreateMutex(NULL, FALSE, NULL);
	mutex = CreateMutex(NULL, FALSE, NULL);
#else
	ghMutex = PTHREAD_MUTEX_INITIALIZER;
	mutex = PTHREAD_MUTEX_INITIALIZER;
#endif

	// Launch GPU threads
	for (int i = 0; i < numGPUs; i++) {
		params[i].obj = this;
		params[i].threadId = i;
		params[i].isRunning = true;
		params[i].gpuId = gpuId[i];
		params[i].gridSizeX = gridSize[i];
		params[i].gridSizeY = gridSize[i+1];
		params[i].THnextKey.Set(&bc->ksNext);
		
		threads[i] = std::thread(_FindKeyGPU, params + i);
	}

	while (!hasStarted(params)) {
		Timer::SleepMillis(500);
	}

	while (!endOfSearch) {
		Timer::SleepMillis(100);
	}

	
	if (params != nullptr) {
		free(params);
	}

}

string VanitySearch::GetHex(vector<unsigned char> &buffer) {

	string ret;

	char tmp[128];
	for (int i = 0; i < (int)buffer.size(); i++) {
		sprintf(tmp, "%02hhX", buffer[i]);
		ret.append(tmp);
	}

	return ret;
}
