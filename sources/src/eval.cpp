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
#include "eval.h"
#include <cstdio>
#include <cstring>
#include <cmath> // round

static const U64 bb_center = SqBb(C3) | SqBb(D3) | SqBb(E3) | SqBb(F3)
                           | SqBb(C4) | SqBb(D4) | SqBb(E4) | SqBb(F4)
	                       | SqBb(C5) | SqBb(D5) | SqBb(E5) | SqBb(F5)
	                       | SqBb(C6) | SqBb(D6) | SqBb(E6) | SqBb(F6);

void cEngine::ClearAll() {

    ClearPawnHash();
    ClearEvalHash();
    ClearHist();
}

void cEngine::ClearEvalHash() {

    ZEROARRAY(EvalTT);
}

void cEngine::EvaluateMaterial(POS *p, eData *e, int sd) {

    int op = Opp(sd);

    int tmp = Par.np_table[p->cnt[sd][P]] * p->cnt[sd][N]    // knights lose value as pawns disappear
            - Par.rp_table[p->cnt[sd][P]] * p->cnt[sd][R];   // rooks gain value as pawns disappear

    if (p->cnt[sd][N] > 1) tmp += Par.values[N_PAIR];        // knight pair
    if (p->cnt[sd][R] > 1) tmp += Par.values[R_PAIR];        // rook pair
    if (p->cnt[sd][B] > 1) tmp += Par.values[B_PAIR];        // bishop pair

    // "elephantiasis correction" for queen, idea by H.G.Mueller (nb. rookVsQueen doesn't help)

    if (p->cnt[sd][Q])
        tmp -= Par.values[ELEPH] * (p->cnt[op][N] + p->cnt[op][B]);

    Add(e, sd, tmp);
}

