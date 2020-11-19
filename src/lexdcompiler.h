#ifndef __LEXDCOMPILER__
#define __LEXDCOMPILER__

#include "icu-iter.h"

#include <lttoolbox/transducer.h>
#include <lttoolbox/alphabet.h>
#include <unicode/ustdio.h>
#include <unicode/unistr.h>

#include <map>
#include <string>
#include <fstream>
#include <iostream>
#include <vector>
#include <set>

using namespace std;
using namespace icu;

struct string_ref {
  unsigned int i;
  string_ref() : i(0) {}
  explicit string_ref(unsigned int _i) : i(_i) {}
  explicit operator unsigned int() const { return i; }
  bool operator == (string_ref other) const { return i == other.i; }
  bool operator != (string_ref other) const { return !(*this == other); }
  bool operator < (string_ref other) const { return i < other.i; }
  bool operator !() const { return empty(); }
  string_ref operator || (string_ref other) const {
    return i ? *this : other;
  }
  bool empty() const { return i == 0; }
  bool valid() const { return i != 0; }
};

template<>
struct std::hash<string_ref> {
  size_t operator()(const string_ref &t) const
  {
    return std::hash<unsigned int>()(t.i);
  }
};

template<typename T>
bool subset(const set<T> &xs, const set<T> &ys)
{
  if(xs.size() > ys.size())
    return false;
  for(auto x: xs)
    if(ys.find(x) == ys.end())
      return false;
  return true;
}

template<typename T>
bool subset_strict(const set<T> &xs, const set<T> &ys)
{
  if(xs.size() >= ys.size())
    return false;
  return subset(xs, ys);
}

template<typename T>
set<T> unionset(const set<T> &xs, const set<T> &ys)
{
  set<T> u = xs;
  unionset_inplace(u, ys);
  return u;
}

template<typename T>
void unionset_inplace(set<T> &xs, const set<T> &ys)
{
  xs.insert(ys.begin(), ys.end());
  return;
}

template<typename T>
set<T> intersectset(const set<T> &xs, const set<T> &ys)
{
  set<T> i = xs;
  for(auto x: xs)
    if(ys.find(x) == ys.end())
      i.erase(x);
  return i;
}

template<typename T>
set<T> subtractset(const set<T> &xs, const set<T> &ys)
{
  set<T> i = xs;
  subtractset_inplace(i, ys);
  return i;
}

template<typename T>
void subtractset_inplace(set<T> &xs, const set<T> &ys)
{
  for(const auto &y: ys)
    xs.erase(y);
}

struct lex_token_t;

struct token_t {
  string_ref name;
  unsigned int part;
  bool operator<(const token_t &t) const
  {
    return name < t.name || (name == t.name &&  part < t.part);
  }
  bool operator==(const token_t &t) const
  {
    return name == t.name && part == t.part;
  }
};

struct trans_sym_t {
  int i;
  trans_sym_t() : i(0) {}
  explicit trans_sym_t(int _i) : i(_i) {}
  explicit operator int() const { return i; }
  bool operator == (trans_sym_t other) const { return i == other.i; }
  bool operator < (trans_sym_t other) const { return i < other.i; }
  trans_sym_t operator || (trans_sym_t other) const {
    return i ? *this : other;
  }
};

struct lex_token_t {
  vector<trans_sym_t> symbols;
  bool operator ==(const lex_token_t &other) const { return symbols == other.symbols; }
};

struct lex_seg_t {
  lex_token_t left, right;
  set<string_ref> tags;
  bool operator == (const lex_seg_t &t) const
  {
    return left == t.left && right == t.right && tags == t.tags;
  }
};

enum RepeatMode
{
  Optional = 1,
  Repeated = 2,

  Normal = 0,
  Question = 1,
  Plus = 2,
  Star = 3
};

struct pattern_element_t {
  token_t left, right;
  set<string_ref> tags, negtags;
  RepeatMode mode;

  bool operator<(const pattern_element_t& o) const
  {
    return left < o.left || (left == o.left && right < o.right) || (left == o.left && right == o.right && mode < o.mode) || (left == o.left && right == o.right && mode == o.mode && tags < o.tags) || (left == o.left && right == o.right && mode == o.mode && tags == o.tags && negtags < o.negtags);
  }

  bool operator==(const pattern_element_t& o) const
  {
    return left == o.left && right == o.right && mode == o.mode && tags == o.tags && negtags == o.negtags;
  }

  bool compatible(const lex_seg_t &tok) const;

  void addTags(const pattern_element_t& tok)
  {
    tags.insert(tok.tags.begin(), tok.tags.end());
  }
  void addNegTags(const pattern_element_t& tok)
  {
    negtags.insert(tok.negtags.begin(), tok.negtags.end());
  }
  void clearTags()
  {
    tags.clear();
    negtags.clear();
  }
};

