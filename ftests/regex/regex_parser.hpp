/*
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *   Product name: redemption, a FLOSS RDP proxy
 *   Copyright (C) Wallix 2010-2013
 *   Author(s): Christophe Grosjean, Raphael Zhou, Jonathan Poelen, Meng Tan
 */

#ifndef REDEMPTION_FTESTS_REGEX_REGEX_PARSE_HPP
#define REDEMPTION_FTESTS_REGEX_REGEX_PARSE_HPP

#include "regex_automate.hpp"

namespace re {

    inline State ** c2range(State ** pst, State * eps,
                            char_int l1, char_int r1,
                            char_int l2, char_int r2)
    {
        *pst = new_split(new_range(l1, r1, eps),
                         new_range(l2, r2, eps)
                        );
        return &eps->out1;
    }

    inline State ** c2range(State ** pst, State * eps,
                            char_int l1, char_int r1,
                            char_int l2, char_int r2,
                            char_int l3, char_int r3)
    {
        *pst = new_split(new_range(l1, r1, eps),
                         new_split(new_range(l2, r2, eps),
                                   new_range(l3, r3, eps)
                                  )
                        );
        return &eps->out1;
    }

    inline State ** c2range(State ** pst, State * eps,
                            char_int l1, char_int r1,
                            char_int l2, char_int r2,
                            char_int l3, char_int r3,
                            char_int l4, char_int r4)
    {
        *pst = new_split(new_range(l1, r1, eps),
                         new_split(new_range(l2, r2, eps),
                                   new_split(new_range(l3, r3, eps),
                                             new_range(l4, r4, eps)
                                   )
                         )
        );
        return &eps->out1;
    }

    inline State ** c2range(State ** pst, State * eps,
                            char_int l1, char_int r1,
                            char_int l2, char_int r2,
                            char_int l3, char_int r3,
                            char_int l4, char_int r4,
                            char_int l5, char_int r5)
    {
        *pst = new_split(new_range(l1, r1, eps),
                         new_split(new_range(l2, r2, eps),
                                   new_split(new_range(l3, r3, eps),
                                             new_split(new_range(l4, r4, eps),
                                                       new_range(l5, r5, eps)
                                             )
                                   )
                         )
        );
        return &eps->out1;
    }

    inline State ** ident_D(State ** pst, State * eps) {
        return c2range(pst, eps, 0,'0'-1, '9'+1,-1u);
    }

    inline State ** ident_w(State ** pst, State * eps) {
        return c2range(pst, eps, 'a','z', 'A','Z', '0','9', '_', '_');
    }

    inline State ** ident_W(State ** pst, State * eps) {
        return c2range(pst, eps, 0,'0'-1, '9'+1,'A'-1, 'Z'+1,'_'-1, '_'+1,'a'-1, 'z'+1,-1u);
    }

    inline State ** ident_s(State ** pst, State * eps) {
        return c2range(pst, eps, ' ',' ', '\t','\v');
    }

    inline State ** ident_S(State ** pst, State * eps) {
        return c2range(pst, eps, 0,'\t'-1, '\v'+1,' '-1, ' '+1,-1u);
    }

    inline State ** c2st(State ** pst, char_int c)
    {
        switch (c) {
            case 'd': return &(*pst = new_range('0','9'))->out1;
            case 'D': return ident_D(pst, new_epsilone());
            case 'w': return ident_w(pst, new_epsilone());
            case 'W': return ident_W(pst, new_epsilone());
            case 's': return ident_s(pst, new_epsilone());
            case 'S': return ident_S(pst, new_epsilone());
            case 'n': return &(*pst = new_character('\n'))->out1;
            case 't': return &(*pst = new_character('\t'))->out1;
            case 'r': return &(*pst = new_character('\r'))->out1;
            case 'v': return &(*pst = new_character('\v'))->out1;
            default : return &(*pst = new_character(c))->out1;
        }
    }

    inline const char * check_interval(char_int a, char_int b)
    {
        bool valid = ('0' <= a && a <= '9' && '0' <= b && b <= '9')
                  || ('a' <= a && a <= 'z' && 'a' <= b && b <= 'z')
                  || ('A' <= a && a <= 'Z' && 'A' <= b && b <= 'Z');
        return (valid && a <= b) ? 0 : "range out of order in character class";
    }