void cEngine::EvaluatePieces(POS *p, eData *e, int sd) {

    U64 bb_pieces, bb_attack, bb_control, bb_possible, bb_contact, bb_zone, bb_file;
    int sq, cnt, own_p_cnt, opp_p_cnt;
    int r_on_7th = 0;
    int mob_mg = 0;
    int mob_eg = 0;
    int tropism_mg = 0;
    int tropism_eg = 0;
    int lines_mg = 0;
    int lines_eg = 0;
    int fwd_weight = 0;
    int fwd_cnt = 0;
    int outpost = 0;
    int att = 0;
    int wood = 0;
	int center_control = 2 * BB.PopCnt(e->p_takes[sd] & bb_center);

    // Init king attack zone

    int op = Opp(sd);
    int king_sq = KingSq(p, op);
    bb_zone = BB.KingAttacks(king_sq);
    bb_zone |= BB.ShiftFwd(bb_zone, op);

    // Init helper bitboards

    U64 n_checks = BB.KnightAttacks(king_sq) & ~p->cl_bb[sd] & ~e->p_takes[op];
    U64 b_checks = BB.BishAttacks(OccBb(p), king_sq) & ~p->cl_bb[sd] & ~e->p_takes[op];
    U64 r_checks = BB.RookAttacks(OccBb(p), king_sq) & ~p->cl_bb[sd] & ~e->p_takes[op];
    U64 q_checks = r_checks & b_checks;
    U64 bb_excluded = p->Pawns(sd);

    // Knight eval

    bb_pieces = p->Knights(sd);
    while (bb_pieces) {
        sq = BB.PopFirstBit(&bb_pieces);                    // get square

        // knight tropism to enemy king (based on Gambit Fruit)

        tropism_mg += Par.values[NTR_MG] * Dist.bonus[sq][king_sq];
        tropism_eg += Par.values[NTR_EG] * Dist.bonus[sq][king_sq];

        if (SqBb(sq) & Mask.away[sd]) {                     // forwardness (based on Toga II 3.0)
            fwd_weight += Par.values[N_FWD];
            fwd_cnt += 1;
        }

        bb_control = BB.KnightAttacks(sq) & ~p->cl_bb[sd];  // get control bitboard
		center_control += BB.PopCnt(bb_control & bb_center);
        if (!(bb_control  & ~e->p_takes[op] & Mask.away[sd])) // we do not attack enemy half of the board
            Add(e, sd, Par.values[N_OWH]);
        e->all_att[sd] |= BB.KnightAttacks(sq);
        e->ev_att[sd]  |= bb_control;
        if (bb_control & n_checks) att += Par.values[N_CHK];// check threats

        bb_possible = bb_control & ~e->p_takes[op];         // reachable outposts
        bb_possible &= ~e->p_can_take[op];
        bb_possible &= Mask.outpost_map[sd];
        if (bb_possible) Add(e, sd, Par.values[N_REACH], 2);

        bb_attack = BB.KnightAttacks(sd);
        if (bb_attack & bb_zone) {                          // king attack
            wood++;
            att += Par.values[N_ATT1] * BB.PopCnt(bb_attack & (bb_zone & ~e->p_takes[op]));
            att += Par.values[N_ATT2] * BB.PopCnt(bb_attack & (bb_zone & e->p_takes[op]));
        }

        cnt = BB.PopCnt(bb_control & ~e->p_takes[op]);      // get mobility count
        mob_mg += Par.n_mob_mg[cnt];
        mob_eg += Par.n_mob_eg[cnt];

        EvaluateOutpost(p, e, sd, N, sq, &outpost);         // outpost
    }

    // Bishop eval

    bb_pieces = p->Bishops(sd);
    while (bb_pieces) {
        sq = BB.PopFirstBit(&bb_pieces);                    // get square

        // bishop tropism  to enemy king (based on Gambit Fruit)

        tropism_mg += Par.values[BTR_MG] * Dist.bonus[sq][king_sq];
        tropism_eg += Par.values[BTR_EG] * Dist.bonus[sq][king_sq];

        if (SqBb(sq) & Mask.away[sd]) {                     // forwardness (based on Toga II 3.0)
            fwd_weight += Par.values[B_FWD];
            fwd_cnt += 1;
        }

        bb_control = BB.BishAttacks(OccBb(p), sq);          // get control bitboard
		center_control += BB.PopCnt(bb_control & bb_center);
        e->all_att[sd] |= bb_control;                       // update attack map
        e->ev_att[sd]  |= bb_control;
        if (!(bb_control & Mask.away[sd]))
             Add(e, sd, Par.values[B_OVH]);                 // we do not attack enemy half of the board
        if (bb_control & b_checks) att += Par.values[B_CHK];// check threats

        bb_attack = BB.BishAttacks(OccBb(p) ^ p->Queens(sd), sq);  // get king attack bitboard

        if (bb_attack & bb_zone) {                          // evaluate king attacks
            wood++;
            att += Par.values[B_ATT1] * BB.PopCnt(bb_attack & (bb_zone & ~e->p_takes[op]));
            att += Par.values[B_ATT2] * BB.PopCnt(bb_attack & (bb_zone & e->p_takes[op]));
        }

        cnt = BB.PopCnt(bb_control & ~e->p_takes[op] & ~bb_excluded); // get mobility count
        mob_mg += Par.b_mob_mg[cnt];
        mob_eg += Par.b_mob_eg[cnt];

        bb_possible = bb_control & ~e->p_takes[op];         // reachable outposts
        bb_possible &= ~e->p_can_take[op];
        bb_possible &= Mask.outpost_map[sd];
        if (bb_possible) Add(e, sd, Par.values[B_REACH], 2);

        EvaluateOutpost(p, e, sd, B, sq, &outpost);         // outpost

        // Bishops side by side

        if (ShiftNorth(SqBb(sq)) & p->Bishops(sd))
            Add(e, sd, Par.values[B_TOUCH]);
        if (ShiftEast(SqBb(sq)) & p->Bishops(sd))
            Add(e, sd, Par.values[B_TOUCH]);

        // Pawns on the same square color as our bishop

        if (bbWhiteSq & SqBb(sq)) {
            own_p_cnt = BB.PopCnt(bbWhiteSq & p->Pawns(sd)) - 4;
            opp_p_cnt = BB.PopCnt(bbWhiteSq & p->Pawns(op)) - 4;
        } else {
            own_p_cnt = BB.PopCnt(bbBlackSq & p->Pawns(sd)) - 4;
            opp_p_cnt = BB.PopCnt(bbBlackSq & p->Pawns(op)) - 4;
        }

        Add(e, sd, Par.values[B_OWN_P] * own_p_cnt
                 + Par.values[B_OPP_P] * opp_p_cnt);
    }

    // Rook eval

    bb_pieces = p->Rooks(sd);
    while (bb_pieces) {
        sq = BB.PopFirstBit(&bb_pieces);                    // get square

        // rook tropism to enemy king (based on Gambit Fruit)

        tropism_mg += Par.values[RTR_MG] * Dist.bonus[sq][king_sq];
        tropism_eg += Par.values[RTR_EG] * Dist.bonus[sq][king_sq];

        if (SqBb(sq) & Mask.away[sd]) {                     // forwardness (based on Toga II 3.0)
            fwd_weight += Par.values[R_FWD];
            fwd_cnt += 1;
        }

        bb_control = BB.RookAttacks(OccBb(p), sq);          // get control bitboard
        e->all_att[sd] |= bb_control;                       // update attack map
        e->ev_att[sd] |= bb_control;

        if ((bb_control & ~p->cl_bb[sd] & r_checks)
        && p->Queens(sd)) {
            att += Par.values[R_CHK];                       // check threat bonus
            bb_contact = (bb_control & BB.KingAttacks(king_sq)) & r_checks;  // get contact check bitboard

            while (bb_contact) {
                int contactSq = BB.PopFirstBit(&bb_contact);    // find a potential contact check
                if (Swap(p, sq, contactSq) >= 0) {              // rook exchanges are also accepted
                    att += Par.values[R_CONTACT];
                    break;
                }
            }
        }

        bb_attack = BB.RookAttacks(OccBb(p) ^ p->StraightMovers(sd), sq);  // get king attack bitboard

        if (bb_attack & bb_zone) {                                         // evaluate king attacks
            wood++;
            att += Par.values[R_ATT1] * BB.PopCnt(bb_attack & (bb_zone & ~e->p_takes[op]));
            att += Par.values[R_ATT2] * BB.PopCnt(bb_attack & (bb_zone & e->p_takes[op]));
        }

        cnt = BB.PopCnt(bb_control & ~bb_excluded);                        // get mobility count
        mob_mg += Par.r_mob_mg[cnt];
        mob_eg += Par.r_mob_eg[cnt];

        // FILE EVALUATION:

        bb_file = BB.FillNorth(SqBb(sq)) | BB.FillSouth(SqBb(sq));   // get file

        if (bb_file & p->Queens(op)) {                      // enemy queen on rook's file
            lines_mg += Par.values[ROQ_MG];
            lines_eg += Par.values[ROQ_EG];
        }

        if (!(bb_file & p->Pawns(sd))) {                    // no own pawns on that file
            if (!(bb_file & p->Pawns(op))) {
                lines_mg += Par.values[ROF_MG];
                lines_eg += Par.values[ROF_EG];
            } else {                                             // half-open file...
                if (bb_file & (p->Pawns(op) & e->p_takes[op])) { // ...with defended enemy pawn
                    lines_mg += Par.values[RBH_MG];
                    lines_eg += Par.values[RBH_EG];
                } else {                                         // ...with undefended enemy pawn
                    lines_mg += Par.values[RGH_MG];
                    lines_eg += Par.values[RGH_EG];
                }
            }
        }

        // Rook on the 7th rank attacking pawns or cutting off enemy king

        if (SqBb(sq) & bb_rel_rank[sd][RANK_7]) {                // rook on 7th rank
            if (p->Pawns(op) & bb_rel_rank[sd][RANK_7]           // attacking enemy pawns
            || p->Kings(op) & bb_rel_rank[sd][RANK_8]) {         // or cutting off enemy king
                lines_mg += Par.values[RSR_MG];
                lines_eg += Par.values[RSR_EG];
                r_on_7th++;
            }
        }
    }

    // Queen eval

    bb_pieces = p->Queens(sd);
    while (bb_pieces) {
        sq = BB.PopFirstBit(&bb_pieces);                    // get square

        // queen tropism to enemy king (based on Gambit Fruit)

        tropism_mg += Par.values[QTR_MG] * Dist.bonus[sq][king_sq];
        tropism_eg += Par.values[QTR_EG] * Dist.bonus[sq][king_sq];

        if (SqBb(sq) & Mask.away[sd]) {                     // forwardness (based on Toga II 3.0)
            fwd_weight += Par.values[Q_FWD];
            fwd_cnt += 1;
        }

        bb_control = BB.QueenAttacks(OccBb(p), sq);         // get control bitboard
        e->all_att[sd] |= bb_control;                       // update attack map
        if (bb_control & q_checks) {                        // check threat bonus
            att += Par.values[Q_CHK];

            bb_contact = bb_control & BB.KingAttacks(king_sq);// queen contact checks
            while (bb_contact) {
                int contactSq = BB.PopFirstBit(&bb_contact);    // find potential contact check square
                if (Swap(p, sq, contactSq) >= 0) {              // if check doesn't lose material, evaluate
                    att += Par.values[Q_CONTACT];
                    break;
                }
            }
        }

        bb_attack  = BB.BishAttacks(OccBb(p) ^ p->DiagMovers(sd), sq);
        bb_attack |= BB.RookAttacks(OccBb(p) ^ p->StraightMovers(sd), sq);

        if (bb_attack & bb_zone) {                          // evaluate king attacks
            wood++;
            att += Par.values[Q_ATT1] * BB.PopCnt(bb_attack & (bb_zone & ~e->p_takes[op]));
            att += Par.values[Q_ATT2] * BB.PopCnt(bb_attack & (bb_zone & e->p_takes[op]));
        }

        cnt = BB.PopCnt(bb_control & ~bb_excluded);         // get mobility count
        mob_mg += Par.q_mob_mg[cnt];
        mob_eg += Par.q_mob_eg[cnt];

        if (SqBb(sq) & bb_rel_rank[sd][RANK_7]) {           // queen on 7th rank
            if (p->Pawns(op) & bb_rel_rank[sd][RANK_7]      // attacking enemy pawns
            ||  p->Kings(op) & bb_rel_rank[sd][RANK_8]) {   // or cutting off enemy king
                lines_mg += Par.values[QSR_MG];
                lines_eg += Par.values[QSR_EG];
            }
        }
    } // end of queen eval

    // Composite factors

    if (r_on_7th > 1) {  // two rooks on 7th rank
        lines_mg += Par.values[RS2_MG];
        lines_eg += Par.values[RS2_EG];
    }

    // Weighting eval parameters

    Add(e, sd, (Par.sd_mob[sd] * mob_mg)  / 100, (Par.sd_mob[sd] * mob_eg)  / 100);
    Add(e, sd, (Par.tropism_weight * tropism_mg) / 100, (Par.tropism_weight * tropism_eg) / 100);
    Add(e, sd, (Par.lines_weight * lines_mg)     / 100, (Par.lines_weight * lines_eg)     / 100);
    Add(e, sd, (Par.forward_weight * fwd_bonus[fwd_cnt] * fwd_weight) / 100, 0);
    Add(e, sd, (Par.outposts_weight * outpost) / 100);
	Add(e, sd, (Par.center_weight * center_control) / 100, 0);

    // King attack eval

    if (wood > 1) {
        if (att > 399) att = 399;
        if (p->cnt[sd][Q] == 0) att = 0;
        Add(e, sd, (Par.danger[att] * Par.sd_att[sd]) / 100);
    }

}

