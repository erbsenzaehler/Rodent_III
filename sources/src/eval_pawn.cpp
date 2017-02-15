/*
Rodent, a UCI chess playing engine derived from Sungorus 1.4
Copyright (C) 2009-2011 Pablo Vazquez (Sungorus author)
Copyright (C) 2011-2017 Pawel Koziol

Rodent is free software: you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version.

Rodent is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without
even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along with this program.
If not, see <http://www.gnu.org/licenses/>.
*/

#include "rodent.h"

static const int empty_ks[64] = {  
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30,
   0,   0,   0,   0,   0, -10, -20, -30};

static const int empty_qs[64] = {
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0,
 -30, -20, -10,   0,   0,   0,   0,   0};

void cEngine::ClearPawnHash(void) {

  for (int e = 0; e < PAWN_HASH_SIZE; e++) {
    PawnTT[e].key = 0;
    PawnTT[e].mg_pawns = 0;
    PawnTT[e].eg_pawns = 0;
  }
}

void cEngine::ScorePawnStruct(POS * p, eData * e) {

  // Try to retrieve score from pawn hashtable

  int addr = p->pawn_key % PAWN_HASH_SIZE;

  if (PawnTT[addr].key == p->pawn_key) {
    e->mg_pawns[WC] = PawnTT[addr].mg_pawns;
    e->eg_pawns[WC] = PawnTT[addr].eg_pawns;
	e->mg_pawns[BC] = 0;
	e->eg_pawns[BC] = 0;
    return;
  }

  e->mg_pawns[WC] = 0;
  e->mg_pawns[BC] = 0;
  e->eg_pawns[WC] = 0;
  e->eg_pawns[BC] = 0;

  EvaluatePawns(p, e, WC);
  EvaluatePawns(p, e, BC);
  EvaluateKing(p, e, WC);
  EvaluateKing(p, e, BC);

  // King on a wing without pawns

  U64 bb_all_pawns = p->Pawns(WC) | p->Pawns(BC);

  if (bb_all_pawns) {
    if (!(bb_all_pawns & Mask.k_side)) {
      AddPawns(e, WC, empty_ks[p->king_sq[WC]], empty_ks[p->king_sq[WC]]);
      AddPawns(e, BC, empty_ks[p->king_sq[BC]], empty_ks[p->king_sq[BC]]);
    }

    if (!(bb_all_pawns & Mask.q_side)) {
      AddPawns(e, WC, empty_qs[p->king_sq[WC]], empty_qs[p->king_sq[WC]]);
      AddPawns(e, BC, empty_qs[p->king_sq[BC]], empty_qs[p->king_sq[BC]]);
    }
  }

  // Evaluate pawn islands (based on Texel)

  const U64 wPawns = p->Pawns(WC);
  const U64 wPawnFiles = BB.FillSouth(wPawns) & 0xff;
  const int wIslands = BB.PopCnt(((~wPawnFiles) >> 1) & wPawnFiles);

  const U64 bPawns = p->Pawns(BC);
  const U64 bPawnFiles = BB.FillSouth(bPawns) & 0xff;
  const int bIslands = BB.PopCnt(((~bPawnFiles) >> 1) & bPawnFiles);
  e->mg_pawns[WC] -= (wIslands - bIslands) * 7;
  e->eg_pawns[WC] -= (wIslands - bIslands) * 7;
	
  // Save stuff in pawn hashtable

  PawnTT[addr].key = p->pawn_key;
  PawnTT[addr].mg_pawns = (Par.struct_weight * (e->mg_pawns[WC] - e->mg_pawns[BC])) / 100;
  PawnTT[addr].eg_pawns = (Par.struct_weight * (e->eg_pawns[WC] - e->eg_pawns[BC])) / 100;

}

void cEngine::EvaluateKing(POS *p, eData *e, int sd) {

  const int qCastle[2] = { B1, B8 };
  const int kCastle[2] = { G1, G8 };
  U64 bb_king_file, bb_next_file;
  int shield = 0;
  int storm = 0;
  int sq = KingSq(p, sd);

  // Normalize king square for pawn shield evaluation,
  // to discourage shuffling the king between g1 and h1.

  if (SqBb(sq) & Mask.ks_castle[sd]) sq = kCastle[sd];
  if (SqBb(sq) & Mask.qs_castle[sd]) sq = qCastle[sd];

  // Evaluate shielding and storming pawns on each file.

  bb_king_file = BB.FillNorth(SqBb(sq)) | BB.FillSouth(SqBb(sq));
  ScoreKingFile(p, sd, bb_king_file, &shield, &storm);

  bb_next_file = ShiftEast(bb_king_file);
  if (bb_next_file) ScoreKingFile(p, sd, bb_next_file, &shield, &storm);

  bb_next_file = ShiftWest(bb_king_file);
  if (bb_next_file) ScoreKingFile(p, sd, bb_next_file, &shield, &storm);

  AddPawns(e, sd, ((Par.shield_weight * shield) / 100) + ((Par.storm_weight * storm) / 100), 0);
  AddPawns(e, sd, ScoreChains(p, sd), 0);
}