    inline State ** st_compilechar(State ** pst, utf_consumer & consumer, char_int c, const char * & msg_err)
    {
        if (c == '\\' && consumer.valid()) {
            return c2st(pst, consumer.bumpc());
        }

        if (c == '[') {
            State * eps = new_epsilone();
            State st(0, 0, 0, eps, eps);
            State * cst = &st;
            bool reverse_result = false;
            std::vector<char_int> characters;
            if (consumer.valid() && (c = consumer.bumpc()) != ']') {
                if (c == '^') {
                    reverse_result = true;
                    c = consumer.bumpc();
                }
                if (c == '-') {
                    characters.push_back('-');
                    c = consumer.bumpc();
                }
                const unsigned char * cs = consumer.s;
                while (consumer.valid() && c != ']') {
                    const unsigned char * p = consumer.s;
                    char_int prev_c = c;
                    while (c != ']' && c != '-') {
                        if (c == '\\') {
                            char_int cc = consumer.bumpc();
                            if (cst->out1 != eps) {
                                cst->out2 = new_split(eps, eps);
                                cst = cst->out2;
                            }
                            switch (cc) {
                                case 'd':
                                case 'D':
                                    if ((cc == 'd') != reverse_result) {
                                        cst->out1 = new_range('0','9', eps);
                                    }
                                    else {
                                        ident_D(&cst->out1, eps);
                                    }
                                    break;
                                case 'W':
                                case 'w':
                                    if ((cc == 'w') != reverse_result) {
                                        ident_w(&cst->out1, eps);
                                    }
                                    else {
                                        ident_W(&cst->out1, eps);
                                    }
                                    break;
                                case 's':
                                case 'S':
                                    if ((cc == 's') != reverse_result) {
                                        ident_s(&cst->out1, eps);
                                    }
                                    else {
                                        ident_S(&cst->out1, eps);
                                    }
                                    break;
                                case 'n': characters.push_back('\n'); break;
                                case 't': characters.push_back('\t'); break;
                                case 'r': characters.push_back('\r'); break;
                                case 'v': characters.push_back('\v'); break;
                                default : characters.push_back(cc); break;
                            }
                        }
                        else {
                            characters.push_back(c);
                        }

                        if ( ! consumer.valid()) {
                            break;
                        }

                        prev_c = c;
                        c = consumer.bumpc();
                    }

                    if (c == '-') {
                        if (cs == consumer.s) {
                            characters.push_back('-');
                        }
                        else if (!consumer.valid()) {
                            msg_err = "missing terminating ]";
                            return 0;
                        }
                        else if (consumer.getc() == ']') {
                            characters.push_back('-');
                            consumer.bumpc();
                        }
                        else if (consumer.s == p) {
                            characters.push_back('-');
                        }
                        else {
                            c = consumer.bumpc();
                            if ((msg_err = check_interval(prev_c, c))) {
                                return 0;
                            }
                            if (characters.size()) {
                                characters.pop_back();
                            }
                            if (cst->out1 != eps) {
                                cst->out2 = new_split(eps, eps);
                                cst = cst->out2;
                            }
                            cst->out1 = new_range(prev_c, c);
                            cs = consumer.s;
                            if (consumer.valid()) {
                                c = consumer.bumpc();
                            }
                        }
                    }
                }
            }

            if (!characters.empty()) {
                std::sort(characters.begin(), characters.end());
                typedef std::vector<char_int>::iterator iterator;
                iterator first = characters.begin();
                iterator last = characters.end();
                char_int cl = *first;
                while (++first != last && (*first == cl || *(first+1) == cl))
                    ;
                State * stc = new_range(cl, *(first-1), eps);
                while (first != last) {
                    char_int cl = *first;
                    while (++first != last && (*first == cl || *(first+1) == cl))
                        ;
                    stc = new_split(new_range(cl, *(first-1), eps), stc);
                }

                if (cst->out1 == eps) {
                    cst->out1 = stc;
                }
                else {
                    cst->out1 = new_split(stc, cst->out1);
                }
            }

            if (c != ']') {
                msg_err = "missing terminating ]";
                StatesWrapper w(st.out1); //quick free
                return pst;
            }

            *pst = st.out1;
            return &eps->out1;
        }

        return &(*pst = (c == '.') ? new_any() : new_character(c))->out1;
    }

