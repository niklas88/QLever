// Copyright 2011, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Authors: Björn Buchhold <buchholb>,
//          Johannes Kalmbach<joka921> (johannes.kalmbach@gmail.com)
//

#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "../global/Constants.h"
#include "../global/Id.h"
#include "../util/Exception.h"
#include "../util/HashMap.h"
#include "../util/HashSet.h"
#include "../util/Log.h"
#include "../util/StringUtils.h"
#include "./CompressedString.h"
#include "ExternalVocabulary.h"

using std::string;
using std::vector;

template <class StringType>
struct AccessReturnTypeGetter {};

template <>
struct AccessReturnTypeGetter<string> {
  using type = const string&;
};
template <>
struct AccessReturnTypeGetter<CompressedString> {
  using type = string;
};

template <class StringType>
using AccessReturnType_t = typename AccessReturnTypeGetter<StringType>::type;

struct IdRange {
  IdRange() : _first(), _last() {}

  IdRange(Id first, Id last) : _first(first), _last(last) {}

  Id _first;
  Id _last;
};

//! Stream operator for convenience.
inline std::ostream& operator<<(std::ostream& stream, const IdRange& idRange) {
  return stream << '[' << idRange._first << ", " << idRange._last << ']';
}

// simple class for members of a prefix compression codebook
struct Prefix {
  Prefix() = default;
  Prefix(char prefix, const string& fulltext)
      : _prefix(prefix), _fulltext(fulltext) {}

  char _prefix;
  string _fulltext;
};

// forward declaration for PrefixComparator
template <class StringType>
class Vocabulary;

template <class S>
class PrefixComparator {
 public:
  explicit PrefixComparator(size_t prefixLength, const Vocabulary<S>* vocab)
      : _prefixLength(prefixLength), _vocab(vocab) {}

  bool operator()(const string& lhs, const string& rhs) const;
  bool operator()(const CompressedString& lhs, const string& rhs) const;
  bool operator()(const string& lhs, const CompressedString& rhs) const;

 private:
  const size_t _prefixLength;
  const Vocabulary<S>* _vocab;
};

/**
 * \brief Comparator for std::strings that optionally supports
 * case-insensitivity.
 *
 * If the constructor is called with ignoreCase=false it is an ordinary string
 * compare using std::less<std::string>.
 * If ignoreCase=True the operator behaves as follows:
 *
 * - the inputs can either be Literals or non-literals like IRIs
 *   In case the type is different, return the standard ordering to keep
 *   literals and IRIs disjoint in the order
 *
 * - split both literals "vaLue"@lang into its vaLue and possibly empty
 *   langtag. For IRIs, the value is the complete string and the langtag is
 * empty
 * - compare the strings according to the lowercase version of their value
 * - in case the lowercase versions are equal, return the order of the language
 * tags
 * - If the strings are still the same, return the order of the original inner
 * values.
 *
 * This gives us a strict ordering on strings.
 */
class StringSortComparator {
 public:
  constexpr StringSortComparator(const StringSortComparator&) = default;
  // Convert an rdf-literal "value"@lang (langtag is optional)
  // to the first possible literal with the same case-insensitive value
  // ("VALUE") in this case. This is done by conversion to uppercase
  // (uppercase comes before lowercase in ASCII/Utf) and removing
  // possible language tags.
  static std::string rdfLiteralToValueForLT(const std::string& input) {
    auto rhs_string = ad_utility::getUppercaseUtf8(input);
    auto split = StringSortComparator::extractComparable(rhs_string);
    if (split.isLiteral && !split.langtag.empty()) {
      // get rid of possible langtags to move to the beginning of the
      // range
      rhs_string = '\"' + std::string(split.val) + '\"';
    }
    return rhs_string;
  }

