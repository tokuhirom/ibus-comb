#include "../include/node.h"
#include "../include/system_lm.h"
#include "../include/user_language_model.h"
#include "../include/tinylisp.h"
#include <codecvt>
#include <memory>
#include <cassert>

#include "debug_log.h"

akaza::Node::Node(int start_pos, const std::wstring &yomi, const std::wstring &word, bool is_bos, bool is_eos) :
        start_pos_(start_pos),
        yomi_(yomi),
        word_(word),
        is_bos_(is_bos), is_eos_(is_eos) {
    if (word == L"__EOS__") {
        // return '__EOS__'  // わざと使わない。__EOS__ 考慮すると変換精度が落ちるので。。今は使わない。
        // うまく使えることが確認できれば、__EOS__/__EOS__ にする。
        this->key_ = L"__EOS__";
    } else {
        this->key_ = word + L"/" + yomi;
    }
    this->cost_ = 0;
    this->word_id_ = -1;
}

std::shared_ptr<akaza::Node> akaza::create_bos_node() {
    return std::make_shared<akaza::Node>(akaza::Node(-1, L"__BOS__", L"__BOS__", true, false));
}

std::shared_ptr<akaza::Node> akaza::create_eos_node(int start_pos) {
    return std::make_shared<akaza::Node>(akaza::Node(start_pos, L"__EOS__", L"__EOS__", false, true));
}

float akaza::Node::calc_node_cost(
        const akaza::UserLanguageModel &user_language_model,
        const akaza::SystemUnigramLM &ulm
) {
    std::wstring key = this->key_;
    std::optional<float> u = user_language_model.get_unigram_cost(key);
    if (u.has_value()) {
        return *u;
    }

    auto[word_id, score] = ulm.find_unigram(key);
    this->word_id_ = word_id;
    if (word_id != akaza::UNKNOWN_WORD_ID) {
        this->cost_ = score;
        return score;
    } else {
        // 労働者災害補償保険法 のように、システム辞書には wikipedia から採録されているが,
        // 言語モデルには採録されていない場合,漢字候補を先頭に持ってくる。
        if (this->word_.size() < this->yomi_.size()) {
            this->cost_ = ulm.get_default_cost_for_short();
        } else {
            this->cost_ = ulm.get_default_cost();
        }
        return this->cost_;
    }
}


static float calc_bigram_cost(const akaza::Node &prev_node,
                              const akaza::Node &next_node,
                              const akaza::UserLanguageModel &ulm,
                              const akaza::SystemBigramLM &system_bigram_lm) {
    // self → node で処理する。
    const auto &prev_key = prev_node.get_key();
    const auto &next_key = next_node.get_key();
    auto u = ulm.get_bigram_cost(prev_key, next_key);
    if (u.has_value()) {
        return *u;
    }

    const auto &id1 = prev_node.get_word_id();
    const auto &id2 = next_node.get_word_id();
    if (id1 == akaza::UNKNOWN_WORD_ID || id2 == akaza::UNKNOWN_WORD_ID) {
        return system_bigram_lm.get_default_score();
    }

    const auto score = system_bigram_lm.find_bigram(id1, id2);
    if (score != 0.0) {
        return score;
    } else {
        return system_bigram_lm.get_default_score();
    }
}

float akaza::Node::get_bigram_cost(const akaza::Node &next_node, const akaza::UserLanguageModel &ulm,
                                   const akaza::SystemBigramLM &system_bigram_lm) {
    auto next_node_key = next_node.get_key();
    auto search = bigram_cache_.find(next_node_key);
    if (search != bigram_cache_.cend()) {
        return search->second;
    } else {
        float cost = calc_bigram_cost(*this, next_node, ulm, system_bigram_lm);
        bigram_cache_[next_node_key] = cost;
        return cost;
    }
}

void akaza::Node::set_prev(std::shared_ptr<Node> &prev) {
    D(std::wcout << this->get_key() << ":" << this->start_pos_
                 << " -> " << prev->get_key() << ":" << prev->get_start_pos() << std::endl);
    assert(!(start_pos_ != 0 && prev->is_bos()));
    assert(this->start_pos_ != prev->start_pos_);
    this->prev_ = prev;
}

bool akaza::Node::operator==(akaza::Node const &node) {
    return this->word_ == node.word_ && this->yomi_ == node.yomi_ && this->start_pos_ == node.start_pos_;
}

bool akaza::Node::operator!=(akaza::Node const &node) {
    return this->word_ != node.word_ || this->yomi_ != node.yomi_ || this->start_pos_ != node.start_pos_;
}

std::wstring akaza::Node::surface(const akaza::tinylisp::TinyLisp &tinyLisp) const {
    if (!word_.empty() && word_[0] == '(') {
        return tinyLisp.run(word_);
    } else {
        return word_;
    }
}
