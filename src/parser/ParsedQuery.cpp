// Copyright 2014, University of Freiburg,
// Chair of Algorithms and Data Structures.
// Author: Björn Buchhold (buchhold@informatik.uni-freiburg.de)

#include "ParsedQuery.h"

#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../util/Conversions.h"
#include "../util/StringUtils.h"
#include "ParseException.h"

using std::string;
using std::vector;

// _____________________________________________________________________________
string ParsedQuery::asString() const {
  std::ostringstream os;

  // PREFIX
  os << "PREFIX: {";
  for (size_t i = 0; i < _prefixes.size(); ++i) {
    os << "\n\t" << _prefixes[i].asString();
    if (i + 1 < _prefixes.size()) {
      os << ',';
    }
  }
  os << "\n}";

  // SELECT
  os << "\nSELECT: {\n\t";
  for (size_t i = 0; i < _selectedVariables.size(); ++i) {
    os << _selectedVariables[i];
    if (i + 1 < _selectedVariables.size()) {
      os << ", ";
    }
  }
  os << "\n}";

  // WHERE
  os << "\nWHERE: \n";
  _rootGraphPattern->toString(os, 1);

  os << "\nLIMIT: " << (_limit.size() > 0 ? _limit : "no limit specified");
  os << "\nTEXTLIMIT: "
     << (_textLimit.size() > 0 ? _textLimit : "no limit specified");
  os << "\nOFFSET: " << (_offset.size() > 0 ? _offset : "no offset specified");
  os << "\nDISTINCT modifier is " << (_distinct ? "" : "not ") << "present.";
  os << "\nREDUCED modifier is " << (_reduced ? "" : "not ") << "present.";
  os << "\nORDER BY: ";
  if (_orderBy.size() == 0) {
    os << "not specified";
  } else {
    for (auto& key : _orderBy) {
      os << key._key << (key._desc ? " (DESC)" : " (ASC)") << "\t";
    }
  }
  os << "\n";
  return os.str();
}

// _____________________________________________________________________________
string SparqlPrefix::asString() const {
  std::ostringstream os;
  os << "{" << _prefix << ": " << _uri << "}";
  return os.str();
}

// _____________________________________________________________________________
PropertyPath::PropertyPath(Operation op, uint16_t limit, const std::string& iri,
                           std::initializer_list<PropertyPath> children)
    : _operation(op), _limit(limit), _iri(iri), _children(children) {}