  // Convert an rdf-literal "value"@lang (langtag is optional)
  // to the last possible literal with the same case-insensitive value
  // ("value"@^?) in this case where ^? denotes the highest possible ASCII value
  // of 127. This is done by conversion to lowercase
  // (uppercase comes before lowercase in ASCII/Utf) and adding
  // the said artificial language tag with a higher ascii value than all
  // possible other langtags. (valid RDF langtags only contain ascii characters)
  static std::string rdfLiteralToValueForGT(const std::string& input) {
    auto rhs_string = ad_utility::getLowercaseUtf8(input);
    auto split2 = StringSortComparator::extractComparable(rhs_string);
    if (split2.isLiteral) {
      rhs_string = '\"' + std::string(split2.val) + '\"' + "@" + char(127);
    }
    return rhs_string;
  }

  StringSortComparator(bool ignoreCase = false) : _ignoreCase(ignoreCase) {}

  bool isIgnoreCase() const { return _ignoreCase; }

  /*
   * @brief The actual comparison operator.
   */
  bool operator()(std::string_view a, std::string_view b) const {
    if (!_ignoreCase) {
      return a < b;
    } else {
      auto splitA = extractComparable(a);
      auto splitB = extractComparable(b);
      if (splitA.isLiteral != splitB.isLiteral) {
        // only one is a literal, compare by the first character to separate
        // datatypes.
        return a < b;
      }
      return caseInsensitiveCompare(splitA, splitB);
    }
  }

  // we want to have the member const, but still be able
  // to copy assign in other places
  StringSortComparator& operator=(const StringSortComparator& other) {
    *const_cast<bool*>(&_ignoreCase) = other._ignoreCase;
    return *this;
  }

 private:
  // A rdf literal or iri split into its components
  struct SplitVal {
    SplitVal(bool lit, std::string_view v, std::string_view l)
        : isLiteral(lit), val(v), langtag(l) {}
    bool isLiteral;  // was the value an rdf-literal
    std::string_view
        val;  // the inner value, possibly stripped by trailing quotation marks
    std::string_view langtag;  // the language tag, possibly empty
  };

  /// @brief split a literal or iri into its components
  static SplitVal extractComparable(std::string_view a) {
    std::string_view res = a;
    bool isLiteral = false;
    std::string_view langtag;
    if (ad_utility::startsWith(res, "\"")) {
      // In the case of prefix filters we might also have
      // Literals that do not have the closing quotation mark
      isLiteral = true;
      res.remove_prefix(1);
      auto endPos = ad_utility::findLiteralEnd(res, "\"");
      if (endPos != string::npos) {
        // this should also be fine if there is no langtag (endPos == size()
        // according to cppreference.com
        langtag = res.substr(endPos + 1);
        res.remove_suffix(res.size() - endPos);
      } else {
        langtag = "";
      }
    }
    return {isLiteral, res, langtag};
  }

  /// @brief the inner comparison logic
  static bool caseInsensitiveCompare(const SplitVal& a, const SplitVal& b) {
    auto aLower = ad_utility::getLowercaseUtf8(a.val);
    auto bLower = ad_utility::getLowercaseUtf8(b.val);
    const auto result = std::mismatch(aLower.cbegin(), aLower.cend(),
                                      bLower.cbegin(), bLower.cend());
    if (result.second == bLower.end()) {
      if (result.first == aLower.end()) {
        // In case a and b are equal wrt case-insensitivity we sort by the
        // language tag. If this also matches we return the actual order of the
        // inner string value. Thus we have a unique ordering that makes life
        // easier.
        return a.langtag != b.langtag ? a.langtag < b.langtag : a.val < b.val;
      }
      // b is a prefix of a, thus a is strictly "bigger"
      return false;
    }

    if (result.first == aLower.end()) {
      // a is a prefix of b
      return true;
    }

    // neither string is a prefix of the other, look at the first mismatch
    // character if we have reach here, both iterators are save to dereference.
    return *result.first < *result.second;
  }
  const bool _ignoreCase;
};

//! A vocabulary. Wraps a vector of strings
//! and provides additional methods for retrieval.
//! Template parameters that are supported are:
//! std::string -> no compression is applied
//! CompressedString -> prefix compression is applied
template <class StringType>
class Vocabulary {
  template <typename T, typename R = void>
  using enable_if_compressed =
      std::enable_if_t<std::is_same_v<T, CompressedString>>;