void cEngine::EvaluateOutpost(POS *p, eData *e, int sd, int pc, int sq, int *outpost) {

    if (SqBb(sq) & Mask.home[sd]) {
        U64 stop = BB.ShiftFwd(SqBb(sq), sd);             // get square in front of a minor
        if (stop & p->Pawns(sd))                          // is it occupied by own pawn?
            *outpost += Par.values[BN_SHIELD];            // bonus for a pawn shielding a minor
    }

    int tmp = Par.sp_pst[sd][pc][sq];                     // get base outpost bonus
    if (tmp) {
        int mul = 0;                                      // reset outpost multiplier
        if (SqBb(sq) & ~e->p_can_take[Opp(sd)]) mul += 2; // is piece in hole of enemy pawn structure?
        if (SqBb(sq) & e->p_takes[sd]) mul += 1;          // is piece defended by own pawn?
        if (SqBb(sq) & e->two_pawns_take[sd]) mul += 1;   // is piece defended by two pawns?
        *outpost += (tmp * mul) / 2;                      // add outpost bonus
    }
}

void cEngine::EvaluatePawns(POS *p, eData *e, int sd) {

    U64 bb_pieces, front_span, fl_phalanx;
    int sq, fl_unopposed;
    int op = Opp(sd);
    int mass_mg = 0;
    int mass_eg = 0;

    bb_pieces = p->Pawns(sd);
    while (bb_pieces) {

        // Set data and flags

        sq = BB.PopFirstBit(&bb_pieces);
        front_span = BB.GetFrontSpan(SqBb(sq), sd);
        fl_unopposed = ((front_span & p->Pawns(op)) == 0);
        fl_phalanx = (BB.ShiftSideways(SqBb(sq)) & p->Pawns(sd));

        // Candidate passers

        if (fl_unopposed) {
            if (fl_phalanx) {
                if (BB.PopCnt((Mask.passed[sd][sq] & p->Pawns(op))) == 1)
                    AddPawns(e, sd, Par.passed_bonus_mg[sd][Rank(sq)] / 3, Par.passed_bonus_eg[sd][Rank(sq)] / 3);
            }
        }

        // Doubled pawn

        if (front_span & p->Pawns(sd))
            AddPawns(e, sd, Par.values[DB_MID], Par.values[DB_END]);

        // Supported pawn

        if (fl_phalanx) {
            mass_mg += Par.sp_pst[sd][PHA_MG][sq];
            mass_eg += Par.sp_pst[sd][PHA_EG][sq];
        }
        else if (SqBb(sq) & e->p_takes[sd]) {
            mass_mg += Par.sp_pst[sd][DEF_MG][sq];
            mass_eg += Par.sp_pst[sd][DEF_EG][sq];
        }

        // Isolated and weak pawn

        if (!(Mask.adjacent[File(sq)] & p->Pawns(sd)))
            AddPawns(e, sd, Par.values[ISO_MG] + Par.values[ISO_OF] * fl_unopposed, Par.values[ISO_EG]);
        else if (!(Mask.supported[sd][sq] & p->Pawns(sd)))
            AddPawns(e, sd, Par.backward_malus_mg[File(sq)] + Par.values[BK_OPE] * fl_unopposed, Par.values[BK_END]);
    }

    AddPawns(e, sd, (mass_mg * Par.pawn_mass_weight) / 100, (mass_eg * Par.pawn_mass_weight) / 100);
}

