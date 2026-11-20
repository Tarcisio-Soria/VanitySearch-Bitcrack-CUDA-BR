#---------------------------------------------------------------------
# Makefile for vanitysearch
#
# Author : Jean-Luc PONS
# Adaptacao BR (painel, NVML, multi-arch): Tarcisio Soria (KKND)
#
# Uso:
#   make                 # build padrao (com telemetria NVML)
#   make NVML=0          # sem telemetria NVML (nao exige libnvidia-ml)
#   make CCBIN=g++-14    # escolhe o compilador host (padrao: o g++ mais novo encontrado)
#
# IMPORTANTE: -fno-strict-aliasing e obrigatorio. Com gcc >= 12 o codigo de hash
# (SSE/RIPEMD160) herdado do VanitySearch quebra o hash160 na CPU e NENHUMA chave
# e encontrada (aviso "wrong private key generated").
#
# GENCODE: SASS nativo, escolhido conforme o CUDA instalado (CUDA >= 11.8):
# sm_52 (so CUDA < 13), 61, 75, 80, 86, 89, 90 e sm_120 (so CUDA >= 12.8).
# Para build rapido deixe so a arquitetura da sua GPU (ex.: RTX 3060 = sm_86).

# Alvo padrao: compila o programa (a primeira regra abaixo e a do kernel CUDA).
.DEFAULT_GOAL := all

SRC = Base58.cpp IntGroup.cpp main.cpp Random.cpp \
      Timer.cpp Int.cpp IntMod.cpp Point.cpp SECP256K1.cpp \
      Vanity.cpp GPU/GPUGenerate.cpp hash/ripemd160.cpp \
      hash/sha256.cpp hash/sha512.cpp hash/ripemd160_sse.cpp \
      hash/sha256_sse.cpp Bech32.cpp Wildcard.cpp PainelBR.cpp

OBJDIR = obj

OBJET = $(addprefix $(OBJDIR)/, \
        Base58.o IntGroup.o main.o Random.o Timer.o Int.o \
        IntMod.o Point.o SECP256K1.o Vanity.o GPU/GPUGenerate.o \
        hash/ripemd160.o hash/sha256.o hash/sha512.o \
        hash/ripemd160_sse.o hash/sha256_sse.o \
        GPU/GPUEngine.o Bech32.o Wildcard.o PainelBR.o)

CCBIN      ?= $(firstword $(shell which g++-14 g++-13 g++-12 g++-11 g++-10 g++-9 2>/dev/null) g++)
NVML       ?= 1
CXX        = $(CCBIN)
CUDA       = /usr/local/cuda
CXXCUDA    = $(CCBIN)
NVCC       = $(CUDA)/bin/nvcc

# Versao do CUDA (12.9 -> 129) e arquiteturas suportadas por ela
CUDA_VER  := $(shell $(NVCC) --version 2>/dev/null | sed -n 's/.*release \([0-9]*\)\.\([0-9]*\).*/\1\2/p')
GENCODE    = -gencode=arch=compute_61,code=sm_61 \
             -gencode=arch=compute_75,code=sm_75 \
             -gencode=arch=compute_80,code=sm_80 \
             -gencode=arch=compute_86,code=sm_86
ifneq ($(shell [ "0$(CUDA_VER)" -lt 130 ] && echo y),)
GENCODE   += -gencode=arch=compute_52,code=sm_52
endif
ifneq ($(shell [ "0$(CUDA_VER)" -ge 118 ] && echo y),)
GENCODE   += -gencode=arch=compute_89,code=sm_89 -gencode=arch=compute_90,code=sm_90
endif
ifneq ($(shell [ "0$(CUDA_VER)" -ge 128 ] && echo y),)
GENCODE   += -gencode=arch=compute_120,code=sm_120
endif

ifdef debug
CXXFLAGS   = -include cstdint -fno-strict-aliasing -mssse3 -Wno-write-strings -g -I. -I$(CUDA)/include
else
CXXFLAGS   = -include cstdint -fno-strict-aliasing -mssse3 -Wno-write-strings -O2 -I. -I$(CUDA)/include
endif
LFLAGS     = -lpthread -L$(CUDA)/lib64 -lcudart
# STATIC=1: runtime do CUDA e libstdc++/libgcc embutidas no binario (usado na Release);
# so o driver NVIDIA (libnvidia-ml) e a glibc do sistema continuam dinamicos.
ifeq ($(STATIC),1)
LFLAGS     = -static-libstdc++ -static-libgcc -lpthread -L$(CUDA)/lib64 -lcudart_static -ldl -lrt
endif
ifeq ($(NVML),1)
CXXFLAGS  += -DHAVE_NVML
LFLAGS    += -L$(CUDA)/lib64/stubs -lnvidia-ml
endif

#--------------------------------------------------------------------

ifdef debug
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) -G -maxrregcount=0 --ptxas-options=-v --compile --compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -g -I$(CUDA)/include $(GENCODE) -o $(OBJDIR)/GPU/GPUEngine.o -c GPU/GPUEngine.cu
else
$(OBJDIR)/GPU/GPUEngine.o: GPU/GPUEngine.cu
	$(NVCC) -maxrregcount=0 --ptxas-options=-v --compile --compiler-options -fPIC -ccbin $(CXXCUDA) -m64 -O2 -I$(CUDA)/include $(GENCODE) -o $(OBJDIR)/GPU/GPUEngine.o -c GPU/GPUEngine.cu
endif

$(OBJDIR)/%.o : %.cpp
	$(CXX) $(CXXFLAGS) -o $@ -c $<

all: VanitySearch

VanitySearch: $(OBJET)
	@echo Making VanitySearch...
	$(CXX) $(OBJET) $(LFLAGS) -o vanitysearch

$(OBJET): | $(OBJDIR) $(OBJDIR)/GPU $(OBJDIR)/hash

$(OBJDIR):
	mkdir -p $(OBJDIR)

$(OBJDIR)/GPU: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p GPU

$(OBJDIR)/hash: $(OBJDIR)
	cd $(OBJDIR) &&	mkdir -p hash

clean:
	@echo Cleaning...
	@rm -f obj/*.o
	@rm -f obj/GPU/*.o
	@rm -f obj/hash/*.o