    inline bool is_range_repetition(const char * s)
    {
        const char * begin = s;
        while (*s && '0' <= *s && *s <= '9') {
            ++s;
        }
        if (begin == s || !*s || (*s != ',' && *s != '}')) {
            return false;
        }
        if (*s == '}') {
            return true;
        }
        begin = ++s;
        while (*s && '0' <= *s && *s <= '9') {
            ++s;
        }
        return *s && *s == '}';
    }

    inline bool is_meta_char(utf_consumer & consumer, char_int c)
    {
        return c == '*' || c == '+' || c == '?' || c == '|' || c == '(' || c == ')' || c == '^' || c == '$' || (c == '{' && is_range_repetition(consumer.str()));
    }


    struct ContextClone {
        std::vector<State*> sts;
        std::vector<State*> sts2;
        const State * st;
        size_t p2;
        const bool definied_out1;
        const bool definied_out2;

        ContextClone(const State * st_base)
        : sts()
        , sts2()
        , st(st_base)
        , definied_out1(st->out1)
        , definied_out2(st->out2)
        {
            if (this->definied_out1) {
                append_state(st->out1, this->sts);
            }
            if (this->definied_out2) {
                this->p2 = this->sts.size();
                append_state(st->out2, this->sts);
            }
            this->sts2.reserve(this->sts.size());
        }

        State * clone()
        {
            State * replicant = this->copy(this->st);
            if (this->definied_out1 || this->definied_out2) {
                typedef std::vector<State*>::iterator iterator;
                iterator last = this->sts.end();
                for (iterator first = this->sts.begin(), first2 = sts2.begin(); last != this->sts.end(); ++first, ++first2) {
                    *first2 = this->copy(*first);
                    (*first)->num = 0;
                }
                for (iterator first = this->sts.begin(), first2 = sts2.begin(); last != this->sts.end(); ++first, ++first2) {
                    if ((*first)->out1) {
                        (*first2)->out1 = this->sts2[std::find(this->sts.begin(), this->sts.end(), (*first)->out1) - this->sts.begin()];
                    }
                    if ((*first)->out2) {
                        (*first2)->out2 = this->sts2[std::find(this->sts.begin(), this->sts.end(), (*first)->out2) - this->sts.begin()];
                    }
                }
                if (this->definied_out1) {
                    replicant->out1 = this->sts2[0];
                }
                if (this->definied_out2) {
                    replicant->out2 = this->sts2[p2];
                }
            }
            return replicant;
        }

        static State * copy(const State * st) {
            return new State(st->type, st->range.l, st->range.r);
        }
    };

    typedef std::pair<State*, State**> IntermendaryState;

    struct FinishFreeList
    {
        std::vector<State *> vec;

        State * new_finish()
        {
            if (this->vec.empty()) {
                return ::re::new_finish();
            }
            State * ret = this->vec.back();
            this->vec.pop_back();
            return ret;
        }

        void free_finish(State * st) {
            this->vec.push_back(st);
        }

        ~FinishFreeList()
        {
            std::for_each(this->vec.begin(), this->vec.end(), StateDeleter());
        }
    };