void cEngine::EvaluatePassers(POS *p, eData *e, int sd) {

    U64 bb_pieces, bb_pawn, bb_stop;
    int sq, mg_tmp, eg_tmp, mul;
    int op = Opp(sd);
    int mg_tot = 0;
    int eg_tot = 0;

    bb_pieces = p->Pawns(sd);
    while (bb_pieces) {
        sq = BB.PopFirstBit(&bb_pieces);
        bb_pawn = SqBb(sq);
        bb_stop = BB.ShiftFwd(SqBb(sq), sd);

        // pawn can attack enemy piece

        if (!(bb_stop & OccBb(p))) {
           if (!(bb_stop & e->p_can_take[op])) {
              if (BB.GetPawnControl(bb_stop, sd) & (p->Bishops(op) | p->Knights(op)))
                  Add(e, sd, Par.values[P_THR]);
              if (bb_pawn & (RANK_2_BB | RANK_7_BB)) { // possible attack by a double pawn move
                   U64 next = BB.ShiftFwd(bb_stop, sd);
                   if (!(next & OccBb(p))) {
                       if (!(next & e->p_can_take[op])) {
                           if (BB.GetPawnControl(next, sd) & (p->Bishops(op) | p->Knights(op)))
                               Add(e, sd, Par.values[P_THR]);
                       }
                   }
               }
           }
        }

        // passed pawns

        if (!(Mask.passed[sd][sq] & p->Pawns(op))) {
            mul = 100;

            if (bb_pawn & e->p_takes[sd]) mul += Par.values[P_DEFMUL];
            if (bb_stop & e->p_takes[sd]) mul += Par.values[P_STOPMUL];

            if (bb_stop & OccBb(p)) mul -= Par.values[P_BL_MUL];   // blocked passers score less

            else if ((bb_stop & e->all_att[sd])  // our control of stop square
                 && (bb_stop & ~e->all_att[op])) mul += Par.values[P_OURSTOP_MUL];

            else if ((bb_stop & e->all_att[op])  // opp control of stop square
                 && (bb_stop & ~e->all_att[sd])) mul -= Par.values[P_OPPSTOP_MUL];

            // in the midgame, we use just a bonus from the table
            // in the endgame, passed pawn attracts both kings.

            mg_tmp = Par.passed_bonus_mg[sd][Rank(sq)];
            eg_tmp = Par.passed_bonus_eg[sd][Rank(sq)]
                     - ((Par.passed_bonus_eg[sd][Rank(sq)] * Dist.bonus[sq][p->king_sq[op]]) / 30)
                     + ((Par.passed_bonus_eg[sd][Rank(sq)] * Dist.bonus[sq][p->king_sq[sd]]) / 90);

            mg_tot += (mg_tmp * mul) / 100;
            eg_tot += (eg_tmp * mul) / 100;
        }
    }

    Add(e, sd, (mg_tot * Par.passers_weight) / 100, (eg_tot * Par.passers_weight) / 100);
}

