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

#include <cstdio>
#include <cstring>

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/time.h>
#endif

#include "rodent.h"

bool InputAvailable() {

#if defined(_WIN32) || defined(_WIN64)
    static bool init = false, pipe;
    static HANDLE inh;
    DWORD dw;

    if (!init) {
        init = true;
        inh = GetStdHandle(STD_INPUT_HANDLE);
        pipe = !GetConsoleMode(inh, &dw);
        if (!pipe) {
            SetConsoleMode(inh, dw & ~(ENABLE_MOUSE_INPUT | ENABLE_WINDOW_INPUT));
            FlushConsoleInputBuffer(inh);
        }
    }
    if (pipe) {
        if (!PeekNamedPipe(inh, NULL, 0, NULL, &dw, NULL))
            return true;
        return dw > 0;
    } else {
        GetNumberOfConsoleInputEvents(inh, &dw);
        return dw > 1;
    }
#else
    fd_set readfds;
    struct timeval tv;

    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    select(STDIN_FILENO + 1, &readfds, NULL, NULL, &tv);
    return FD_ISSET(STDIN_FILENO, &readfds);
#endif
}

int Clip(int sc, int lim) {

    if (sc < -lim) return -lim;
    if (sc > lim) return lim;
    return sc;
}

int GetMS() {

#if defined(_WIN32) || defined(_WIN64)
    return GetTickCount();
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000 + tv.tv_usec / 1000;
#endif
}

U64 Random64() {

    static U64 next = 1;

    next = next * 1103515245 + 12345;
    return next;
}

void POS::InitHashKey() {

    U64 key = 0;

    for (int i = 0; i < 64; i++)
        if (pc[i] != NO_PC)
            key ^= zob_piece[pc[i]][i];

    key ^= zob_castle[c_flags];
    if (ep_sq != NO_SQ)
        key ^= zob_ep[File(ep_sq)];

    if (side == BC)
        key ^= SIDE_RANDOM;

    hash_key = key;
}

void POS::InitPawnKey() {

    U64 key = 0;

    for (int i = 0; i < 64; i++) {
        if ((tp_bb[P] & SqBb(i)) || (tp_bb[K] & SqBb(i)))
            key ^= zob_piece[pc[i]][i];
    }

    pawn_key = key;
}

void PrintMove(int move) {

    printf("%s", MoveToStr(move));
}

char *MoveToStr(int move) {

    static char internalstring[6];
    MoveToStr(move, internalstring);
    return internalstring;
}

void MoveToStr(int move, char *move_str) {

    static const char prom_char[5] = "nbrq";

    move_str[0] = File(Fsq(move)) + 'a';
    move_str[1] = Rank(Fsq(move)) + '1';
    move_str[2] = File(Tsq(move)) + 'a';
    move_str[3] = Rank(Tsq(move)) + '1';
    move_str[4] = '\0';

    // Bugfix by Dave Kaye for compatibility with Knights GUI (Linux) and UCI specs
    // (needed if a GUI forces the engine to analyse in checkmate/stalemate position)

    if (strcmp(move_str, "a1a1") == 0) {
        strcpy(move_str, "0000");
    }

    if (IsProm(move)) {
        move_str[4] = prom_char[(move >> 12) & 3];
        move_str[5] = '\0';
    }
}

int StrToMove(POS *p, char *move_str) {

    int from = Sq(move_str[0] - 'a', move_str[1] - '1');
    int to   = Sq(move_str[2] - 'a', move_str[3] - '1');
    int type = NORMAL;

    // change move type if necessary

    if (TpOnSq(p, from) == K && Abs(to - from) == 2)
        type = CASTLE;
    else if (TpOnSq(p, from) == P) {
        if (to == p->ep_sq)
            type = EP_CAP;
        else if (Abs(to - from) == 16)
            type = EP_SET;
        else if (move_str[4] != '\0')

            // record promotion piece

            switch (move_str[4]) {
                case 'n':
                    type = N_PROM;
                    break;
                case 'b':
                    type = B_PROM;
                    break;
                case 'r':
                    type = R_PROM;
                    break;
                case 'q':
                    type = Q_PROM;
                    break;
            }
    }

    // return move

    return (type << 12) | (to << 6) | from;
}

void PvToStr(int *pv, char *pv_str) {

    int *movep;

    pv_str[0] = '\0';
    for (movep = pv; *movep; movep++) {
        strcat(pv_str, MoveToStr(*movep));
        strcat(pv_str, " ");
    }
}

void BuildPv(int *dst, int *src, int move) {

    *dst++ = move;
    while ((*dst++ = *src++))
        ;
}

void WasteTime(int miliseconds) {

#if defined(_WIN32) || defined(_WIN64)
    Sleep(miliseconds);
#else
    usleep(miliseconds * 1000);
#endif
}

#if defined(_WIN32) || defined(_WIN64)
// constexpr for detecting relative paths
constexpr bool relative = _BOOKSPATH[1] != L':' || _PERSONALITIESPATH[1] != L':';
bool ChDir(const wchar_t *new_path) {
    if (relative) {
        wchar_t exe_path[1024];

        // getting the current executable location ...
        GetModuleFileNameW(NULL, exe_path, sizeof(exe_path)/sizeof(exe_path[0])); *(wcsrchr(exe_path, '\\') + 1) = L'\0';

        // go there ...
        printf_debug("go to \'%ls\'\n", exe_path);
        SetCurrentDirectoryW(exe_path);
    }
    // and now go further, it's for relative paths
    printf_debug("go to \'%ls\'\n", new_path);
    return SetCurrentDirectoryW(new_path);
}
#else
// constexpr for detecting relative paths
constexpr bool relative = _BOOKSPATH[0] != '/' || _PERSONALITIESPATH[0] != '/';
bool ChDir(const char *new_path) {
    if (relative) {
        static bool first_run = true; static char cwd_storage[1024];

        if (first_run) {        // try to get executable path or save the init location
            ssize_t size = readlink("/proc/self/exe", cwd_storage, sizeof(cwd_storage));
            if (size != 0)
                *(strrchr(cwd_storage, '/') + 1) = '\0';
            else
                getcwd(cwd_storage, sizeof(cwd_storage)/sizeof(cwd_storage[0]));
            first_run = false;
        }
        else
            chdir(cwd_storage); // go to the init location, it's for relative paths
        printf_debug("go to \'%s\'\n", cwd_storage);
    }
    printf_debug("go to \'%s\'\n", new_path);
    return chdir(new_path) == 0;
}
#endif