  template <typename T, typename R = void>
  using enable_if_uncompressed =
      std::enable_if_t<!std::is_same_v<T, CompressedString>>;

 public:
  template <
      typename = std::enable_if_t<std::is_same_v<StringType, string> ||
                                  std::is_same_v<StringType, CompressedString>>>

  Vocabulary(){};

  // variable for dispatching
  static constexpr bool _isCompressed =
      std::is_same_v<StringType, CompressedString>;

  virtual ~Vocabulary() = default;

  //! clear all the contents, but not the settings for prefixes etc
  void clear() {
    _words.clear();
    _externalLiterals.clear();
  }
  //! Read the vocabulary from file.
  void readFromFile(const string& fileName, const string& extLitsFileName = "");

  //! Write the vocabulary to a file.
  // We don't need to write compressed vocabularies with the current index
  // building procedure
  template <typename U = StringType, typename = enable_if_uncompressed<U>>
  void writeToFile(const string& fileName) const;

  //! Write to binary file to prepare the merging. Format:
  // 4 Bytes strlen, then character bytes, then 8 bytes zeros for global id
  template <typename U = StringType, typename = enable_if_uncompressed<U>>
  void writeToBinaryFileForMerging(const string& fileName) const;

  //! Append a word to the vocabulary. Wraps the std::vector method.
  void push_back(const string& word) {
    if constexpr (_isCompressed) {
      _words.push_back(compressPrefix(word));
    } else {
      _words.push_back(word);
    }
  }

  //! Get the word with the given id or an empty optional if the
  //! word is not in the vocabulary.
  //! Only enabled when uncompressed which also means no externalization
  template <typename U = StringType, typename = enable_if_uncompressed<U>>
  const std::optional<std::reference_wrapper<const string>> operator[](
      Id id) const {
    if (id < _words.size()) {
      return _words[static_cast<size_t>(id)];
    } else {
      return std::nullopt;
    }
  }

  //! Get the word with the given id or an empty optional if the
  //! word is not in the vocabulary. Returns an lvalue because compressed or
  //! externalized words don't allow references
  template <typename U = StringType, typename = enable_if_compressed<U>>
  const std::optional<string> idToOptionalString(Id id) const {
    if (id < _words.size()) {
      // internal, prefixCompressed word
      return expandPrefix(_words[static_cast<size_t>(id)]);
    } else if (id == ID_NO_VALUE) {
      return std::nullopt;
    } else {
      // this word must be externalized
      id -= _words.size();
      AD_CHECK(id < _externalLiterals.size());
      return _externalLiterals[id];
    }
  }

  //! Get the word with the given id.
  //! lvalue for compressedString and const& for string-based vocabulary
  AccessReturnType_t<StringType> at(Id id) const {
    if constexpr (_isCompressed) {
      return expandPrefix(_words[static_cast<size_t>(id)]);
    } else {
      return _words[static_cast<size_t>(id)];
    }
  }

  // AccessReturnType_t<StringType> at(Id id) const { return operator[](id); }

  //! Get the number of words in the vocabulary.
  size_t size() const { return _words.size(); }

  //! Reserve space for the given number of words.
  void reserve(unsigned int n) { _words.reserve(n); }

  //! Get an Id from the vocabulary for some "normal" word.
  //! Return value signals if something was found at all.
  bool getId(const string& word, Id* id) const {
    if (!shouldBeExternalized(word)) {
      *id = lower_bound(word);
      // works for the case insensitive version because
      // of the strict ordering.
      return *id < _words.size() && at(*id) == word;
    }
    bool success = _externalLiterals.getId(word, id);
    *id += _words.size();
    return success;
  }

  Id getValueIdForLT(const string& indexWord) const {
    Id lb = lower_bound(indexWord);
    return lb;
  }

  Id getValueIdForLE(const string& indexWord) const {
    Id lb = lower_bound(indexWord);
    if ((lb < _words.size() && lb > 0) && at(lb) != indexWord) {
      // If indexWord is not in the vocab, it may be that
      // we ended up one too high. We don't want this to match in LE.
      // The one before is actually lower than index word but that's fine
      // b/c of the LE comparison.
      --lb;
    }
    return lb;
  }