void cEngine::ScoreKingFile(POS * p, int sd, U64 bb_file, int *shield, int *storm) {

  int shelter = ScoreFileShelter(bb_file &  p->Pawns(sd), sd);
  if (p->Kings(sd) & bb_file) shelter = ((shelter * 120) / 100);
  if (bb_file & bbCentralFile) shelter /= 2;
  *shield += shelter;
  *storm += ScoreFileStorm(bb_file & p->Pawns(Opp(sd)), sd);
}

int cEngine::ScoreFileShelter(U64 bb_own_pawns, int sd) {

  if (!bb_own_pawns) return -36;
  if (bb_own_pawns & bbRelRank[sd][RANK_2]) return    2;
  if (bb_own_pawns & bbRelRank[sd][RANK_3]) return  -11;
  if (bb_own_pawns & bbRelRank[sd][RANK_4]) return  -20;
  if (bb_own_pawns & bbRelRank[sd][RANK_5]) return  -27;
  if (bb_own_pawns & bbRelRank[sd][RANK_6]) return  -32;
  if (bb_own_pawns & bbRelRank[sd][RANK_7]) return  -35;
	return 0;
}

int cEngine::ScoreFileStorm(U64 bb_opp_pawns, int sd) {

  if (!bb_opp_pawns) return -16;
  if (bb_opp_pawns & bbRelRank[sd][RANK_3]) return -32;
  if (bb_opp_pawns & bbRelRank[sd][RANK_4]) return -16;
  if (bb_opp_pawns & bbRelRank[sd][RANK_5]) return -8;
	return 0;
}

#define SQ(sq) RelSqBb(sq,sd)
#define opPawns p->Pawns(op)
#define sdPawns p->Pawns(sd)
#define OWN_PAWN(sq) (p->Pawns(sd) & RelSqBb(sq,sd))
#define OPP_PAWN(sq) (p->Pawns(op) & RelSqBb(sq,sd))
#define CONTAINS(bb, s1, s2) (bb & SQ(s1)) && (bb & SQ(s2))
static const int bigChainScore = 18;
static const int smallChainScore = 13;

int cEngine::ScoreChains(POS *p, int sd)
{
  int mgResult = 0;
  int sq = p->king_sq[sd];
  int op = Opp(sd);

  // basic pointy chain

  if (SqBb(sq) & Mask.ks_castle[sd]) {

    if (OPP_PAWN(E4)) {
      if (CONTAINS(opPawns, D5, C6)) { // c6-d5-e4 triad
        mgResult -= (CONTAINS(sdPawns, D4, E3)) ? bigChainScore : smallChainScore;
      }

      if (CONTAINS(opPawns, D5, F3)) { // d5-e4-f3 triad
        mgResult -= (OWN_PAWN(E3)) ? bigChainScore : smallChainScore;
      }
    }

    if (OPP_PAWN(E5)) {
      if (CONTAINS(opPawns, F4, D6)) { // d6-e5-f4 triad
        // storm of a "g" pawn in the King's Indian
      if (OPP_PAWN(G5)) {
            mgResult -= 4; 
            if (OPP_PAWN(H4)) return 10; // opponent did us a favour!
      }
        if (OPP_PAWN(G4)) mgResult -= 12;

        mgResult -= (CONTAINS(sdPawns, E4, D5)) ? bigChainScore : smallChainScore;
      }

      if (CONTAINS(opPawns, G3, F4)) { // e5-f4-g3 triad
        mgResult -= (OWN_PAWN(F3)) ? bigChainScore : smallChainScore;
      }
    }
  }
  
  if (SqBb(sq) & Mask.qs_castle[sd]) {

    // basic pointy chain

    if (OPP_PAWN(D4)) {
      if (CONTAINS(opPawns, E5, F6)) {
        mgResult -= (CONTAINS(sdPawns, E4, D3)) ? bigChainScore : smallChainScore;
      }
      
      if (CONTAINS(opPawns, F5, C3)) {
        mgResult -= (SQ(D3) & sdPawns) ? bigChainScore : smallChainScore;
      }
    }

    if (OPP_PAWN(D5)) {
      if (CONTAINS(opPawns, C4, E6)) {
        // storm of a "b" pawn
        if (OPP_PAWN(B5)) {
          mgResult -= 4;
          if (OPP_PAWN(A4)) return 0; // this is not how you handle pawn chains
        }
        if (OPP_PAWN(B4)) mgResult -= 12;

        mgResult -= (CONTAINS(sdPawns, E4, D5)) ? bigChainScore : smallChainScore;
      }

      if (CONTAINS(opPawns, B3, C4)) {
        mgResult -= (OWN_PAWN(C3)) ? bigChainScore : smallChainScore;
      }
    }
  }

  return mgResult;
}