void cEngine::EvaluateUnstoppable(eData *e, POS *p) {

    U64 bb_pieces, bb_span;
    int w_dist = 8;
    int b_dist = 8;
    int sq, king_sq, pawn_sq, tempo, prom_dist;

    // White unstoppable passers

    if (p->cnt[BC][N] + p->cnt[BC][B] + p->cnt[BC][R] + p->cnt[BC][Q] == 0) {
        king_sq = KingSq(p, BC);
        if (p->side == BC) tempo = 1; else tempo = 0;
        bb_pieces = p->Pawns(WC);
        while (bb_pieces) {
            sq = BB.PopFirstBit(&bb_pieces);
            if (!(Mask.passed[WC][sq] & p->Pawns(BC))) {
                bb_span = BB.GetFrontSpan(SqBb(sq), WC);
                pawn_sq = ((WC - 1) & 56) + (sq & 7);
                prom_dist = Min(5, Dist.metric[sq][pawn_sq]);

                if (prom_dist < (Dist.metric[king_sq][pawn_sq] - tempo)) {
                    if (bb_span & p->Kings(WC)) prom_dist++;
                    w_dist = Min(w_dist, prom_dist);
                }
            }
        }
    }

    // Black unstoppable passers

    if (p->cnt[WC][N] + p->cnt[WC][B] + p->cnt[WC][R] + p->cnt[WC][Q] == 0) {
        king_sq = KingSq(p, WC);
        if (p->side == WC) tempo = 1; else tempo = 0;
        bb_pieces = p->Pawns(BC);
        while (bb_pieces) {
            sq = BB.PopFirstBit(&bb_pieces);
            if (!(Mask.passed[BC][sq] & p->Pawns(WC))) {
                bb_span = BB.GetFrontSpan(SqBb(sq), BC);
                pawn_sq = ((BC - 1) & 56) + (sq & 7);
                prom_dist = Min(5, Dist.metric[sq][pawn_sq]);

                if (prom_dist < (Dist.metric[king_sq][pawn_sq] - tempo)) {
                    if (bb_span & p->Kings(BC)) prom_dist++;
                    b_dist = Min(b_dist, prom_dist);
                }
            }
        }
    }

    if (w_dist < b_dist - 1) Add(e, WC, 0, 500);
    if (b_dist < w_dist - 1) Add(e, BC, 0, 500);
}