typedef vector<pattern_element_t> pattern_t;
typedef vector<lex_seg_t> entry_t;
typedef int line_number_t;

enum FlagDiacriticType
{
  Unification,
  Positive,
  Negative,
  Require,
  Disallow,
  Clear
};

class LexdCompiler
{
private:
  bool shouldAlign;
  bool shouldCompress;
  bool tagsAsFlags;
  bool shouldHypermin;
  bool tagsAsMinFlags;

  map<UnicodeString, string_ref> name_to_id;
  vector<UnicodeString> id_to_name;

  const UnicodeString &name(string_ref r) const;

  map<string_ref, vector<entry_t>> lexicons;
  // { id => [ ( line, [ pattern ] ) ] }
  map<string_ref, vector<pair<line_number_t, pattern_t>>> patterns;
  map<pattern_element_t, Transducer*> patternTransducers;
  map<pattern_element_t, Transducer*> lexiconTransducers;
  map<pattern_element_t, vector<Transducer*>> entryTransducers;
  map<string_ref, set<string_ref>> flagsUsed;
  map<pattern_element_t, pair<int, int>> transducerLocs;
  map<string_ref, bool> lexiconFreedom;

  UFILE* input;
  bool inLex;
  bool inPat;
  vector<entry_t> currentLexicon;
  set<string_ref> currentLexicon_tags;
  string_ref currentLexiconId;
  unsigned int currentLexiconPartCount;
  string_ref currentPatternId;
  line_number_t lineNumber;
  bool doneReading;
  unsigned int anonymousCount;
  unsigned int transitionCount;

  Transducer* hyperminTrans;

  string_ref left_sieve_name;
  string_ref right_sieve_name;
  vector<pattern_element_t> left_sieve_tok;
  vector<pattern_element_t> right_sieve_tok;

  void die(const wstring & msg);
  void finishLexicon();
  string_ref internName(const UnicodeString& name);
  string_ref checkName(UnicodeString& name);
  RepeatMode readModifier(char_iter& iter);
  void readTags(char_iter& iter, UnicodeString& line, set<string_ref>* tags, set<string_ref>* negtags);
  lex_seg_t processLexiconSegment(char_iter& iter, UnicodeString& line, unsigned int part_count);
  token_t readToken(char_iter& iter, UnicodeString& line);
  pattern_element_t readPatternElement(char_iter& iter, UnicodeString& line);
  void processPattern(char_iter& iter, UnicodeString& line);
  void processNextLine();

  bool isLexiconToken(const pattern_element_t& tok);
  vector<int> determineFreedom(pattern_t& pat);
  map<string_ref, unsigned int> matchedParts;
  void applyMode(Transducer* trans, RepeatMode mode);
  void insertEntry(Transducer* trans, const lex_seg_t &seg);
  void appendLexicon(string_ref lexicon_id, const vector<entry_t> &to_append);
  Transducer* getLexiconTransducer(pattern_element_t tok, unsigned int entry_index, bool free);
  void buildPattern(int state, Transducer* t, const pattern_t& pat, vector<int> is_free, unsigned int pos);
  Transducer* buildPattern(const pattern_element_t &tok);
  Transducer* buildPatternWithFlags(const pattern_element_t &tok, int pattern_start_state);
  trans_sym_t alphabet_lookup(const UnicodeString &symbol);
  trans_sym_t alphabet_lookup(trans_sym_t l, trans_sym_t r);

  int insertPreTags(Transducer* t, int state, set<string_ref>& tags, set<string_ref>& negtags);
  int insertPostTags(Transducer* t, int state, set<string_ref>& tags, set<string_ref>& negtags);
  void encodeFlag(UnicodeString& str, int flag);
  trans_sym_t getFlag(FlagDiacriticType type, string_ref flag, unsigned int value);
  Transducer* getLexiconTransducerWithFlags(pattern_element_t& tok, bool free);

  void buildAllLexicons();
  int buildPatternSingleLexicon(pattern_element_t tok, int start_state);

public:
  LexdCompiler();
  ~LexdCompiler();
  Alphabet alphabet;
  void setShouldAlign(bool val)
  {
    shouldAlign = val;
  }
  void setShouldCompress(bool val)
  {
    shouldCompress = val;
  }
  void setTagsAsFlags(bool val)
  {
    tagsAsFlags = val;
  }
  void setShouldHypermin(bool val)
  {
    shouldHypermin = val;
  }
  Transducer* buildTransducer(bool usingFlags);
  Transducer* buildTransducerSingleLexicon();
  void readFile(UFILE* infile);
  void printStatistics() const;
};

#endif