    inline IntermendaryState intermendary_st_compile(utf_consumer & consumer,
                                                     FinishFreeList & ffl,
                                                     const char * & msg_err,
                                                     int recusive = 0/*, bool ismatch = true*/)
    {
        struct FreeState {
            static IntermendaryState invalide(State& st)
            {
                StatesWrapper w(st.out1); //quick free
                return IntermendaryState(0,0);
            }
        };

        struct selected {
            static State ** next_pst(FinishFreeList & ffl, State ** pst)
            {
                if (*pst) {
                    if ((*pst)->is_finish()) {
                        ffl.free_finish(*pst);
                    }
                    else {
                        pst = &(*pst)->out1;
                    }
                }
                return pst;
            }
        };

        State st(EPSILONE);
        State ** pst = &st.out1;
        State ** spst = pst;
        State * bst = &st;
        State * eps = 0;

        char_int c = consumer.bumpc();

        while (c) {
            /**///std::cout << "c: " << (c) << std::endl;
            if (c == '^' || c == '$') {
                pst = selected::next_pst(ffl, pst);
                *pst = c == '^' ? new_begin() : new_last();

                if ((c = consumer.bumpc()) && !is_meta_char(consumer, c)) {
                    pst = &(*pst)->out1;
                }
                continue;
            }

            if (!is_meta_char(consumer, c)) {
                pst = selected::next_pst(ffl, pst);
                do {
                    spst = pst;
                    if (!(pst = st_compilechar(pst, consumer, c, msg_err))) {
                        return FreeState::invalide(st);
                    }
                    if (is_meta_char(consumer, c = consumer.bumpc())) {
                        break;
                    }
                } while (c);
            }
            else {
                switch (c) {
                    case '?': {
                        *pst = new_epsilone();
                        *spst = new_split(*pst, *spst);
                        spst = pst;
                        break;
                    }
                    case '*':
                        *spst = new_split(ffl.new_finish(), *spst);
                        *pst = *spst;
                        pst = &(*spst)->out1;
                        spst = pst;
                        break;
                    case '+':
                        *pst = new_split(ffl.new_finish(), *spst);
                        spst = pst;
                        pst = &(*pst)->out1->out1;
                        break;
                    case '|':
                        if (!eps) {
                            eps = new_epsilone();
                        }
                        *pst = eps;
                        bst = new_split(bst == &st ? st.out1 : bst);
                        pst = &bst->out2;
                        break;
                    case '{': {
                        /**///std::cout << ("{") << std::endl;
                        char * end = 0;
                        unsigned m = strtoul(consumer.str(), &end, 10);
                        /**///std::cout << ("end ") << *end << std::endl;
                        /**///std::cout << "m: " << (m) << std::endl;
                        if (*end != '}') {
                            /**///std::cout << ("reste") << std::endl;
                            if (*(end+1) == '}') {
                                /**///std::cout << ("infini") << std::endl;
                                if (m == 1) {
                                    (*pst)->out1 = new_split(0, *pst);
                                    pst = &(*pst)->out1->out1;
                                }
                                else if (m) {
                                    ContextClone cloner(*pst);
                                    pst = selected::next_pst(ffl, pst);
                                    while (--m) {
                                        *pst = cloner.clone();
                                        pst = &(*pst)->out1;
                                    }
                                    *pst = new_split(0, cloner.clone());
                                    (*pst)->out2->out1 = *pst;
                                    pst = &(*pst)->out1;
                                }
                                else {
                                    *pst = new_split(0, *pst);
                                    (*pst)->out2->out1 = *pst;
                                    pst = &(*pst)->out1;
                                }
                            }
                            else {
                                /**///std::cout << ("range") << std::endl;
                                unsigned n = strtoul(end+1, &end, 10);
                                if (m > n || (0 == m && 0 == n)) {
                                    msg_err = "numbers out of order in {} quantifier";
                                    return FreeState::invalide(st);
                                }
                                /**///std::cout << "n: " << (n) << std::endl;
                                n -= m;
                                if (n > 50) {
                                    msg_err = "numbers too large in {} quantifier";
                                    return FreeState::invalide(st);
                                }
                                if (0 == m) {
                                    --end;
                                    /**///std::cout << ("m = 0") << std::endl;
                                    if (n != 1) {
                                        /**///std::cout << ("n != 1") << std::endl;
                                        ContextClone cloner(*pst);
                                        *pst = new_split(0, *pst);
                                        pst = &(*pst)->out1;

                                        while (--n) {
                                            *pst = cloner.clone();
                                            *pst = new_split(0, *pst);
                                            pst = &(*pst)->out1;
                                        }
                                    }
                                }
                                else {
                                    --end;
                                    ContextClone cloner(*pst);
                                    pst = selected::next_pst(ffl, pst);
                                    while (--m) {
                                        *pst = cloner.clone();
                                        pst = &(*pst)->out1;
                                    }

                                    State * split = new_split();
                                    while (n--) {
                                        *pst = new_split(0, split);
                                        (*pst)->out1 = cloner.clone();
                                        (*pst)->out1->out1 = split;
                                        pst = &(*pst)->out2;
                                    }
                                    pst = &split->out1;
                                }
                            }
                        }
                        else if (0 == m) {
                            msg_err = "numbers is 0 in {}";
                            return FreeState::invalide(st);
                        }
                        else {
                            /**///std::cout << ("fixe ") << m << std::endl;
                            ContextClone cloner(*pst);
                            pst = selected::next_pst(ffl, pst);
                            while (--m) {
                                /**///std::cout << ("clone") << std::endl;
                                *pst = cloner.clone();
                                pst = &(*pst)->out1;
                            }
                            end -= 1;
                        }
                        consumer.str(end + 1 + 1);
                        /**///std::cout << "'" << (*consumer.s) << "'" << std::endl;
                        break;
                    }
                    case ')':
                        if (0 == recusive) {
                            msg_err = "unmatched parentheses";
                            return FreeState::invalide(st);
                        }

                        pst = selected::next_pst(ffl, pst);
                        *pst = eps;
                        pst = &(*pst)->out1;
                        return IntermendaryState(bst == &st ? st.out1 : bst, pst);
                        break;
                    default:
                        return FreeState::invalide(st);
                    case '(':
                        if (*consumer.s == '?' && *(consumer.s+1) == ':') {
                            if (!*consumer.s || !(*consumer.s+1) || !(*consumer.s+2)) {
                                msg_err = "unmatched parentheses";
                                return FreeState::invalide(st);
                            }
                            consumer.s += 2;
                            IntermendaryState intermendary = intermendary_st_compile(consumer, ffl, msg_err, recusive+1);
                            if (intermendary.first) {
                                pst = selected::next_pst(ffl, pst);
                                *pst= intermendary.first;
                                pst = intermendary.second;
                            }
                            else if (0 == intermendary.second) {
                                return FreeState::invalide(st);
                            }
                            break;
                        }
                        IntermendaryState intermendary = intermendary_st_compile(consumer, ffl, msg_err, recusive+1);
                        if (intermendary.first) {
                            pst = selected::next_pst(ffl, pst);
                            *pst = new_cap_open(intermendary.first);
                            pst = intermendary.second;
                            *pst = new_cap_close();
                            pst = &(*pst)->out1;
                        }
                        else if (0 == intermendary.second) {
                            return FreeState::invalide(st);
                        }
                        break;
                }
                c = consumer.bumpc();
            }
        }

        if (0 != recusive) {
            msg_err = "unmatched parentheses";
            return FreeState::invalide(st);
        }
        return IntermendaryState(bst == &st ? st.out1 : bst, pst);
    }

