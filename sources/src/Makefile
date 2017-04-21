#
# Makefile
#
#define the directory for the executable file
BINDIR = /usr/bin

#define the directory for the data files
#
# Add to CXXflags if desired -DBOOKPATH=$(DATADIR)
#
DATADIR = /usr/share/rodentIII

EXE = rodentIII
CXX = g++
PGOBENCH = (echo bench) | ./$(EXE)

OBJS = attacks.o bitboard.o book.o book_internal.o data.o eval.o eval_draw.o eval_patterns.o eval_pawn.o gen.o init.o \
	legal.o magicmoves.o main.o mask.o movedo.o moveundo.o next.o params.o quiesce.o recognize.o search.o \
	setboard.o swap.o trans.o uci.o uci_options.o util.o

warnings = yes
debug=no
static = no
popcount = yes
lto = yes

CXXFLAGS += -std=c++14 -m64 -O3 -fno-rtti
LDFLAGS += -lpthread -Wl,--gc-sections

#Warnings.
ifeq ($(warnings),yes)
	CXXFLAGS += -Wall -Wpedantic -Wextra -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align \
		-Wunused -Woverloaded-virtual -Wmissing-declarations -Wdeprecated -Wfatal-errors
else
	CXXFLAGS += -w
endif

#Debug.
ifeq ($(debug),yes)
	CXXFLAGS += -g -DWRITEDEBUGFILE
	LDFLAGS += -g
else
	CXXFLAGS += -DNDEBUG
	LDFLAGS += -DNDEBUG
endif

#Static. Only works with g++ on Windows.
ifeq ($(static),yes)
	CXXFLAGS += -static
	LDFLAGS += -static
endif

ifeq ($(popcount),yes)
	CXXFLAGS += -mpopcnt -msse3
	LDFLAGS += -mpopcnt -msse3
endif

#LTO.
ifeq ($(debug),no)
ifeq ($(lto),yes)
	CXXFLAGS += -flto
	LDFLAGS += $(CXXFLAGS)
endif
endif


.PHONY: $(EXE) clang-pgo pgo strip clean install update remove
default: $(EXE)

$(EXE): $(OBJS)
	$(CXX) -o $(EXE) $(OBJS) $(LDFLAGS)
	
pgo:
	$(MAKE) clean
	$(MAKE) CXXFLAGS='$(CXXFLAGS) -fprofile-generate' LDFLAGS='$(LDFLAGS) -lgcov' $(EXE)
	$(PGOBENCH)
	@rm -f *.o
	$(MAKE) CXXFLAGS='$(CXXFLAGS) -fprofile-use -fno-peel-loops -fno-tracer' LDFLAGS='$(LDFLAGS) -lgcov' $(EXE)
	@rm -f *.gcda 
	
clang-pgo:
	$(MAKE) clean
	$(MAKE) CXXFLAGS='$(CXXFLAGS) -fprofile-instr-generate' LDFLAGS='$(LDFLAGS) -fprofile-instr-generate' $(EXE)
	$(PGOBENCH)
	@llvm-profdata merge -output=default.profdata *.profraw
	@rm -f *.o
	$(MAKE) CXXFLAGS='$(CXXFLAGS) -fprofile-instr-use=default.profdata' LDFLAGS='$(LDFLAGS) -fprofile-use' $(EXE)
	@rm -f *.profdata *.profraw
	

strip:
	strip $(EXE)

clean:
	rm -f $(EXE) $(EXE).exe *.o *.gcda
	
install:
	mkdir -p $(BINDIR)
	mkdir -p $(DATADIR)
	cp $(EXENAME) $(BINDIR)
	cp $(CONFIGFILE) $(DATADIR)
	find ../../books/ -name '*.bin' -type f -exec cp '{}' $(DATADIR) \;
	find ../../personalities/ -name '*.ini' -type f -exec cp '{}' $(DATADIR) \;
	chmod 755 $(BINDIR)/$(EXENAME)
	chmod 644 $(DATADIR)/*

# this just performs a copy of rodentIII engine
update:
	cp $(EXENAME) $(BINDIR)
	
remove:
	rm -f $(BINDIR)/$(EXENAME)
	rm -rf $(DATADIR)