void cEngine::Add(eData *e, int sd, int mg_val, int eg_val) {

    e->mg[sd] += mg_val;
    e->eg[sd] += eg_val;
}

void cEngine::Add(eData *e, int sd, int val) {

    e->mg[sd] += val;
    e->eg[sd] += val;
}

void cEngine::AddPawns(eData *e, int sd, int mg_val, int eg_val) {

    e->mg_pawns[sd] += mg_val;
    e->eg_pawns[sd] += eg_val;
}

int cEngine::Interpolate(POS *p, eData *e) {

    int mg_tot = e->mg[WC] - e->mg[BC];
    int eg_tot = e->eg[WC] - e->eg[BC];
    int mg_phase = Min(p->phase, 24);
    int eg_phase = 24 - mg_phase;

    return (mg_tot * mg_phase + eg_tot * eg_phase) / 24;
}

void cEngine::EvaluateThreats(POS *p, eData *e, int sd) {

    int pc, sq, sc;
    int mg = 0;
    int eg = 0;
    int op = Opp(sd);

    U64 bb_undefended = p->cl_bb[op];
    U64 bb_threatened = bb_undefended & e->p_takes[sd];
    U64 bb_defended = bb_undefended & e->all_att[op];
    U64 bb_hanging = bb_undefended & ~e->p_takes[op];

    bb_undefended &= ~p->Pawns(op);
    bb_undefended &= ~e->all_att[sd];
    bb_undefended &= ~e->all_att[op];

    bb_hanging |= bb_threatened;     // piece attacked by our pawn isn't well defended
    bb_hanging &= e->all_att[sd];    // hanging piece has to be attacked
    bb_hanging &= ~p->Pawns(op);     // currently we don't evaluate threats against pawns

    bb_defended &= e->ev_att[sd];    // N, B, R attacks (pieces attacked by pawns are scored as hanging)
    bb_defended &= ~e->p_takes[sd];  // no defense against pawn attack
    bb_defended &= ~p->Pawns(op);    // currently we don't evaluate threats against pawns

    // hanging pieces (attacked and undefended, based on DiscoCheck)

    while (bb_hanging) {
        sq = BB.PopFirstBit(&bb_hanging);
        pc = TpOnSq(p, sq);
        sc = tp_value[pc] / 64;
        mg += 10 + sc;
        eg += 18 + sc;
    }

    // defended pieces under attack

    while (bb_defended) {
        sq = BB.PopFirstBit(&bb_defended);
        pc = TpOnSq(p, sq);
        sc = tp_value[pc] / 96;
        mg += 5 + sc;
        eg += 9 + sc;
    }

    // unattacked and undefended

    while (bb_undefended) {
        bb_undefended &= (bb_undefended - 1);
        mg += 5;
        eg += 9;
    }

    Add(e, sd, (Par.threats_weight * mg) / 100, (Par.threats_weight * eg) / 100);
}