  Id getValueIdForGT(const string& indexWord) const {
    Id lb = lower_bound(indexWord);
    if ((lb < _words.size() && lb > 0) && at(lb) != indexWord) {
      // If indexWord is not in the vocab, lb points to the next value.
      // But if this happened, we know that there is nothing in between and
      // it's
      // fine to use one lower
      --lb;
    }
    return lb;
  }

  Id getValueIdForGE(const string& indexWord) const {
    Id lb = lower_bound(indexWord);
    return lb;
  }

  //! Get an Id range that matches a prefix.
  //! Return value signals if something was found at all.
  bool getIdRangeForFullTextPrefix(const string& word, IdRange* range) const {
    AD_CHECK_EQ(word[word.size() - 1], PREFIX_CHAR);
    range->_first = lower_bound(word.substr(0, word.size() - 1));
    range->_last =
        upper_bound(word.substr(0, word.size() - 1), range->_first,
                    PrefixComparator<StringType>(word.size() - 1, this)) -
        1;
    bool success = range->_first < _words.size() &&
                   ad_utility::startsWith(at(range->_first),
                                          word.substr(0, word.size() - 1)) &&
                   range->_last < _words.size() &&
                   ad_utility::startsWith(at(range->_last),
                                          word.substr(0, word.size() - 1)) &&
                   range->_first <= range->_last;
    if (success) {
      AD_CHECK_LT(range->_first, _words.size());
      AD_CHECK_LT(range->_last, _words.size());
    }
    return success;
  }

  // only used during Index building, not needed for compressed vocabulary
  template <typename U = StringType, typename = enable_if_uncompressed<U>>
  void createFromSet(const ad_utility::HashSet<StringType>& set);

  template <typename U = StringType, typename = enable_if_uncompressed<U>>
  ad_utility::HashMap<string, Id> asMap();

  static bool isLiteral(const string& word);
  static bool isExternalizedLiteral(const string& word);

  bool shouldBeExternalized(const string& word) const;

  bool shouldEntityBeExternalized(const string& word) const;

  bool shouldLiteralBeExternalized(const string& word) const;

  // only still needed for text vocabulary
  template <typename U = StringType, typename = enable_if_uncompressed<U>>
  void externalizeLiterals(const string& fileName);

  void externalizeLiteralsFromTextFile(const string& textFileName,
                                       const string& outFileName) {
    _externalLiterals.buildFromTextFile(textFileName, outFileName);
  }

  const ExternalVocabulary& getExternalVocab() const {
    return _externalLiterals;
  }

  static string getLanguage(const string& literal);

  // _____________________________________________________
  template <typename U = StringType, typename = enable_if_compressed<U>>
  string expandPrefix(const CompressedString& word) const;

  // _____________________________________________
  template <typename U = StringType, typename = enable_if_compressed<U>>
  CompressedString compressPrefix(const string& word) const;

  // initialize compression with a list of prefixes
  // The prefixes do not have to be in any specific order
  //
  // StringRange prefixes can be of any type where
  // for (const string& el : prefixes {}
  // works
  template <typename StringRange, typename U = StringType,
            typename = enable_if_compressed<U>>
  void initializePrefixes(const StringRange& prefixes);

  // set the list of prefixes for words which will become part of the
  // externalized vocabulary. Good for entity names that normally don't appear
  // in queries or results but take a lot of space (e.g. Wikidata statements)
  //
  // StringRange prefixes can be of any type where
  // for (const string& el : prefixes {}
  // works
  template <class StringRange>
  void initializeExternalizePrefixes(const StringRange& prefixes);

  // set the list of languages (in "en" language code format) that should be
  // kept internalized. By default this is just English
  //
  // StringRange prefixes can be of any type where
  // for (const string& el : prefixes {}
  // works
  template <class StringRange>
  void initializeInternalizedLangs(const StringRange& prefixes);