    inline bool st_compile(StatesWrapper & stw, const char * s, const char * * msg_err = 0, size_t * pos_err = 0)
    {
        const char * err = 0;
        utf_consumer consumer(s);
        FinishFreeList ffl;
        State * st = intermendary_st_compile(consumer, ffl, err).first;
        if (err) {
            if (msg_err) {
                *msg_err = err;
            }
            if (pos_err) {
                *pos_err = consumer.str() - s;
            }
            return false;
        }

        stw.reset(st);
        state_list_t::iterator last = stw.states.end();
        bool has_epsilone = false;
        for (state_list_t::iterator first = stw.states.begin(); first != last; ++first) {
            (*first)->num = 0;
            has_epsilone = has_epsilone || (*first)->is_epsilone();
        }

        if (has_epsilone) {
            for (state_list_t::iterator first = stw.states.begin(); first != last; ++first) {
                State * nst = (*first)->out1;
                while (nst && nst->is_epsilone()) {
                    if (nst->num != -3u) {
                        nst->num = -3u;
                    }
                    nst = nst->out1;
                }
                (*first)->out1 = nst;
            }
            state_list_t::iterator first = stw.states.begin();
            while (first != last && (*first)->out1 && !(*first)->out1->is_epsilone()) {
                ++first;
            }
            state_list_t::iterator result = first;
            for (; first != last; ++first) {
                if ((*first)->out1 && (*first)->out1->is_epsilone()) {
                    delete (*first)->out1;
                }
                else {
                    *result = *first;
                    ++result;
                }
            }
            stw.states.resize(result - stw.states.begin());
        }

        stw.init_nums();

        return true;
    }
}

#endif