#ifdef USE_RISKY_PARAMETER

// from Rodent Risky code by Roman T. Sovanyan

int cEngine::EvalScaleByDepth(POS *p, int ply, int eval) {

    int eval_adj = eval;

    //Correct self-side score by depth for human opponent

    if ((Par.riskydepth > 0)
    && (ply >= Par.riskydepth)
    && (Abs(eval) > Par.draw_score)
    && (Abs(eval) < 1000)) {

        eval_adj = (int)round(
                (eval < 0) == (p->side == Par.prog_side) ? (double)eval * (Glob.nodes > 100 ? 0.5 : 1) * Par.riskydepth / ply :
                                                           (double)eval * (Glob.nodes > 100 ?   2 : 1) * ply / Par.riskydepth
                             );

        if (eval_adj > 1000) eval_adj = 1000;
    }

    return eval_adj;
}
#endif

int cEngine::Evaluate(POS *p, eData *e) {

    // Try retrieving score from per-thread eval hashtable

    int addr = p->hash_key % EVAL_HASH_SIZE;

    if (EvalTT[addr].key == p->hash_key) {
        int sc = EvalTT[addr].score;
        return p->side == WC ? sc : -sc;
    }

    // Clear eval data

    e->mg[WC] = p->mg_sc[WC];
    e->mg[BC] = p->mg_sc[BC];
    e->eg[WC] = p->eg_sc[WC];
    e->eg[BC] = p->eg_sc[BC];

    // Init helper bitboards (pawn info)

    e->p_takes[WC] = BB.GetWPControl(p->Pawns(WC));
    e->p_takes[BC] = BB.GetBPControl(p->Pawns(BC));
    e->p_can_take[WC] = BB.FillNorth(e->p_takes[WC]);
    e->p_can_take[BC] = BB.FillSouth(e->p_takes[BC]);
    e->two_pawns_take[WC] = BB.GetDoubleWPControl(p->Pawns(WC));
    e->two_pawns_take[BC] = BB.GetDoubleBPControl(p->Pawns(BC));

    // Init or clear attack maps

    e->all_att[WC] = e->p_takes[WC] | BB.KingAttacks(KingSq(p, WC));
    e->all_att[BC] = e->p_takes[BC] | BB.KingAttacks(KingSq(p, BC));
    e->ev_att[WC] = 0ULL;
    e->ev_att[BC] = 0ULL;

    // Run all the evaluation subroutines

    EvaluateMaterial(p, e, WC);
    EvaluateMaterial(p, e, BC);
    EvaluatePieces(p, e, WC);
    EvaluatePieces(p, e, BC);
    EvaluatePawnStruct(p, e);
    EvaluatePassers(p, e, WC);
    EvaluatePassers(p, e, BC);
    EvaluateUnstoppable(e, p);
    EvaluateThreats(p, e, WC);
    EvaluateThreats(p, e, BC);
    Add(e, p->side, 14, 7); // tempo bonus

    // Evaluate patterns

    EvaluateKnightPatterns(p, e);
    EvaluateBishopPatterns(p, e);
    EvaluateKingPatterns(p, e);
    EvaluateCentralPatterns(p, e);

    // Add pawn score (which might come from hash)

    e->mg[WC] += e->mg_pawns[WC];
    e->mg[BC] += e->mg_pawns[BC];
    e->eg[WC] += e->eg_pawns[WC];
    e->eg[BC] += e->eg_pawns[BC];

    // Add asymmetric bonus for keeping certain type of pieces

    e->mg[Par.prog_side] += Par.keep_pc[Q] * p->cnt[Par.prog_side][Q];
    e->mg[Par.prog_side] += Par.keep_pc[R] * p->cnt[Par.prog_side][R];
    e->mg[Par.prog_side] += Par.keep_pc[B] * p->cnt[Par.prog_side][B];
    e->mg[Par.prog_side] += Par.keep_pc[N] * p->cnt[Par.prog_side][N];
    e->mg[Par.prog_side] += Par.keep_pc[P] * p->cnt[Par.prog_side][P];

    // Interpolate between midgame and endgame scores

    int score = Interpolate(p, e);

    // Material imbalance evaluation (based on Crafty)

    int minor_balance = p->cnt[WC][N] - p->cnt[BC][N] + p->cnt[WC][B] - p->cnt[BC][B];
    int major_balance = p->cnt[WC][R] - p->cnt[BC][R] + 2 * p->cnt[WC][Q] - 2 * p->cnt[BC][Q];

    int x = Max(major_balance + 4, 0);
    if (x > 8) x = 8;

    int y = Max(minor_balance + 4, 0);
    if (y > 8) y = 8;

    score += Par.imbalance[x][y];

    // Weakening: add pseudo-random value to eval score

    if (Par.eval_blur) {
        int rand_mod = (Par.eval_blur / 2) - (p->hash_key % Par.eval_blur);
        score += rand_mod;
    }

    // Special case code for KBN vs K checkmate

    score += CheckmateHelper(p);

    // Decrease score for drawish endgames

    int draw_factor = 64;
    if (score > 0) draw_factor = GetDrawFactor(p, WC);
    if (score < 0) draw_factor = GetDrawFactor(p, BC);
    score = (score * draw_factor) / 64;

    // Ensure that returned value doesn't exceed mate score

    score = Clip(score, MAX_EVAL);

    // Save eval score in the evaluation hash table

    EvalTT[addr].key = p->hash_key;
    EvalTT[addr].score = score;

    // Return score relative to the side to move

    return p->side == WC ? score : -score;
}