  // Compress the file at path infile, write to file at outfile using the
  // specified prefixes.
  // Arguments:
  //   infile - path to original vocabulary, one word per line
  //   outfile- output path. Will be overwritten by also one word per line
  //            in the same order as the infile
  //   prefixes - a list of prefixes which we will compress
  template <typename U = StringType, typename = enable_if_compressed<U>>
  static void prefixCompressFile(const string& infile, const string& outfile,
                                 const vector<string>& prefixes);

  // ___________________________________________________________________
  void setCaseInsensitiveOrdering(bool ignoreCase) {
    _caseComparator = StringSortComparator(ignoreCase);
  }

  // ___________________________________________________________________
  bool isCaseInsensitiveOrdering() const {
    return _caseComparator.isIgnoreCase();
  }

  // _____________________________________________________________________
  const StringSortComparator& getCaseComparator() const {
    return _caseComparator;
  }

 private:
  // Wraps std::lower_bound and returns an index instead of an iterator
  Id lower_bound(const string& word) const {
    if constexpr (_isCompressed) {
      auto pred = [this](const CompressedString& a, const string& b) {
        return this->_caseComparator(this->expandPrefix(a), b);
      };
      return static_cast<Id>(
          std::lower_bound(_words.begin(), _words.end(), word, pred) -
          _words.begin());
    } else {
      return static_cast<Id>(std::lower_bound(_words.begin(), _words.end(),
                                              word, _caseComparator) -
                             _words.begin());
    }
  }

  // _______________________________________________________________
  Id upper_bound(const string& word) const {
    if constexpr (_isCompressed) {
      auto pred = [this](const string& a, const CompressedString& b) {
        return this->_caseComparator(a, this->expandPrefix(b));
      };
      return static_cast<Id>(
          std::upper_bound(_words.begin(), _words.end(), word, pred) -
          _words.begin());
    } else {
      return static_cast<Id>(
          std::upper_bound(_words.begin(), _words.end(), word) - _words.begin(),
          _caseComparator);
    }
  }

  // Wraps std::lower_bound and returns an index instead of an iterator
  Id lower_bound(const string& word, size_t first) const {
    if constexpr (_isCompressed) {
      auto pred = [this](const CompressedString& a, const string& b) {
        return this->_caseComparator(this->expandPrefix(a), b);
      };
      return static_cast<Id>(
          std::lower_bound(_words.begin() + first, _words.end(), word, pred) -
          _words.begin());
    } else {
      return static_cast<Id>(std::lower_bound(_words.begin() + first,
                                              _words.end(), word,
                                              _caseComparator) -
                             _words.begin());
    }
  }

  // Wraps std::upper_bound and returns an index instead of an iterator
  // Only compares words that have at most word.size() or to prefixes of
  //
  // that length otherwise.
  Id upper_bound(const string& word, size_t first,
                 PrefixComparator<StringType> comp) const {
    AD_CHECK_LE(first, _words.size());
    // the prefix comparator handles the case-insensitive compare if activated
    typename vector<StringType>::const_iterator it =
        std::upper_bound(_words.begin() + first, _words.end(), word, comp);
    Id retVal =
        (it == _words.end()) ? size() : static_cast<Id>(it - _words.begin());
    AD_CHECK_LE(retVal, size());
    return retVal;
  }

  // TODO<joka921> these following two members are only used with the
  // compressed vocabulary. They don't use much space if empty, but still it
  // would be cleaner to throw them out when using the uncompressed version
  //
  // list of all prefixes and their codewords, sorted descending by the length
  // of the prefixes. Used for lookup when encoding strings
  std::vector<Prefix> _prefixVec{};

  // maps (numeric) keys to the prefix they encode.
  // currently only 128 prefixes are supported.
  array<std::string, NUM_COMPRESSION_PREFIXES> _prefixMap{""};

  // If a word starts with one of those prefixes it will be externalized
  vector<std::string> _externalizedPrefixes;

  // If a word uses one of these language tags it will be internalized,
  // defaults to English
  vector<std::string> _internalizedLangs{"en"};

  vector<StringType> _words;
  ExternalVocabulary _externalLiterals;
  StringSortComparator _caseComparator;
};

#include "./VocabularyImpl.h"