// _____________________________________________________________________________
void PropertyPath::writeToStream(std::ostream& out) const {
  switch (_operation) {
    case Operation::ALTERNATIVE:
      out << "(";
      if (_children.size() > 0) {
        _children[0].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")|(";
      if (_children.size() > 1) {
        _children[1].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")";
      break;
    case Operation::INVERSE:
      out << "^(";
      if (_children.size() > 0) {
        _children[0].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")";
      break;
    case Operation::IRI:
      out << _iri;
      break;
    case Operation::SEQUENCE:
      out << "(";
      if (_children.size() > 0) {
        _children[0].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")/(";
      if (_children.size() > 1) {
        _children[1].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")";
      break;
    case Operation::TRANSITIVE:
      out << "(";
      if (_children.size() > 0) {
        _children[0].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")*";
      break;
    case Operation::TRANSITIVE_MAX:
      out << "(";
      if (_children.size() > 0) {
        _children[0].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")";
      if (_limit == 1) {
        out << "?";
      } else {
        out << "*" << _limit;
      }
      break;
    case Operation::TRANSITIVE_MIN:
      out << "(";
      if (_children.size() > 0) {
        _children[0].writeToStream(out);
      } else {
        out << "missing" << std::endl;
      }
      out << ")+";
      break;
  }
}

// _____________________________________________________________________________
std::string PropertyPath::asString() const {
  std::stringstream s;
  writeToStream(s);
  return s.str();
}

// _____________________________________________________________________________
void PropertyPath::computeCanBeNull() {
  _can_be_null = _children.size() > 0;
  for (PropertyPath& p : _children) {
    p.computeCanBeNull();
    _can_be_null &= p._can_be_null;
  }
  if (_operation == Operation::TRANSITIVE ||
      _operation == Operation::TRANSITIVE_MAX ||
      (_operation == Operation::TRANSITIVE_MIN && _limit == 0)) {
    _can_be_null = true;
  }
}

// _____________________________________________________________________________
std::ostream& operator<<(std::ostream& out, const PropertyPath& p) {
  p.writeToStream(out);
  return out;
}

// _____________________________________________________________________________
string SparqlTriple::asString() const {
  std::ostringstream os;
  os << "{s: " << _s << ", p: " << _p << ", o: " << _o << "}";
  return os.str();
}

// _____________________________________________________________________________
string SparqlFilter::asString() const {
  std::ostringstream os;
  os << "FILTER(" << _lhs;
  switch (_type) {
    case EQ:
      os << " < ";
      break;
    case NE:
      os << " != ";
      break;
    case LT:
      os << " < ";
      break;
    case LE:
      os << " <= ";
      break;
    case GT:
      os << " > ";
      break;
    case GE:
      os << " >= ";
      break;
    case LANG_MATCHES:
      os << " LANG_MATCHES ";
      break;
    case PREFIX:
      os << " PREFIX ";
      break;
    case SparqlFilter::REGEX:
      os << " REGEX ";
      if (_regexIgnoreCase) {
        os << "ignoring case ";
      }
      break;
  }
  os << _rhs << ")";
  return os.str();
}

// _____________________________________________________________________________
void ParsedQuery::expandPrefixes() {
  ad_utility::HashMap<string, string> prefixMap;
  prefixMap["ql"] = "<QLever-internal-function/>";
  for (const auto& p : _prefixes) {
    prefixMap[p._prefix] = p._uri;
  }

  vector<GraphPattern*> graphPatterns;
  graphPatterns.push_back(_rootGraphPattern.get());
  // Traverse the graph pattern tree using dfs expanding the prefixes in every
  // pattern.
  while (!graphPatterns.empty()) {
    GraphPattern* pattern = graphPatterns.back();
    graphPatterns.pop_back();
    for (const std::shared_ptr<GraphPatternOperation>& p : pattern->_children) {
      if (p->_type == GraphPatternOperation::Type::SUBQUERY) {
        // Pass the prefixes to the subquery and expand them.
        p->_subquery->_prefixes = _prefixes;
        p->_subquery->expandPrefixes();
      } else {
        for (const std::shared_ptr<GraphPattern>& pattern :
             p->_childGraphPatterns) {
          graphPatterns.push_back(pattern.get());
        }
      }
    }

    for (auto& trip : pattern->_whereClauseTriples) {
      expandPrefix(trip._s, prefixMap);
      expandPrefix(trip._p, prefixMap);
      if (trip._p._operation == PropertyPath::Operation::IRI &&
          trip._p._iri.find("in-context") != string::npos) {
        auto tokens = ad_utility::split(trip._o, ' ');
        trip._o = "";
        for (size_t i = 0; i < tokens.size(); ++i) {
          expandPrefix(tokens[i], prefixMap);
          trip._o += tokens[i];
          if (i + 1 < tokens.size()) {
            trip._o += " ";
          }
        }
      } else {
        expandPrefix(trip._o, prefixMap);
      }
    }
    for (auto& f : pattern->_filters) {
      expandPrefix(f._lhs, prefixMap);
      expandPrefix(f._rhs, prefixMap);
    }
  }
}

// _____________________________________________________________________________
void ParsedQuery::expandPrefix(
    PropertyPath& item, const ad_utility::HashMap<string, string>& prefixMap) {
  // Use dfs to process all leaves of the proprety path tree.
  std::vector<PropertyPath*> to_process;
  to_process.push_back(&item);
  while (!to_process.empty()) {
    PropertyPath* p = to_process.back();
    to_process.pop_back();
    if (p->_operation == PropertyPath::Operation::IRI) {
      expandPrefix(p->_iri, prefixMap);
    } else {
      for (PropertyPath& c : p->_children) {
        to_process.push_back(&c);
      }
    }
  }
}

// _____________________________________________________________________________
void ParsedQuery::expandPrefix(
    string& item, const ad_utility::HashMap<string, string>& prefixMap) {
  if (!ad_utility::startsWith(item, "?") &&
      !ad_utility::startsWith(item, "<")) {
    std::optional<string> langtag = std::nullopt;
    if (ad_utility::startsWith(item, "@")) {
      auto secondPos = item.find("@", 1);
      if (secondPos == string::npos) {
        throw ParseException(
            "langtaged predicates must have form @lang@ActualPredicate. Second "
            "@ is missing in " +
            item);
      }
      langtag = item.substr(1, secondPos - 1);
      item = item.substr(secondPos + 1);
    }

    size_t i = item.find(':');
    size_t from = item.find("^^");
    if (from == string::npos) {
      from = 0;
    } else {
      from += 2;
    }
    if (i != string::npos && i >= from &&
        prefixMap.count(item.substr(from, i - from)) > 0) {
      string prefixUri = prefixMap.find(item.substr(from, i - from))->second;
      if (from == 0) {
        item = prefixUri.substr(0, prefixUri.size() - 1) + item.substr(i + 1) +
               '>';
      } else {
        item = item.substr(0, from) +
               prefixUri.substr(0, prefixUri.size() - 1) + item.substr(i + 1) +
               '>';
      }
    }
    if (langtag) {
      item =
          ad_utility::convertToLanguageTaggedPredicate(item, langtag.value());
    }
  }
}

void ParsedQuery::parseAliases() {
  for (size_t i = 0; i < _selectedVariables.size(); i++) {
    const std::string& var = _selectedVariables[i];
    if (var[0] == '(') {
      // remove the leading and trailing bracket
      std::string inner = var.substr(1, var.size() - 2);
      // Replace the variable in the selected variables array with the aliased
      // name.
      _selectedVariables[i] = parseAlias(inner);
    }
  }
  for (size_t i = 0; i < _orderBy.size(); i++) {
    OrderKey& key = _orderBy[i];
    if (key._key[0] == '(') {
      // remove the leading and trailing bracket
      std::string inner = key._key.substr(1, key._key.size() - 2);
      // Preserve the descending or ascending order but change the key name.
      key._key = parseAlias(inner);
    }
  }
}

// _____________________________________________________________________________
std::string ParsedQuery::parseAlias(const std::string& alias) {
  std::string newVarName = "";
  std::string lowerInner = ad_utility::getLowercaseUtf8(alias);
  if (ad_utility::startsWith(lowerInner, "count") ||
      ad_utility::startsWith(lowerInner, "group_concat") ||
      ad_utility::startsWith(lowerInner, "first") ||
      ad_utility::startsWith(lowerInner, "last") ||
      ad_utility::startsWith(lowerInner, "sample") ||
      ad_utility::startsWith(lowerInner, "min") ||
      ad_utility::startsWith(lowerInner, "max") ||
      ad_utility::startsWith(lowerInner, "sum") ||
      ad_utility::startsWith(lowerInner, "avg")) {
    Alias a;
    a._isAggregate = true;
    size_t pos = lowerInner.find(" as ");
    if (pos == std::string::npos) {
      throw ParseException("Alias (" + alias +
                           ") is malformed: keyword 'as' is missing or not "
                           "surrounded by spaces.");
    }
    // skip the leading space of the 'as'
    pos++;
    newVarName = alias.substr(pos + 2);
    newVarName = ad_utility::strip(newVarName, " \t\n");
    a._outVarName = newVarName;
    a._function = alias;

    // find the second opening bracket
    pos = alias.find('(', 1);
    pos++;
    while (pos < alias.size() &&
           ::std::isspace(static_cast<unsigned char>(alias[pos]))) {
      pos++;
    }
    if (lowerInner.compare(pos, 8, "distinct") == 0) {
      // skip the distinct and any space after it
      pos += 8;
      while (pos < alias.size() &&
             ::std::isspace(static_cast<unsigned char>(alias[pos]))) {
        pos++;
      }
    }
    size_t start = pos;
    while (pos < alias.size() &&
           !::std::isspace(static_cast<unsigned char>(alias[pos]))) {
      pos++;
    }
    if (pos == start || pos >= alias.size()) {
      throw ParseException(
          "Alias (" + alias +
          ") is malformed: no input variable given (e.g. COUNT(?a))");
    }

    a._inVarName = alias.substr(start, pos - start - 1);
    bool isUnique = true;
    // check if another alias for the output variable already exists:
    for (const Alias& other : _aliases) {
      if (other._outVarName == a._outVarName) {
        // Check if the aliases are equal, otherwise throw an exception
        if (other._isAggregate != a._isAggregate ||
            other._function != a._function) {
          // TODO(florian): For a proper comparison the alias would need to have
          // been parsed fully already. As the alias is still stored as a string
          // at this point the comparison of two aliases is also string based.
          throw ParseException(
              "Two aliases try to bind values to the variable " +
              a._outVarName);
        } else {
          isUnique = false;
          break;
        }
      }
    }
    if (isUnique) {
      // only add the alias if it doesn't already exist
      _aliases.push_back(a);
    }
  } else {
    throw ParseException("Unknown or malformed alias: (" + alias + ")");
  }
  return newVarName;
}

// _____________________________________________________________________________
ParsedQuery::GraphPattern::~GraphPattern() {}

// _____________________________________________________________________________
ParsedQuery::GraphPattern::GraphPattern(GraphPattern&& other)
    : _whereClauseTriples(std::move(other._whereClauseTriples)),
      _filters(std::move(other._filters)),
      _optional(other._optional),
      _children(std::move(other._children)) {
  other._children.clear();
}

// _____________________________________________________________________________
ParsedQuery::GraphPattern::GraphPattern(const GraphPattern& other)
    : _whereClauseTriples(other._whereClauseTriples),
      _filters(other._filters),
      _optional(other._optional) {
  _children.reserve(other._children.size());
  for (const std::shared_ptr<GraphPatternOperation>& g : other._children) {
    _children.push_back(std::make_shared<GraphPatternOperation>(*g));
  }
}

// _____________________________________________________________________________
ParsedQuery::GraphPattern& ParsedQuery::GraphPattern::operator=(
    const ParsedQuery::GraphPattern& other) {
  _whereClauseTriples = std::vector<SparqlTriple>(other._whereClauseTriples);
  _filters = std::vector<SparqlFilter>(other._filters);
  _optional = other._optional;
  _children.clear();
  _children.reserve(other._children.size());
  for (const std::shared_ptr<GraphPatternOperation>& g : other._children) {
    _children.push_back(std::make_shared<GraphPatternOperation>(*g));
  }
  return *this;
}

// _____________________________________________________________________________
void ParsedQuery::GraphPattern::toString(std::ostringstream& os,
                                         int indentation) const {
  for (int j = 1; j < indentation; ++j) os << "  ";
  os << "{";
  for (size_t i = 0; i + 1 < _whereClauseTriples.size(); ++i) {
    os << "\n";
    for (int j = 0; j < indentation; ++j) os << "  ";
    os << _whereClauseTriples[i].asString() << ',';
  }
  if (_whereClauseTriples.size() > 0) {
    os << "\n";
    for (int j = 0; j < indentation; ++j) os << "  ";
    os << _whereClauseTriples.back().asString();
  }
  for (size_t i = 0; i + 1 < _filters.size(); ++i) {
    os << "\n";
    for (int j = 0; j < indentation; ++j) os << "  ";
    os << _filters[i].asString() << ',';
  }
  if (_filters.size() > 0) {
    os << "\n";
    for (int j = 0; j < indentation; ++j) os << "  ";
    os << _filters.back().asString();
  }
  for (const std::shared_ptr<GraphPatternOperation>& child : _children) {
    os << "\n";
    child->toString(os, indentation + 1);
  }
  os << "\n";
  for (int j = 1; j < indentation; ++j) os << "  ";
  os << "}";
}

// _____________________________________________________________________________
void ParsedQuery::GraphPattern::recomputeIds(size_t* id_count) {
  bool allocatedIdCounter = false;
  // Store the shared id_count on the stack. Unused by all but the first
  // recursive call but cheaper than heap allocation
  size_t id_count_store = 0;
  if (id_count == nullptr) {
    id_count = &id_count_store;
  }
  _id = *id_count;
  (*id_count)++;
  for (const std::shared_ptr<GraphPatternOperation>& op : _children) {
    switch (op->_type) {
      case GraphPatternOperation::Type::OPTIONAL:
      case GraphPatternOperation::Type::UNION:
        for (const std::shared_ptr<GraphPattern>& p : op->_childGraphPatterns) {
          p->recomputeIds(id_count);
        }
        break;
      case GraphPatternOperation::Type::TRANS_PATH:
        if (op->_pathData._childGraphPattern != nullptr) {
          op->_pathData._childGraphPattern->recomputeIds(id_count);
        }
        break;
      case GraphPatternOperation::Type::SUBQUERY:
        // subquery children have their own id space
        break;
    }
  }
}

// _____________________________________________________________________________
ParsedQuery::GraphPatternOperation::GraphPatternOperation(
    ParsedQuery::GraphPatternOperation::Type type,
    std::initializer_list<std::shared_ptr<ParsedQuery::GraphPattern>> children)
    : _type(type), _childGraphPatterns() {
  switch (_type) {
    case Type::OPTIONAL:
      if (children.size() != 1) {
        AD_THROW(ad_semsearch::Exception::CHECK_FAILED,
                 "Optional expects one sub graph pattern.");
      }
      break;
    case Type::UNION:
      if (children.size() != 2) {
        AD_THROW(ad_semsearch::Exception::CHECK_FAILED,
                 "Union expects two sub graph patterns.");
      }
      break;
    default:
      AD_THROW(ad_semsearch::Exception::CHECK_FAILED,
               "This constructor should only be used for UNION and OPTIONAL "
               "type operations.");
  }
  _childGraphPatterns.insert(_childGraphPatterns.end(), children.begin(),
                             children.end());
}

// _____________________________________________________________________________
ParsedQuery::GraphPatternOperation::GraphPatternOperation(
    ParsedQuery::GraphPatternOperation::Type type)
    : _type(type) {
  switch (_type) {
    case Type::SUBQUERY:
      new (&_subquery) std::shared_ptr<ParsedQuery>(nullptr);
      break;
    case Type::TRANS_PATH:
      new (&_pathData._left) std::string();
      new (&_pathData._right) std::string();
      new (&_pathData._childGraphPattern)
          std::shared_ptr<GraphPattern>(nullptr);
      _pathData._max = 0;
      _pathData._min = 0;
      break;
    default:
      AD_THROW(ad_semsearch::Exception::CHECK_FAILED,
               "This constructor should only be used for SUBQUERY"
               "type operations.");
  }
}

// _____________________________________________________________________________
ParsedQuery::GraphPatternOperation::GraphPatternOperation(
    GraphPatternOperation&& other)
    : _type(other._type) {
  if (_type == Type::SUBQUERY) {
    new (&_subquery) std::shared_ptr<ParsedQuery>(nullptr);
    _subquery = other._subquery;
    other._subquery = nullptr;
  } else if (_type == Type::TRANS_PATH) {
    new (&_pathData._left) std::string();
    new (&_pathData._right) std::string();
    new (&_pathData._childGraphPattern) std::shared_ptr<GraphPattern>(nullptr);
    _pathData = std::move(other._pathData);
    other._pathData._childGraphPattern = nullptr;
  } else {
    new (&_childGraphPatterns) std::vector<std::shared_ptr<GraphPattern>>();
    _childGraphPatterns = std::move(other._childGraphPatterns);
    other._childGraphPatterns.clear();
  }
}

// _____________________________________________________________________________
ParsedQuery::GraphPatternOperation::GraphPatternOperation(
    const GraphPatternOperation& other)
    : _type(other._type) {
  if (_type == Type::SUBQUERY) {
    new (&_subquery) std::shared_ptr<ParsedQuery>(nullptr);
    _subquery = std::make_shared<ParsedQuery>(*other._subquery);
  } else if (_type == Type::TRANS_PATH) {
    new (&_pathData._left) std::string();
    new (&_pathData._right) std::string();
    new (&_pathData._childGraphPattern) std::shared_ptr<GraphPattern>(nullptr);
    _pathData._childGraphPattern =
        std::make_shared<GraphPattern>(*other._pathData._childGraphPattern);
    _pathData._min = other._pathData._min;
    _pathData._max = other._pathData._max;
    _pathData._left = other._pathData._left;
    _pathData._right = other._pathData._right;
  } else {
    new (&_childGraphPatterns) std::vector<std::shared_ptr<GraphPattern>>();
    _childGraphPatterns.reserve(other._childGraphPatterns.size());
    for (const std::shared_ptr<GraphPattern>& child :
         other._childGraphPatterns) {
      _childGraphPatterns.push_back(std::make_shared<GraphPattern>(*child));
    }
  }
}

// _____________________________________________________________________________
ParsedQuery::GraphPatternOperation& ParsedQuery::GraphPatternOperation::
operator=(const ParsedQuery::GraphPatternOperation& other) {
  if (_type == Type::SUBQUERY) {
    _subquery.~shared_ptr();
  } else if (_type == Type::TRANS_PATH) {
    _pathData._childGraphPattern.~shared_ptr();
    _pathData._left.~string();
    _pathData._right.~string();
  } else {
    _childGraphPatterns.~vector();
  }
  _type = other._type;
  if (_type == Type::SUBQUERY) {
    new (&_subquery) std::shared_ptr<ParsedQuery>(nullptr);
    _subquery = std::make_shared<ParsedQuery>(*other._subquery);
  } else if (_type == Type::TRANS_PATH) {
    new (&_pathData._left) std::string();
    new (&_pathData._right) std::string();
    new (&_pathData._childGraphPattern) std::shared_ptr<GraphPattern>(nullptr);
    _pathData._childGraphPattern =
        std::make_shared<GraphPattern>(*other._pathData._childGraphPattern);
    _pathData._min = other._pathData._min;
    _pathData._max = other._pathData._max;
    _pathData._left = other._pathData._left;
    _pathData._right = other._pathData._right;
  } else {
    new (&_childGraphPatterns) std::vector<std::shared_ptr<GraphPattern>>();
    _childGraphPatterns.reserve(other._childGraphPatterns.size());
    for (const std::shared_ptr<GraphPattern>& p : other._childGraphPatterns) {
      _childGraphPatterns.push_back(std::make_shared<GraphPattern>(*p));
    }
  }
  return *this;
}

// _____________________________________________________________________________
ParsedQuery::GraphPatternOperation::~GraphPatternOperation() {
  if (_type == Type::SUBQUERY) {
    _subquery.~shared_ptr();
  } else if (_type == Type::TRANS_PATH) {
    _pathData._childGraphPattern.~shared_ptr();
    _pathData._left.~string();
    _pathData._right.~string();
  } else {
    _childGraphPatterns.~vector();
  }
}

// _____________________________________________________________________________
void ParsedQuery::GraphPatternOperation::toString(std::ostringstream& os,
                                                  int indentation) const {
  for (int j = 1; j < indentation; ++j) os << "  ";
  switch (_type) {
    case Type::OPTIONAL:
      os << "OPTIONAL ";
      _childGraphPatterns[0]->toString(os, indentation);
      break;
    case Type::UNION:
      _childGraphPatterns[0]->toString(os, indentation);
      os << " UNION ";
      _childGraphPatterns[1]->toString(os, indentation);
      break;
    case Type::SUBQUERY:
      if (_subquery != nullptr) {
        os << _subquery->asString();
      } else {
        os << "Missing Subquery\n";
      }
      break;
    case Type::TRANS_PATH:
      os << "TRANS PATH from " << _pathData._left << " to " << _pathData._right
         << " with at least " << _pathData._min << " and at most "
         << _pathData._max << " steps of ";
      if (_pathData._childGraphPattern != nullptr) {
        _pathData._childGraphPattern->toString(os, indentation);
      } else {
        os << "Missing graph pattern.";
      }
      break;
  }
}
